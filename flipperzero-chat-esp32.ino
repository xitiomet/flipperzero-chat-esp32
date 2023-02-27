/* FlipperZero Sub-Ghz Chat Bridge
 * 
 * A sketch for the ESP32 to allow communication with the FlipperZero's SubGhz chat feature
 *  Requires a TI-CC1101 to be connected to the SPI port
 * 
 * xitiomet - 2023
 * openstatic.org
 *
 * Hookup between devices:
 *
 *  ESP-32   to   CC1101
 *  ------   --   ------
 *  3.3v     to   VIN
 *  GND      to   GND
 *  5        to   CS
 *  18       to   SCLK
 *  19       to   MISO / SO
 *  23       to   MOSI / SI
 */

#define ARDUINOJSON_USE_LONG_LONG 1
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <WebSocketsServer.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <FFat.h>
#include "./DNSServer.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define OLED_RESET -1
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define SCREEN_ADDRESS 0x3c ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define PIX_ROW_1 0
#define PIX_ROW_1p5 8
#define PIX_ROW_2 24
#define PIX_ROW_3 40
#define PIX_ROW_4 56

#define MAX_MEMBERS 40
#define SS_PIN    5
float frequency = 433.92;

WiFiMulti wifiMulti;
WebSocketsServer webSocketServer(81);
WebServer httpServer(80);
IPAddress apIP(10, 10, 10, 1);
DNSServer dnsServer;

String members[MAX_MEMBERS];
String member_sources[MAX_MEMBERS];
int member_nums[MAX_MEMBERS];
int member_rssi[MAX_MEMBERS];

uint8_t wl_status;
boolean mdns_started = false;
boolean apMode = true;
boolean captiveDNS = false;
boolean displayInit = false;

long radioRxCount = 0;
long lastSecondAt = 0;
String line1 = "";
String line2 = "";
String line3 = "";
String line4 = "";

String radioBuffer = "";

//furi_hal_subghz_preset_gfsk_9_99kb_async_regs
void flipperChatPreset()
{
  ELECHOUSE_cc1101.setMHZ(frequency);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_FIFOTHR, 0x47);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_PKTCTRL0, 0x05);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_FSCTRL1, 0x06);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_SYNC1, 0x46);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_SYNC0, 0x4C);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_ADDR, 0x00);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_PKTLEN, 0x00);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_MDMCFG4, 0xC8);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_MDMCFG3, 0x93);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_MDMCFG2, 0x12);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_DEVIATN, 0x34);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_MCSM0, 0x18);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_FOCCFG, 0x16);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_AGCCTRL2, 0x43);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_AGCCTRL1, 0x40);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_AGCCTRL0, 0x91);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_WORCTRL, 0xFB);
}

// The CC1101 has a buffer limit of like 53, this will break up outbound messages to small enough chunks for the flipper to receive
void streamToRadio(String &outData)
{
  int data_len = outData.length();
  //Serial.print("Data to Stream to Radio:");
  //Serial.print("Len=");
  //Serial.println(data_len);
  if (data_len < 53)
  {
    //Serial.println("No chunking!");
    char data_array[data_len+1];
    outData.toCharArray(data_array, data_len+1);
    ELECHOUSE_cc1101.SendData(data_array, data_len+1);
  } else {
    int chunksNeeded = (data_len / 50);
    int lastChunkSize = (data_len % 50);
    for(int i = 0; i <= chunksNeeded; i++)
    {
      //Serial.print("Chunk=");
      //Serial.print(i);
      int chunkLen = 50;
      if (i == chunksNeeded)
      {
        chunkLen = lastChunkSize;
      }
      //Serial.print(" ChunkLen=");
      //Serial.print(chunkLen);
      int startAt = i * 50;
      int endAt = startAt + chunkLen;
      //Serial.print(" startAt=");
      //Serial.print(startAt);
      //Serial.print(" endAt=");
      //Serial.println(endAt);
      char data_array[chunkLen+1];
      outData.substring(startAt, endAt).toCharArray(data_array, chunkLen+1);
      ELECHOUSE_cc1101.SendData(data_array, chunkLen+1);
      delay(15);
    }
  }
}

