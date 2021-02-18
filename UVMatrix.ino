/*
     ESP32
     Data Out for WS2812 Pixels is Pin 23     
*/
#include "SPI.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include "NeoViaSPI.h"
#include "artNetPacket.h"

//networking
const char * ssid = "WIFI_SSID";
const char * password = "WIFI_PASSWORD";
IPAddress thisDevice(192,168,1,100);    //IP address of the ESP32
IPAddress gateway(192,168,1,254);       //IP address of yoour Default Gaetway/Router
IPAddress subnet(255,255,255,0);        //SUBNET Mask usualy leave as is

unsigned int artNetPort = 6454;
const short int maxPacketBufferSize = 530;
char packetBuffer[maxPacketBufferSize];
WiFiUDP udp;
short int packetSize=0;
artNetPacket dmxData;
IPAddress localMulticastIP(239, 0, 0, 57);

//DMX Config
const byte numberOfDMXUniverses = 24;
const unsigned short int universeRange[2] = {0,23};  //  [Starting Universe ID, Ending Universe ID] (inclusive)
//Set to 1 to only render to the LEDs when ALL DMX frames havea arrived
byte renderOnlyIfAllFramesArrive = 1;
byte broadcastReceive = 1;


byte frameChecks[numberOfDMXUniverses][2];
unsigned short int dmxIndex=0, frameCntBreak=0, frameTicks=0, frameTCnt=0, innerFTCnt=0, startPixel=0, pxIndex=0;
const byte universePixelCount[numberOfDMXUniverses] = {170,170,170,170,170,170,170,170,170,170,170,170,170,170,170,170,170,170,170,170,170,170,170,50};
const byte numberOfPixelsPerUniverse = 170;
const unsigned short int numLeds = 3960;

NeoViaSPI leds = NeoViaSPI(numLeds);
byte tempColour[3] = {0,0,0};

void setup()
{

  Serial.begin(115200);
  Serial.print("\r\n\r\n");
  Serial.print("ArtNet Mode\r\n");

  //Init SPI for physical Pixel Driver
  SPI.begin();
  SPI.setBitOrder(MSBFIRST);
  SPI.setFrequency(3333333);
  //clear all pixels
  renderLEDs();

  //Eable WIFI
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("UV_MATRIX");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
        delay(100);
        Serial.print(".");
  }
  //Sets the IP details of this ESP32
  WiFi.config(thisDevice, gateway, subnet);
  Serial.print("\r\nONLINE\t");
  Serial.print(WiFi.localIP());

  //Set up UDP
  if(broadcastReceive)
  {
    udp.begin(WiFi.localIP(), artNetPort);
  }
  else
  {
    udp.beginMulticast(localMulticastIP, artNetPort);
  }

  //set up DMX Frame Check-in system
  for(frameTCnt=0; frameTCnt<numberOfDMXUniverses; frameTCnt++)
  {
    //Set This frames ID
    frameChecks[frameTCnt][0] = frameTCnt;
    //Set this frame to be clear = 0
    frameChecks[frameTCnt][1] = 0;
  }

}

void renderLEDs()
{
  leds.encode();
  SPI.writeBytes(leds.neoBits, leds._NeoBitsframeLength);
}

//ARTNET STUFF
void pollDMX()
{
     packetSize = udp.parsePacket();
     if(packetSize==maxPacketBufferSize)
     {
        udp.read(packetBuffer, maxPacketBufferSize);
        //Serial.printf("\r\n\tUDP Packet Received for Univirse\t[%d.%d]", packetBuffer[14], packetBuffer[15]);
        //packetBuffer[14] is the UNIVERSE byte check that it is within the range configured above
        if( universeID(packetBuffer[14], packetBuffer[15])>=universeRange[0] && universeID(packetBuffer[14], packetBuffer[15])<=universeRange[1])
        {
          dmxData.parseArtNetPacket(packetBuffer);
        }
     }
     udp.flush();
}

void artNetToSPI(byte panelID)
{
  startPixel = panelID * numberOfPixelsPerUniverse;
  dmxIndex = 0;

  for(pxIndex=startPixel; pxIndex<startPixel+universePixelCount[panelID]; pxIndex++)
  {
    tempColour[0] = dmxData.data[dmxIndex];
    tempColour[1] = dmxData.data[dmxIndex+1];
    tempColour[2] = dmxData.data[dmxIndex+2];
    leds.setPixel(pxIndex, tempColour);
    dmxIndex+=3;
  }

  //Check if RenderAllFrames mode is enabled
  if(renderOnlyIfAllFramesArrive)
  {
    //Check in this frames data to see if the entire canvas should be rendered
    if(checkFrame(panelID))
    {
      renderLEDs();
    }
  }
  else
  {
    renderLEDs();
  }
}

byte checkFrame(byte dmxFrameID)
{
  frameTicks = 0;
  
  //If all frames are OLD begin full check
  if(frameCntBreak==0)
  {
    frameCntBreak=1;
  }
  
  //Go through all frames and see which ones data has arrive
  for(frameTCnt=0; frameTCnt<numberOfDMXUniverses; frameTCnt++)
  {
    if( frameChecks[frameTCnt][0] == dmxFrameID && frameChecks[frameTCnt][1]==0)
    {
      //Current frame was old and is now NEW
      frameChecks[frameTCnt][1] = 1;
      break;
    }
    else if(frameChecks[frameTCnt][0] == dmxFrameID && frameChecks[frameTCnt][1]==1)
    {
      //Current frame was already writen to in this round
      //Frames are out of sync reject render this time round
      frameCntBreak=0;
      //clear all frame checks
      for(innerFTCnt=0; innerFTCnt<numberOfDMXUniverses; innerFTCnt++)
      {
        frameChecks[innerFTCnt][1]=0;
      }
      break;
    }
  }
  //if still checking
  if(frameCntBreak)
  {
    for(frameTCnt=0; frameTCnt<numberOfDMXUniverses; frameTCnt++)
    {
      if(frameChecks[frameTCnt][1]==1)
      {
        frameTicks++;
      }
    }
    //if all frames are checked
    if(frameTicks==numberOfDMXUniverses)
    {
      frameCntBreak=0;
      //Frames IN sync
      //clear checks
      for(frameTCnt=0; frameTCnt<numberOfDMXUniverses; frameTCnt++)
      {
        frameChecks[frameTCnt][1]=0;
      }
      //return render
      return 1;
    }
  }
  //return DO NOT render
  return 0;
}

void loop()
{    
    //ArtNet Mode
    pollDMX();
    if(dmxData.hasChanged)
    {
      //Serial.printf("\r\n\t\tProcessing DMX DATA For U\t[%d]\tSUBNET\t[%d]\tUNIV\t[%d]", universeID(dmxData.universe[0], dmxData.universe[1]), dmxData.universe[1], dmxData.universe[0] );
      dmxData.hasChanged = 0;
      //artNetToSPI(dmxData.universe[0]);
      artNetToSPI( universeID( dmxData.universe[0], dmxData.universe[1] ) );
    }
    yield(); 
}

byte universeID(byte uniID, byte subnet)
{
  return (subnet*16) + uniID;
}
