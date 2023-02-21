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

#define SS_PIN    5
float frequency = 433.92;

WiFiMulti wifiMulti;
WebSocketsServer webSocketServer(81);
WebServer httpServer(80);
IPAddress apIP(10, 10, 10, 1);

uint8_t wl_status;
boolean mdns_started = false;
boolean apMode = true;

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

void processJSONPayload(int num, uint8_t * payload)
{
  IPAddress ip = webSocketServer.remoteIP(num);
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
        int data_len = xmitData.length() + 1;
        char data_array[data_len];
        xmitData.toCharArray(data_array, data_len);
        ELECHOUSE_cc1101.SendData(data_array, data_len);
      } else if (root.containsKey("event")) {
        if (event.equals("join"))
        {
          String xmitData = "\x1B[0;33m" + username + " joined chat.\x1B[0m\r\n";
          int data_len = xmitData.length() + 1;
          char data_array[data_len];
          xmitData.toCharArray(data_array, data_len);
          ELECHOUSE_cc1101.SendData(data_array, data_len);
        } else if (event.equals("part")) {
          String xmitData = "\x1B[0;33m" + username + " left chat.\x1B[0m\r\n";
          int data_len = xmitData.length() + 1;
          char data_array[data_len];
          xmitData.toCharArray(data_array, data_len);
          ELECHOUSE_cc1101.SendData(data_array, data_len);
        } if (event.equals("frequency")) {
          frequency = root["mhz"].as<float>();
          ELECHOUSE_cc1101.setMHZ(frequency);
        }
      }
      root["source"] = ip.toString();
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

void setup()
{
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

  wl_status = wifiMulti.run();
  if (wl_status == WL_CONNECTED)
  {
    Serial.print("Network IP: ");
    Serial.println(WiFi.localIP());
    tryMDNS();
  }
}

byte buffer[255] = {0};

void checkRadio()
{
  if (ELECHOUSE_cc1101.CheckRxFifo(200))
  {
    int len = ELECHOUSE_cc1101.ReceiveData(buffer);
    int rssi = ELECHOUSE_cc1101.getRssi();
    buffer[len] = '\0';
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
      String username = String(nick);
      String text = String(msg);
      if (username.indexOf(" joined chat.") >= 0)
      {
        String out;
        StaticJsonDocument<2048> jsonBuffer;
        jsonBuffer["username"] = username.substring(0, username.length() - 13);
        jsonBuffer["event"] = "join";
        jsonBuffer["rssi"] = rssi;
        jsonBuffer["source"] = "radio";
        serializeJson(jsonBuffer, out);
        webSocketServer.broadcastTXT(out);
      } else if (username.indexOf(" left chat.") >= 0 || text.indexOf(" left chat.") >= 0) {
        String out; 
        StaticJsonDocument<2048> jsonBuffer;
        jsonBuffer["username"] = username.substring(0, username.length() - 11);
        jsonBuffer["event"] = "part";
        jsonBuffer["rssi"] = rssi;
        jsonBuffer["source"] = "radio";
        serializeJson(jsonBuffer, out);
        webSocketServer.broadcastTXT(out);
      } else {
        String out;
        StaticJsonDocument<2048> jsonBuffer;
        jsonBuffer["username"] = username;
        jsonBuffer["text"] = text;
        jsonBuffer["event"] = "chat";
        jsonBuffer["rssi"] = rssi;
        jsonBuffer["source"] = "radio";
        serializeJson(jsonBuffer, out);
        webSocketServer.broadcastTXT(out);
      }
    } else {
      String out;
      StaticJsonDocument<2048> jsonBuffer;
      JsonArray raw_array = jsonBuffer.createNestedArray("raw");
      for(int i = 0; i < len; i++)
      {
        raw_array.add((int)buffer[i]);
      }
      jsonBuffer["len"] = len;
      jsonBuffer["event"] = "raw";
      jsonBuffer["rssi"] = rssi;
      jsonBuffer["source"] = "radio";
      serializeJson(jsonBuffer, out);
      webSocketServer.broadcastTXT(out);      
    }
  }
}

void loopNetwork()
{
  webSocketServer.loop();
  httpServer.handleClient();
}

void loop()
{
  if (!apMode)
  {
    uint8_t last_wl_status = wl_status;
    wl_status = wifiMulti.run(); 
    if (wl_status == WL_CONNECTED)
    {
      if (last_wl_status != wl_status)
      {
        Serial.print("Network IP: ");
        Serial.println(WiFi.localIP());
        tryMDNS();
      }
      loopNetwork();
    }
  } else {
    loopNetwork();
  }
  checkRadio();
}


void webSocketServerEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght)
{
  switch (type)
  {
    case WStype_DISCONNECTED: {
        StaticJsonDocument<128> jsonBuffer;
        jsonBuffer["event"] = "disconnect";
        jsonBuffer["num"] = num;
        String out;
        serializeJson(jsonBuffer, out);
        webSocketServer.broadcastTXT(out);
      }
      break;
    case WStype_CONNECTED: {
        IPAddress ip = webSocketServer.remoteIP(num);
        Serial.print("Websocket Connection: ");
        Serial.println(ip);
        StaticJsonDocument<128> jsonBuffer;
        jsonBuffer["clientIp"] = ip.toString();
        jsonBuffer["mhz"] = frequency;
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
    if (settings.containsKey("apMode"))
    {
      apMode = settings["apMode"].as<bool>();
    }
    if (apMode)
    {
      WiFi.mode(WIFI_AP);
      WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
      if (settings.containsKey("apPassword"))
      {
        String ssid = settings["apSSID"].as<String>();
        String password = settings["apPassword"].as<String>();
        WiFi.softAP(ssid.c_str(), password.c_str());
      } else {
        String ssid = settings["apSSID"].as<String>();
        WiFi.softAP(ssid.c_str(), NULL);
      }
      wl_status = WL_CONNECTED;
      tryMDNS();
    } else {
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