void processJSONPayload(int num, uint8_t * payload)
{
  IPAddress ip = webSocketServer.remoteIP(num);
  String source = ip.toString();
  char* data = (char *) payload;
  DynamicJsonDocument root(2048);
  DeserializationError error = deserializeJson(root, data);
  if (error)
  {
    Serial.println(error.c_str());
  } else {
    if (root.containsKey("username") && root.containsKey("event"))
    {
      String username = root["username"].as<String>();
      String event = root["event"].as<String>();
      if (root.containsKey("text"))
      {
        String text = root["text"].as<String>();
        String xmitData = "\x1B[0;33m" + username + "\x1B[0m: " + text + "\r\n";
        streamToRadio(xmitData);
      } else if (root.containsKey("event")) {
        if (event.equals("join"))
        {
          registerUser(num, username, source, WiFi.RSSI());
        } else if (event.equals("part")) {
          deleteUser(username);
        } if (event.equals("frequency")) {
          frequency = root["mhz"].as<float>();
          ELECHOUSE_cc1101.setMHZ(frequency);
        } else if (event.equals("raw")) {
          String data = root["data"].as<String>();
          streamToRadio(data);
        }
      }
      root["source"] = source;
      root["num"] = num;
      String out;
      serializeJson(root, out);
      for(int i = 0; i < webSocketServer.connectedClients(); i++)
      {
        if (i != num)
          webSocketServer.sendTXT(i, out);
      }
    }
  }
}

int registerUser(int wsNum, String &username, String &source, int rssi)
{
  for(int i = 0; i < MAX_MEMBERS; i++)
  {
    if (members[i] == "" || members[i].equals(username))
    {
      members[i] = username;
      member_nums[i] = wsNum;
      member_sources[i] = source;
      member_rssi[i] = rssi;
      String out;
      StaticJsonDocument<2048> jsonBuffer;
      jsonBuffer["username"] = members[i];
      jsonBuffer["event"] = "join";
      jsonBuffer["rssi"] = rssi;
      if (source.equals("radio"))
      {
        jsonBuffer["source"] = "radio";
      } else {
        jsonBuffer["source"] = source;
      }
      serializeJson(jsonBuffer, out);
      webSocketServer.broadcastTXT(out);
      if (!source.equals("radio"))
      {
        String xmitData = "\x1B[0;33m" + username + " joined chat.\x1B[0m\r\n";
        streamToRadio(xmitData);
      }
      return i;
    }
  }
}

void deleteUsersForNum(int wsNum)
{
  if (wsNum >= 0)
  {
    for(int i = 0; i < MAX_MEMBERS; i++)
    {
      if (member_nums[i] == wsNum)
      {
        deleteUser(i);
        return;
      }
    }
  }
}

int findUser(String &username)
{
  for(int i = 0; i < MAX_MEMBERS; i++)
  {
    if (members[i].equals(username))
    {
      return i;
    }
  }
  return -1;
}

int deleteUser(String &username)
{
  int idx = findUser(username);
  if (idx >= 0)
  {
    deleteUser(idx);
  }
  return idx;
}

void deleteUser(int idx)
{
  String out;
  StaticJsonDocument<2048> jsonBuffer;
  jsonBuffer["username"] = members[idx];
  jsonBuffer["event"] = "part";
  jsonBuffer["rssi"] = member_rssi[idx];
  jsonBuffer["source"] = member_sources[idx];
  serializeJson(jsonBuffer, out);
  webSocketServer.broadcastTXT(out);
  if (!member_sources[idx].equals("radio"))
  {
    String xmitData = "\x1B[0;33m" + members[idx] + " left chat.\x1B[0m\r\n";
    streamToRadio(xmitData);
  }
  members[idx] = "";
  member_nums[idx] = 0;
  member_sources[idx] = "";
  member_rssi[idx] = 0;
}

void setup()
{
  for(int i = 0; i < MAX_MEMBERS; i++)
  {
    members[i] = "";
    member_nums[i] = 0;
    member_sources[i] = "";
    member_rssi[i] = 0;
  }
  Serial.begin(115200);
  Serial.println("INIT");
  FFat.begin();
  loadSettings();
  ELECHOUSE_cc1101.Init();
  flipperChatPreset();
  webSocketServer.begin();
  webSocketServer.onEvent(webSocketServerEvent);
  httpServer.on("/", handleRoot);
  httpServer.onNotFound(handleNotFound);
  httpServer.begin();

  if (display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    Serial.println("Display INIT OK");
    display.display();
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.display();
    displayInit = true;
  } else {
    Serial.println("Display INIT FAIL");
    displayInit = false;
  }
  
  Serial.println("INIT_COMPLETE");
}

byte buffer[255] = {0};

