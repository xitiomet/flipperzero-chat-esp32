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

int maxFlippers = 0;
long radioRxCount = 0;
long lastSecondAt = 0;
String line1 = "";
String line2 = "";
String line3 = "";
String line4 = "";

byte radioBuffer[2048] = {0};
int radioBufferPos = 0;
int radioRssi = 0;

//furi_hal_subghz_preset_gfsk_9_99kb_async_regs
void flipperChatPreset()
{
  ELECHOUSE_cc1101.setMHZ(frequency);
  //ELECHOUSE_cc1101.SpiWriteReg(CC1101_IOCFG0, 0x0D); //GDO0 Output Pin Configuration
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_FIFOTHR, 0x47); //RX FIFO and TX FIFO Thresholds
  //1 : CRC calculation in TX and CRC check in RX enabled,
  //1 : Variable packet length mode. Packet length configured by the first byte after sync word
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_PKTCTRL0, 0x05);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_FSCTRL1, 0x06); //Frequency Synthesizer Control
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_SYNC1, 0x46);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_SYNC0, 0x4C);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_ADDR, 0x00);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_PKTLEN, 0x00);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_MDMCFG4, 0xC8); //Modem Configuration 9.99
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_MDMCFG3, 0x93); //Modem Configuration
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_MDMCFG2, 0x12); // 2: 16/16 sync word bits detected
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_DEVIATN, 0x34); //Deviation = 19.042969
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_MCSM0, 0x18);   //Main Radio Control State Machine Configuration
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_FOCCFG, 0x16);  //Frequency Offset Compensation Configuration
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_AGCCTRL2, 0x43);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_AGCCTRL1, 0x40);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_AGCCTRL0, 0x91);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_WORCTRL, 0xFB);
}

void advertiseGateway(String &ssid, String &ip)
{
  String message = "\x1B[0;94mSubGhz Chat Bridge now online\x1B[0m\r\n SSID " + ssid + "\r\n IP " + ip + "\r\n";
  streamToRadio(message);
}

// The CC1101 has a buffer limit of like 53, this will break up outbound messages to small enough chunks for the flipper to receive
void streamToRadio(String &outData)
{
  int data_len = outData.length();
  if (data_len < 53)
  {
    char data_array[data_len+1];
    outData.toCharArray(data_array, data_len+1);
    ELECHOUSE_cc1101.SendData(data_array, data_len+1);
  } else {
    int chunksNeeded = (data_len / 50);
    int lastChunkSize = (data_len % 50);
    for(int i = 0; i <= chunksNeeded; i++)
    {
      int chunkLen = 50;
      if (i == chunksNeeded)
      {
        chunkLen = lastChunkSize;
      }
      int startAt = i * 50;
      int endAt = startAt + chunkLen;
      char data_array[chunkLen+1];
      outData.substring(startAt, endAt).toCharArray(data_array, chunkLen+1);
      ELECHOUSE_cc1101.SendData(data_array, chunkLen+1);
      delay(15);
    }
  }
}

void processJSONPayload(int num, uint8_t * payload)
{
  boolean doRestart = false;
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
        Serial.print("(");
        Serial.print(source);
        Serial.print(") ");
        Serial.print(username);
        Serial.print(": ");
        Serial.println(text);
        String xmitData = "\x1B[0;91m" + username + "\x1B[0m: " + text + "\r\n";
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
        } else if (event.equals("restart")) {
          doRestart = true;
        } else if (event.equals("radioReset")) {
          flipperChatPreset();
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
    if (doRestart)
    {
      ESP.restart();
    }
  }
}

bool isCleanUsername(String &username)
{
  for(int i = 0; i < username.length(); i++)
  {
    char cc = username.charAt(i);
    if (cc >= 32 && cc <= 126)
    {
      // character is ok
    } else {
      return false;
    }
  }
  return true;
}