void checkRadio()
{
  bool rxed = false;
  if(ELECHOUSE_cc1101.CheckRxFifo(200))
  {
    int len = ELECHOUSE_cc1101.ReceiveData(buffer);    
    buffer[len] = '\0';
    rxed = true;
  }
  if (rxed)
  {
    int rssi = ELECHOUSE_cc1101.getRssi();
    if (buffer[0] == 27 && buffer[1] == 91 && buffer[2] == 48)
    {
      char nick[20];
      char msg[150];
      int mStart = 0;
      for(int i = 7; i <= 254; i++)
      {
        if (buffer[i] != 27)
        {
          nick[i-7] = buffer[i];
        } else { 
          nick[i-7] = '\0';
          mStart = i+6;
          break; 
        }
      }
      for(int i = mStart; i <= 254; i++)
      {
        if (buffer[i] != 0)
        {
          msg[i-mStart] = buffer[i];
        } else { 
          msg[i-mStart] = '\0';
          break; 
        }
      }
      radioRxCount++;
      String username = String(nick);
      String text = String(msg);
      String source = "radio";
      if (username.indexOf(" joined chat.") >= 0)
      {
        String realUsername = String(username.substring(0, username.length() - 13));
        registerUser(0, realUsername, source, rssi);
      } else if (username.indexOf(" left chat.") >= 0 || text.indexOf(" left chat.") >= 0) {
        String realUsername = String(username.substring(0, username.length() - 11));
        deleteUser(realUsername);
      } else {
        if (findUser(username) == -1)
        {
          registerUser(0, username, source, rssi);
        }
        String out;
        StaticJsonDocument<4096> jsonBuffer;
        jsonBuffer["username"] = username;
        jsonBuffer["text"] = text;
        jsonBuffer["event"] = "chat";
        jsonBuffer["rssi"] = rssi;
        jsonBuffer["source"] = source;
        serializeJson(jsonBuffer, out);
        webSocketServer.broadcastTXT(out);
      }
    }
  }
}

void redraw()
{
  if (displayInit)
  {
    display.clearDisplay();
    display.setCursor(0,PIX_ROW_1);
    display.setTextSize(2);
    display.print(line1);
    display.setTextSize(1);
    display.setCursor(0,PIX_ROW_2);
    display.print(line2);
    display.setCursor(0,PIX_ROW_3);
    display.print(line3);
    display.setCursor(0,PIX_ROW_4);
    display.print(line4);
    display.display();
  }
}

void loopNetwork()
{
  webSocketServer.loop();
  httpServer.handleClient();
}

void everySecond()
{
  int flipperCount = 0;
  int networkCount = 0;
  for(int i = 0; i < MAX_MEMBERS; i++)
  {
    if (members[i] != "")
    {
      if (member_sources[i].equals("radio"))
      {
        flipperCount++;
      } else {
        networkCount++;
      }
    }
  }
  line2 = "Flippers: " + String(flipperCount);
  line3 = "net: " + String(networkCount) + " rrx: " + String(radioRxCount);
  line1 = String(frequency) + " Mhz";
  redraw();
}

void loop()
{
  long ts = millis();
  if (!apMode)
  {
    // client mode with mDNS
    uint8_t last_wl_status = wl_status;
    wl_status = wifiMulti.run(); 
    if (wl_status == WL_CONNECTED)
    {
      if (last_wl_status != wl_status)
      {
        Serial.print("Network IP: ");
        Serial.println(WiFi.localIP());
        line4 = WiFi.localIP().toString();
        tryMDNS();
      }
      loopNetwork();
    }
  } else {
    // apMode with captive DNS
    loopNetwork();
    if (captiveDNS)
    {
      dnsServer.processNextRequest();
    }
  }
  checkRadio();
  if (ts - lastSecondAt >= 1000)
  {
    everySecond();
    lastSecondAt = ts;
  }
}


void webSocketServerEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght)
{
  switch (type)
  {
    case WStype_DISCONNECTED: {
        deleteUsersForNum((int)num);
      }
      break;
    case WStype_CONNECTED: {
        IPAddress ip = webSocketServer.remoteIP(num);
        Serial.print("Websocket Connection: ");
        Serial.println(ip);
        StaticJsonDocument<2048> jsonBuffer;
        jsonBuffer["clientIp"] = ip.toString();
        jsonBuffer["mhz"] = frequency;
        JsonArray usersJson = jsonBuffer.createNestedArray("users");
        for(int i = 0; i < MAX_MEMBERS; i++)
        {
          if (members[i] != "")
          {
            JsonObject d = usersJson.createNestedObject();
            d["u"] = members[i];
            d["s"] = member_sources[i];
            d["r"] = member_rssi[i];
          }
        }
        String out;
        serializeJson(jsonBuffer, out);
        webSocketServer.sendTXT(num, out);
      }
      break;
    case WStype_TEXT:
      if (payload[0] == '{') {
        processJSONPayload(num, payload);
      }
      break;
    }
}

void tryMDNS()
{
  if (!mdns_started)
  {
    if (MDNS.begin("FlipperZeroChat"))
    {
      MDNS.addService("ws", "tcp", 81);
      MDNS.addService("http", "tcp", 80);
    }
  }
}

// ====================================================================================
// File Serving
// ====================================================================================


// Load Settings from flash
void loadSettings()
{
  StaticJsonDocument<4096> settings;
  File file = FFat.open("/settings.json", "r");
  if (!file)
  {
    Serial.println("/settings.json doesn't exist");
    file.close();
  } else {
    DeserializationError error = deserializeJson(settings, file);
    Serial.println("/settings.json read ok!");
    file.close();
    if (settings.containsKey("startFrequency"))
    {
      frequency = settings["startFrequency"].as<float>();
    }
    if (settings.containsKey("apMode") && settings.containsKey("apSSID"))
    {
      apMode = settings["apMode"].as<bool>();
    }
    if (settings.containsKey("captiveDNS"))
    {
      captiveDNS = settings["captiveDNS"].as<bool>();
    }
    if (apMode)
    {
      Serial.println("Access Point Mode!");
      WiFi.mode(WIFI_AP);
      WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
      line4 = apIP.toString();
      String ssid = settings["apSSID"].as<String>();
      if (settings.containsKey("apPassword"))
      {
        String password = settings["apPassword"].as<String>();
        if (password.equals("null"))
        {
          Serial.println("AP has no password");
          WiFi.softAP(ssid.c_str(), NULL);
        } else {
          Serial.print("AP password = ");
          Serial.println(password);
          WiFi.softAP(ssid.c_str(), password.c_str());
        }
      } else {
        Serial.println("AP has no password");
        String ssid = settings["apSSID"].as<String>();
        WiFi.softAP(ssid.c_str(), NULL);
      }
      wl_status = WL_CONNECTED;
      tryMDNS();
      if (captiveDNS)
      {
        Serial.println("Captive DNS Enabled");
        dnsServer.start(53, "*", apIP);
      } else {
        Serial.println("Captive DNS Disabled");
      }
    } else {
      Serial.println("WiFi Client Mode!");
      WiFi.mode(WIFI_STA);
    }
    if (settings.containsKey("wifi") && !apMode)
    {
      JsonArray wifi = settings["wifi"].as<JsonArray>();
      for (JsonObject network : wifi)
      {
        if (network.containsKey("password"))
        {
          String ssid = network["ssid"].as<String>();
          String password = network["password"].as<String>();
          wifiMulti.addAP(ssid.c_str(), password.c_str());
          Serial.print("Added Network ");
          Serial.print(ssid);
          Serial.println(" to WifiMulti");
        } else {
          String ssid = network["ssid"].as<String>();
          wifiMulti.addAP(ssid.c_str(), NULL);
          Serial.print("Added Network ");
          Serial.print(ssid);
          Serial.println(" to WifiMulti");
        }
      }
      wl_status = wifiMulti.run();
      if (wl_status == WL_CONNECTED)
      {
        Serial.print("Network IP: ");
        Serial.println(WiFi.localIP());
        line4 = WiFi.localIP().toString();
        tryMDNS();
      }
    }
  }
}

String getContentType(String filename)
{
  if (filename.endsWith(".htm")) {
    return "text/html";
  } else if (filename.endsWith(".html")) {
    return "text/html";
  } else if (filename.endsWith(".css")) {
    return "text/css";
  } else if (filename.endsWith(".js")) {
    return "application/javascript";
  } else if (filename.endsWith(".json")) {
    return "text/json";
  } else if (filename.endsWith(".png")) {
    return "image/png";
  } else if (filename.endsWith(".gif")) {
    return "image/gif";
  } else if (filename.endsWith(".jpg")) {
    return "image/jpeg";
  } else if (filename.endsWith(".ico")) {
    return "image/x-icon";
  } else if (filename.endsWith(".svg")) {
    return "image/svg+xml";
  } else if (filename.endsWith(".xml")) {
    return "text/xml";
  } else if (filename.endsWith(".pdf")) {
    return "application/x-pdf";
  } else if (filename.endsWith(".zip")) {
    return "application/x-zip";
  } else if (filename.endsWith(".gz")) {
    return "application/x-gzip";
  }
  return "text/plain";
}

void handleFileRead(String path)
{
  bool found = false;
  if (path.endsWith("/"))
  {
    path += "index.html";
  }
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if (FFat.exists(pathWithGz) || FFat.exists(path))
  {
    if (FFat.exists(pathWithGz))
    {
      path += ".gz";
    }
    File file = FFat.open(path, "r");
    httpServer.streamFile(file, contentType);
    file.close();
    found = true;
  }
  if (!found)
  {
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += httpServer.uri();
    message += "\nMethod: ";
    message += ( httpServer.method() == HTTP_GET ) ? "GET" : "POST";
    message += "\nArguments: ";
    message += httpServer.args();
    message += "\n";
    message += "Path: ";
    message += path;
    message += "\n";
    
    for ( uint8_t i = 0; i < httpServer.args(); i++ )
    {     
      message += " " + httpServer.argName ( i ) + ": " + httpServer.arg ( i ) + "\n";
    }
    httpServer.send ( 404, "text/plain", message );
  }
}

void handleRoot()
{
  handleFileRead(httpServer.uri());
}

void handleNotFound()
{
  handleFileRead(httpServer.uri());
}