void registerUser(int wsNum, String &username, String &source, int rssi)
{
  if (isCleanUsername(username))
  {
    for(int i = 0; i < MAX_MEMBERS; i++)
    {
      if (members[i] == "" || members[i].equals(username))
      {
        Serial.print(username);
        Serial.println(" joined the chat");
        members[i] = username;
        member_nums[i] = wsNum;
        member_sources[i] = source;
        member_rssi[i] = rssi;
        String out;
        StaticJsonDocument<1024> jsonBuffer;
        jsonBuffer["username"] = members[i];
        jsonBuffer["event"] = "join";
        jsonBuffer["rssi"] = rssi;
        jsonBuffer["source"] = source;
        serializeJson(jsonBuffer, out);
        webSocketServer.broadcastTXT(out);
        if (!source.equals("radio") && !source.equals("flipper"))
        {
          String xmitData = "\x1B[0;92m" + username + " joined chat.\x1B[0m\r\n";
          streamToRadio(xmitData);
        }
        return;
      }
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
  Serial.print(members[idx]);
  Serial.println(" left the chat");
  String out;
  StaticJsonDocument<1024> jsonBuffer;
  jsonBuffer["username"] = members[idx];
  jsonBuffer["event"] = "part";
  jsonBuffer["rssi"] = member_rssi[idx];
  jsonBuffer["source"] = member_sources[idx];
  serializeJson(jsonBuffer, out);
  webSocketServer.broadcastTXT(out);
  if (!member_sources[idx].equals("radio") && !member_sources[idx].equals("flipper"))
  {
    String xmitData = "\x1B[0;90m" + members[idx] + " left chat.\x1B[0m\r\n";
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
  clearRadioBuffer();
  Serial.begin(115200);
  Serial.println("INIT");
  if (ELECHOUSE_cc1101.getCC1101())
  {
    Serial.println("CC1101 Connection OK");
  } else {
    Serial.println("CC1101 Connection Error");
  }
  FFat.begin();
  loadSettings();
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

void clearRadioBuffer()
{
  radioBufferPos = 0;
  for(int n = 0; n < 2048; n++)
  {
    radioBuffer[n] = '\0';
  }
  //Serial.println("radio buffer cleared");
}

void appendToRadioBuffer(byte c)
{
  if (radioBufferPos < 2047)
  {
    radioBuffer[radioBufferPos] = c;
    radioBufferPos++;
    radioBuffer[radioBufferPos] = '\0';
  } else {
    clearRadioBuffer();
  }
}

byte buffer[255] = {0};

void readChatFromRadioBuffer()
{
  char nick[20];
  char msg[1024];
  int mStart = 0;
  for(int i = 7; i < 2048; i++)
  {
    if (radioBuffer[i] != 27)
    {
      nick[i-7] = radioBuffer[i];
    } else { 
      nick[i-7] = '\0';
      mStart = i+6;
      break; 
    }
  }
  for(int i = mStart; i < 2048; i++)
  {
    if (radioBuffer[i] != 0)
    {
      msg[i-mStart] = radioBuffer[i];
    } else { 
      msg[i-mStart] = '\0';
      break; 
    }
  }
  radioRxCount++;
  String username = String(nick);
  String text = String(msg);
  text.trim();
  String source = "radio";
  if (radioBuffer[4] == 51 && radioBuffer[5] == 51)
  {
    source = "flipper";
  }
  if (findUser(username) == -1)
  {
    registerUser(0, username, source, radioRssi);
  }
  Serial.print("(");
  Serial.print(source);
  Serial.print(") ");
  Serial.print(username);
  Serial.print(": ");
  Serial.println(text);
  String out;
  StaticJsonDocument<3072> jsonBuffer;
  jsonBuffer["username"] = username;
  jsonBuffer["text"] = text;
  jsonBuffer["event"] = "chat";
  jsonBuffer["rssi"] = radioRssi;
  jsonBuffer["source"] = source;
  serializeJson(jsonBuffer, out);
  webSocketServer.broadcastTXT(out);
}

void checkRadio()
{
  bool rxed = false;
  if(ELECHOUSE_cc1101.CheckRxFifo(50))
  {
    int len = ELECHOUSE_cc1101.ReceiveData(buffer);
    radioRssi = ELECHOUSE_cc1101.getRssi();
    buffer[len] = '\0';
    for(int i = 0; i < len; i++)
    {
      byte c = buffer[i];
      if (radioBufferPos == 0 && c != 27)
      {
        // keep scanning
        //Serial.print((char) c);
        //Serial.println(" reset buffer");
      } else if (radioBufferPos == 1 && c != 91) {
        // reset not message start
        radioBufferPos = 0;
        //Serial.println("reset buffer2");
      } else if ((c >= 32 && c <= 126) || c == 27 || c == 10 || c == 13) {
        appendToRadioBuffer(c);
      }
    }
    rxed = true;
    ELECHOUSE_cc1101.SetRx();
  }
  if (rxed && radioBufferPos > 0)
  {
    if ((radioBuffer[0] == 27 && radioBuffer[1] == 91 && radioBuffer[2] == 48) && radioBuffer[radioBufferPos-1] == 10)
    {
      //Serial.print("color: ");
      //Serial.print((int)radioBuffer[4]);
      //Serial.print((int)radioBuffer[5]);
      //Serial.print(" ");
      //Serial.print((char)radioBuffer[4]);
      //Serial.println((char)radioBuffer[5]);
      if ((radioBuffer[4] == 51 && radioBuffer[5] == 51) || (radioBuffer[4] == 57 && radioBuffer[5] == 49))
      {
        readChatFromRadioBuffer();
      } else {
        String source = "radio";
        if (radioBuffer[4] == 51 && (radioBuffer[5] == 52 || radioBuffer[5] == 49))
        {
          source = "flipper";
        }
        String data = String((char *)radioBuffer);
        if (data.indexOf(" joined chat.") >= 0)
        {
          String realUsername = String(data.substring(7, data.length() - 19));
          registerUser(0, realUsername, source, radioRssi);
        } else if (data.indexOf(" left chat.") >= 0) {
          String realUsername = String(data.substring(7, data.length() - 17));
          deleteUser(realUsername);
        }
      }
      clearRadioBuffer();
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
  if (!apMode)
  {
    uint8_t last_wl_status = wl_status;
    wl_status = wifiMulti.run();
    if (wl_status == WL_CONNECTED)
    {
      if (last_wl_status != wl_status)
      {
        onNetworkConnect();
      }
    }
  }
  int flipperCount = 0;
  int networkCount = 0;
  for(int i = 0; i < MAX_MEMBERS; i++)
  {
    if (members[i] != "")
    {
      if (member_sources[i].equals("flipper"))
      {
        flipperCount++;
      } else {
        networkCount++;
      }
    }
  }
  if (flipperCount > maxFlippers)
    maxFlippers = flipperCount;
  line2 = "Flippers: " + String(flipperCount) + " / " + String(maxFlippers);
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
    if (wl_status == WL_CONNECTED)
    {
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

void onNetworkConnect()
{
  String ssid = WiFi.SSID();
  String ip = WiFi.localIP().toString();
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("Network IP: ");
  Serial.println(ip);
  line4 = ip;
  tryMDNS();
  advertiseGateway(ssid, ip);
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
    ELECHOUSE_cc1101.Init();
    flipperChatPreset();
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
      advertiseGateway(ssid, line4);
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
        onNetworkConnect();
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
