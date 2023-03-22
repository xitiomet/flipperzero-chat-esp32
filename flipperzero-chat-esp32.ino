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
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <FFat.h>
#include "./DNSServer.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <NTPClient.h>
#include "TimeLib.h"
#include "BluetoothSerial.h"

#define OLED_RESET -1
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define SCREEN_ADDRESS 0x3c ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define PIX_ROW_1 0
#define PIX_ROW_1p5 14
#define PIX_ROW_2 24
#define PIX_ROW_3 40
#define PIX_ROW_4 56

#define MAX_IRC_CLIENTS 7
#define MAX_MEMBERS 40
#define HISTORY_SIZE 15
#define SS_PIN    5
float frequency = 433.92;

WiFiMulti wifiMulti;
WiFiServer ircServer(6667);

WiFiClient ircClients[MAX_IRC_CLIENTS];
char ircBuffers[MAX_IRC_CLIENTS][1024];
int ircBufferPos[MAX_IRC_CLIENTS];
int ircUserId[MAX_IRC_CLIENTS];
String ircUsernames[MAX_IRC_CLIENTS];
String ircNicknames[MAX_IRC_CLIENTS];
bool ircGreeted[MAX_IRC_CLIENTS];

WebSocketsServer webSocketServer(81);
WebServer httpServer(80);
IPAddress apIP(10, 10, 10, 1);
DNSServer dnsServer;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "time.nist.gov", 0, 60000);

String history[HISTORY_SIZE];
int historyPosition = 0;

//member storage for all users, irc, websocket, radio, flipper
String members[MAX_MEMBERS];
String member_sources[MAX_MEMBERS];
int member_nums[MAX_MEMBERS];
int member_rssi[MAX_MEMBERS];
long member_last_active[MAX_MEMBERS];

uint8_t wl_status;
boolean mdns_started = false;
boolean apMode = true;
boolean captiveDNS = false;
boolean displayInit = false;
boolean replayChatHistory = true;

int tick = 0;
int maxFlippers = 0;
long radioRxCount = 0;
long lastSecondAt = 0;
int flipperCount = 0;
int networkCount = 0;
String line1 = "";
String line2 = "";
String line3 = "";
String local_ip = "";
String local_ssid = "";
String hostname = "FlipperZeroChat";

String flashMessageTitle = "";
String flashMessageBody = "";
long flashMessageAt = 0;

byte radioBuffer[2048] = {0};
int radioBufferPos = 0;
int radioRssi = 0;

String Serial2Nickname = "";
String Serial2Buffer = "";

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

void advertiseGateway()
{
  int uc = userCount();
  String message = "\x1B[0;93mSubGhz Chat Bridge (" + hostname + ") - Online\x1B[0m\r\n FREQUENCY " + String(frequency) + " Mhz\r\n SSID " + local_ssid + "\r\n IP " + local_ip + "\r\n USERS " + String(uc) + "\r\n\r\n";
  streamToRadio(message);
  Serial2.print(message);
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

void saveHistory(String &jsonData)
{
  if (replayChatHistory)
  {
    history[historyPosition] = jsonData;
    historyPosition++;
    if (historyPosition >= HISTORY_SIZE)
      historyPosition = 0;
  }
}

void replayHistory(int wsnum)
{
  if (replayChatHistory)
  {
    int t = historyPosition;
    for(int i = 0; i < HISTORY_SIZE; i++)
    {
      if (!history[t].equals(""))
      {
        webSocketServer.sendTXT(wsnum, history[t]);
      }
      t++;
      if (t >= HISTORY_SIZE)
      {
        t = 0;
      }
    }
  }
}

void replayHistorySerial2()
{
  int t = historyPosition;
  for(int i = 0; i < HISTORY_SIZE; i++)
  {
    if (!history[t].equals(""))
    {
      DynamicJsonDocument histObj(2048);
      DeserializationError error = deserializeJson(histObj, history[t]);
      if (error)
      {
        Serial.println(error.c_str());
      } else if (histObj.containsKey("username") && histObj.containsKey("event") && histObj.containsKey("text") && histObj.containsKey("source")){
        String username = histObj["username"].as<String>();
        String event = histObj["event"].as<String>();
        String text = histObj["text"].as<String>();
        String source = histObj["source"].as<String>();
        if (event.equals("chat"))
        {
          if (source.equals("flipper"))
          {
            String line = "\x1B[0;33m" + username + "\x1B[0m: " + text + "\r\n";
            Serial2.print(line);
          } else {
            String line = "\x1B[0;91m" + username + "\x1B[0m: " + text + "\r\n";
            Serial2.print(line);
          }
        } else if (event.equals("info")) {
          String line = "\x1B[0;94m" + text + "\x1B[0m\r\n";
          Serial2.print(line);
        }
      }
    }
    t++;
    if (t >= HISTORY_SIZE)
    {
      t = 0;
    }
  }
}

void changeFrequency(String &username, float frq)
{
  if ((frq >= 300.00 && frq <= 348.00) || (frq >= 387.00 && frq <= 464.00) || (frq >= 779.00 && frq <= 928.00))
  {
    frequency = frq;
    ELECHOUSE_cc1101.setMHZ(frequency);
    broadcastIrc(":TheReaper TOPIC #lobby :" + String(frequency) + " Mhz");
    broadcastIrc(":TheReaper NOTICE #lobby :The frequency has been changed to " + String(frequency) + " Mhz (by " + username + ")");
    String line = "\x1B[0;94mThe frequency has been changed to " + String(frequency) + " Mhz (by " + username + ")\x1B[0m\r\n";
    Serial2.print(line);
    String out;
    int uid = findUser(username);
    if (uid >= 0)
    {
      StaticJsonDocument<1024> jsonBuffer;
      jsonBuffer["username"] = members[uid];
      jsonBuffer["event"] = "frequency";
      jsonBuffer["rssi"] = member_rssi[uid];
      jsonBuffer["mhz"] = frequency;
      jsonBuffer["source"] = member_sources[uid];
      jsonBuffer["utc"] = now();
      serializeJson(jsonBuffer, out);
      webSocketServer.broadcastTXT(out);
    } else {
      StaticJsonDocument<1024> jsonBuffer;
      jsonBuffer["username"] = username;
      jsonBuffer["event"] = "frequency";
      jsonBuffer["mhz"] = frequency;
      jsonBuffer["utc"] = now();
      serializeJson(jsonBuffer, out);
      webSocketServer.broadcastTXT(out);
    }
  }
}

// Handle JSON payloads from the websocket on port 81
void processJSONPayload(int num, uint8_t * payload)
{
  IPAddress ip = webSocketServer.remoteIP(num);
  String source = "ws_" + ip.toString();
  char* data = (char *) payload;
  DynamicJsonDocument root(3072);
  DeserializationError error = deserializeJson(root, data);
  if (error)
  {
    Serial.println(error.c_str());
  } else {
    if (root.containsKey("username") && root.containsKey("event"))
    {
      String username = root["username"].as<String>();
      String event = root["event"].as<String>();
      int uid = findUser(username);
      if (uid >= 0 && uid < MAX_MEMBERS)
      {
        member_last_active[uid] = millis();
      }
      // Lets set the time if this user is providing it!
      if (timeStatus() != timeSet && root.containsKey("utc"))
      {
        time_t utc = root["utc"].as<time_t>();
        setTime(utc);
      }
      if (root.containsKey("text") && event.equals("chat"))
      {
        String text = root["text"].as<String>();
        //Serial.print("(");
        //Serial.print(source);
        //Serial.print(") ");
        //Serial.print(username);
        //Serial.print(": ");
        //Serial.println(text);
        String xmitData = "\x1B[0;91m" + username + "\x1B[0m: " + text + "\r\n";
        Serial2.print(xmitData);
        streamToRadio(xmitData);
        flashMessage(username, text);
        lobbyPrivmsg(username,text);
        root["source"] = source;
        root["num"] = num;
        if (!root.containsKey("utc") && timeStatus() == timeSet)
        {
          root["utc"] = now();
        }
        String out;
        serializeJson(root, out);
        for(int i = 0; i < webSocketServer.connectedClients(); i++)
        {
          if (i != num)
            webSocketServer.sendTXT(i, out);
        }
        saveHistory(out);
      } else {
        if (event.equals("join"))
        {
          uid = registerUser(num, username, source, WiFi.RSSI());
        } else if (event.equals("part")) {
          deleteUser(username);
        } if (event.equals("frequency")) {
          changeFrequency(username, root["mhz"].as<float>());
        } else if (event.equals("raw")) {
          String data = root["data"].as<String>();
          streamToRadio(data);
        } else if (event.equals("restart")) {
          ESP.restart();
        } else if (event.equals("radioReset")) {
          flipperChatPreset();
        } else if (event.equals("name") && root.containsKey("to")) {
          String to = root["to"].as<String>();
          userChangeName(username, to, true);
        } else if (event.equals("advertise")) {
          advertiseGateway();
        } else if (event.equals("history")) {
          replayHistory(num);
        } else if (event.equals("private") && root.containsKey("to") && root.containsKey("text")) {
          bool isAction = false;
          if (root.containsKey("action"))
          {
            isAction = root["action"].as<bool>();
          }
          String to = root["to"].as<String>();
          String text = root["text"].as<String>();
          realPrivateMessage(to, text, isAction, uid);
        }
      }
    }
  }
}

void realPrivateMessage(String &to, String &text, bool isAction, int from_uid)
{
  if (from_uid >= 0)
  {
    int num = member_nums[from_uid];
    int findIrcId = findIrcNick(to);
    if (findIrcId >= 0)
    {
      if (isAction)
      {
        String line = ":" + members[from_uid] + " PRIVMSG " + ircNicknames[findIrcId] + " :\001ACTION " + text + "\001\r\n";
        ircClients[findIrcId].print(line);
      } else {
        String line = ":" + members[from_uid] + " PRIVMSG " + ircNicknames[findIrcId] + " :" + text + "\r\n";
        ircClients[findIrcId].print(line);
      }
    } else {
      int findUserId = findUser(to);
      if (findUserId >= 0)
      {
        int wsNum = member_nums[findUserId];
        if (wsNum >= 0)
        {
          StaticJsonDocument<3072> jsonBuffer;
          jsonBuffer["username"] = members[from_uid];
          jsonBuffer["text"] = text;
          jsonBuffer["event"] = "private";
          jsonBuffer["rssi"] = WiFi.RSSI();
          jsonBuffer["utc"] = now();
          jsonBuffer["action"] = isAction;
          jsonBuffer["source"] = member_sources[from_uid];
          jsonBuffer["to"] = members[findUserId];
          String out;
          serializeJson(jsonBuffer, out);
          //Serial.println(out);
          webSocketServer.sendTXT(wsNum, out);
        } else {
          StaticJsonDocument<1024> jsonBuffer;
          jsonBuffer["for"] = members[from_uid];
          jsonBuffer["text"] = "User cannot receive private messages";
          jsonBuffer["event"] = "error";
          jsonBuffer["utc"] = now();
          jsonBuffer["to"] = to;
          String out;
          serializeJson(jsonBuffer, out);
          webSocketServer.sendTXT(num, out);
        }
      } else {
        StaticJsonDocument<1024> jsonBuffer;
        jsonBuffer["for"] = members[from_uid];
        jsonBuffer["text"] = "Couldn't find user";
        jsonBuffer["event"] = "error";
        jsonBuffer["utc"] = now();
        jsonBuffer["to"] = to;
        String out;
        serializeJson(jsonBuffer, out);
        webSocketServer.sendTXT(num, out);
      }
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

int userCount()
{
  int mc = 0;
  for(int i = 0; i < MAX_MEMBERS; i++)
  {
    if (members[i] != "")
    {
      mc++;
    }
  }
  return mc;
}

int registerUser(int wsNum, String &username, String &source, int rssi)
{
  if (isCleanUsername(username))
  {
    // lets make sure they aren't already registerd
    for(int i = 0; i < MAX_MEMBERS; i++)
    {
      if (members[i].equals(username))
      {
        member_nums[i] = wsNum;
        member_sources[i] = source;
        member_rssi[i] = rssi;
        member_last_active[i] = millis();
        return i;
      }
    }
    // ok lets find them a slot
    for(int i = 0; i < MAX_MEMBERS; i++)
    {
      if (members[i].equals(""))
      {
        //Serial.print(username);
        //Serial.println(" joined the chat");
        String bdy = "Joined the chat";
        flashMessage(username, bdy);
        members[i] = username;
        member_nums[i] = wsNum;
        member_sources[i] = source;
        member_rssi[i] = rssi;
        member_last_active[i] = millis();
        String out;
        StaticJsonDocument<1024> jsonBuffer;
        jsonBuffer["username"] = members[i];
        jsonBuffer["event"] = "join";
        jsonBuffer["rssi"] = rssi;
        jsonBuffer["source"] = source;
        serializeJson(jsonBuffer, out);
        for(int i = 0; i < webSocketServer.connectedClients(); i++)
        {
          if (i != wsNum)
            webSocketServer.sendTXT(i, out);
        }
        String xmitData = "\x1B[0;92m" + username + " joined chat.\x1B[0m\r\n";
        if (!source.equals("radio") && !source.equals("flipper"))
        {
          streamToRadio(xmitData);
        }
        Serial2.print(xmitData);
        String ircJoin = ":" + username + "!~" + username + "@" + source + " JOIN :#lobby";
        broadcastIrc(ircJoin);
        return i;
      }
    }
  }
  return -1;
}

// delete all users connected to a specific websocket
void deleteUsersForNum(int wsNum)
{
  if (wsNum >= 0)
  {
    for(int i = 0; i < MAX_MEMBERS; i++)
    {
      if (member_nums[i] == wsNum && !members[i].equals("") && member_sources[i].startsWith("ws"))
      {
        deleteUser(i);
      }
    }
  }
}

// find user id by username
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

// delete user by username
int deleteUser(String &username)
{
  int idx = findUser(username);
  if (idx >= 0)
  {
    deleteUser(idx);
  }
  return idx;
}

// delete user by user id
void deleteUser(int idx)
{
  if (!members[idx].equals(""))
  {
    //Serial.print(members[idx]);
    //Serial.println(" left the chat");
    String bdy = "Left the chat";
    flashMessage(members[idx], bdy);
    String out;
    StaticJsonDocument<1024> jsonBuffer;
    jsonBuffer["username"] = members[idx];
    jsonBuffer["event"] = "part";
    jsonBuffer["rssi"] = member_rssi[idx];
    jsonBuffer["source"] = member_sources[idx];
    serializeJson(jsonBuffer, out);
    webSocketServer.broadcastTXT(out);
    String xmitData = "\x1B[0;90m" + members[idx] + " left chat.\x1B[0m\r\n";
    if (!member_sources[idx].equals("radio") && !member_sources[idx].equals("flipper"))
    {
      streamToRadio(xmitData);
    }
    Serial2.print(xmitData);
    String ircPart = ":" + members[idx] + " PART #lobby";
    broadcastIrc(ircPart);
  }
  members[idx] = "";
  member_nums[idx] = -1;
  member_sources[idx] = "";
  member_rssi[idx] = 0;
  member_last_active[idx] = -1;
}

void setup()
{
  for(int i = 0; i < MAX_MEMBERS; i++)
  {
    members[i] = "";
    member_nums[i] = -1;
    member_sources[i] = "";
    member_rssi[i] = 0;
    member_last_active[i] = -1;
  }
  for(int i = 0; i < HISTORY_SIZE; i++)
  {
    history[i] = "";
  }
  for(int i = 0; i < MAX_IRC_CLIENTS; i++)
  {
    ircBufferPos[i] = 0;
    ircBuffers[i][0] = 0;
    ircUserId[i] = -1;
    ircUsernames[i] = "";
    ircNicknames[i] = "";
    ircGreeted[i] = false;
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
  ircServer.begin();
  ircServer.setNoDelay(true);
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
  int uid = findUser(username);
  if (uid == -1)
  {
    uid = registerUser(-1, username, source, radioRssi);
  } else {
    member_rssi[uid] = radioRssi;
  }
  if (uid >= 0 && uid < MAX_MEMBERS)
  {
    member_last_active[uid] = millis();
  }
  String out;
  StaticJsonDocument<3072> jsonBuffer;
  jsonBuffer["username"] = username;
  jsonBuffer["text"] = text;
  jsonBuffer["event"] = "chat";
  jsonBuffer["rssi"] = radioRssi;
  jsonBuffer["utc"] = now();
  jsonBuffer["source"] = source;
  serializeJson(jsonBuffer, out);
  webSocketServer.broadcastTXT(out);
  flashMessage(username,text);
  lobbyPrivmsg(username,text);
  saveHistory(out);
  if (source.equals("flipper"))
  {
    Serial2.print("\x1B[0;33m" + username + "\x1B[0m: " + text + "\r\n");
  } else {
    Serial2.print("\x1B[0;91m" + username + "\x1B[0m: " + text + "\r\n");
  }
}

void readInfoFromRadioBuffer()
{
  char info[1024];
  int mStart = 0;
  for(int i = 7; i < 2048; i++)
  {
    if (radioBuffer[i] != 27)
    {
      info[i-7] = radioBuffer[i];
    } else { 
      info[i-7] = '\0';
      mStart = i+6;
      break; 
    }
  }
  radioRxCount++;
  String text = String(info);
  text.trim();
  int nnal = text.indexOf(" is now known as ");
  if (nnal >= 0)
  {
    String formerNick = text.substring(0, nnal);
    int uid = findUser(formerNick);
    if (uid >= 0)
    {
      if (member_sources[uid].equals("radio") || member_sources[uid].equals("flipper"))
      {
        String newNick = text.substring(nnal+17,text.length());
        userChangeName(formerNick, newNick, false);
      }
    }
  } else {
    String username = "";
    if (get_token(text, username, 0, ' '))
    {
      int uid = findUser(username);
      if (uid >= 0)
      {
        member_rssi[uid] = radioRssi;
        String out;
        StaticJsonDocument<3072> jsonBuffer;
        jsonBuffer["username"] = username;
        jsonBuffer["text"] = text;
        jsonBuffer["event"] = "info";
        jsonBuffer["rssi"] = member_rssi[uid];
        jsonBuffer["utc"] = now();
        jsonBuffer["source"] = member_sources[uid];
        serializeJson(jsonBuffer, out);
        webSocketServer.broadcastTXT(out);
        saveHistory(out);
        Serial2.print("\x1B[0;94m" + text + "\x1B[0m\r\n");
        text.replace(username,"\001ACTION");
        text += "\001";
        lobbyPrivmsg(username, text);
      }
    }
  }
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
      } else if (radioBuffer[4] == 57 && radioBuffer[5] == 52) {
        readInfoFromRadioBuffer();
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
          registerUser(-1, realUsername, source, radioRssi);
        } else if (data.indexOf(" left chat.") >= 0) {
          String realUsername = String(data.substring(7, data.length() - 17));
          deleteUser(realUsername);
        }
      }
      clearRadioBuffer();
    }
  }
}

// show a message on the OLED screen of the device
void flashMessage(String &title, String &body)
{
  flashMessageAt = millis();
  flashMessageTitle = title;
  flashMessageBody = body;
}

// Refresh the OLED screen
void redraw()
{
  if (displayInit)
  {
    display.clearDisplay();
    if (flashMessageAt > 0 && (millis() - flashMessageAt) < 10000)
    {
      display.setCursor(0,PIX_ROW_1);
      display.setTextSize(1);
      display.print(flashMessageTitle);
      display.drawLine(0, 10, display.width()-1, 10, SSD1306_WHITE);
      display.setCursor(0,PIX_ROW_1p5);
      display.print(flashMessageBody);
    } else {
      display.setCursor(0,PIX_ROW_1);
      display.setTextSize(2);
      display.print(line1);
      display.setTextSize(1);
      display.setCursor(0,PIX_ROW_2);
      display.print(line2);
      display.setCursor(0,PIX_ROW_3);
      display.print(line3);
      display.setCursor(0,PIX_ROW_4);
      display.print(local_ip);
    }
    display.display();
  }
}

void clearIrcBuffer(int num)
{
  ircBufferPos[num] = 0;
  for(int n = 0; n < 2048; n++)
  {
    ircBuffers[num][n] = '\0';
  }
}

void sendIrcServerResponse(int num, String code, String data)
{
  int uid = ircUserId[num];
  String output = ":" + hostname + " " + code + " " + ircNicknames[num] + " " + data + "\r\n";
  //Serial.print("IRC RESP: ");
  //Serial.print(output);
  ircClients[num].print(output);
}

void ircMOTD(int num)
{
  sendIrcServerResponse(num, "375", ":- " + local_ip + " Message of the day -");
  sendIrcServerResponse(num, "372", ":-    _____       __    ________             ________          __");
  sendIrcServerResponse(num, "372", ":-   / ___/__  __/ /_  / ____/ /_  ____     / ____/ /_  ____ _/ /_");
  sendIrcServerResponse(num, "372", ":-   \\__ \\/ / / / __ \\/ / __/ __ \\/_  /    / /   / __ \\/ __ `/ __/");
  sendIrcServerResponse(num, "372", ":-  ___/ / /_/ / /_/ / /_/ / / / / / /_   / /___/ / / / /_/ / /_");
  sendIrcServerResponse(num, "372", ":- /____/\\__,_/_.___/\\____/_/ /_/ /___/   \\____/_/ /_/\\__,_/\\__/");
  sendIrcServerResponse(num, "372", ":-");
  sendIrcServerResponse(num, "372", ":-       https://github.com/xitiomet/flipperzero-chat-esp32");
  sendIrcServerResponse(num, "372", ":-");
  sendIrcServerResponse(num, "372", ":- FlipperZero's - Online: " + String(flipperCount) + " Seen: " + String(maxFlippers));
  sendIrcServerResponse(num, "372", ":- Network Connections: " + String(networkCount));
  sendIrcServerResponse(num, "372", ":- Radio Messages RX Total: " + String(radioRxCount));
  sendIrcServerResponse(num, "372", ":-");
  sendIrcServerResponse(num, "376", ":End of MOTD command");
}

void ircGreet(int num)
{
  sendIrcServerResponse(num, "001", ":Welcome to the SubGHZ Chat Network");
  sendIrcServerResponse(num, "002", ":Your host is " + local_ip + ", running FlipperZero SubGhz Chat Relay");
  sendIrcServerResponse(num, "003", ":This server was created on unknown");
  sendIrcServerResponse(num, "004", hostname + " SubGhzChat * *");
  sendIrcServerResponse(num, "005", "RFC2812 IRCD=FZSubGhzChat CHARSET=UTF-8 CASEMAPPING=ascii PREFIX=(qaohv)~&@%+ CHANTYPES=# CHANMODES=beI,k,l,imMnOPQRstVz CHANLIMIT=#&+:10 :are supported on this server");
  sendIrcServerResponse(num, "005", "CHANNELLEN=50 NICKLEN=20 TOPICLEN=490 AWAYLEN=127 KICKLEN=400 MODES=5 MAXLIST=beI:50 EXCEPTS=e INVEX=I PENALTY FNC :are supported on this server");
  sendIrcServerResponse(num, "251", ":There are " + String(userCount()) + " users on 1 server");
  sendIrcServerResponse(num, "252", "1 :IRC Operators online");
  sendIrcServerResponse(num, "254", "1 :channels formed");
  ircMOTD(num);
  ircClients[num].print(":TheReaper INVITE " + ircNicknames[num] + " #lobby\r\n");
  ircGreeted[num] = true;
}

// a stringtokenizer
bool get_token(String &from, String &to, uint8_t index, char separator)
{
  uint16_t start = 0, idx = 0;
  uint8_t cur = 0;
  while (idx < from.length())
  {
    if (from.charAt(idx) == separator)
    {
      if (cur == index)
      {
        to = from.substring(start, idx);
        return true;
      }
      cur++;
      while ((idx < from.length() - 1) && (from.charAt(idx + 1) == separator)) idx++;
      start = idx + 1;
    }
    idx++;
  }
  if ((cur == index) && (start < from.length()))
  {
    to = from.substring(start, from.length());
    return true;
  }
  return false;
}

void handleIrcCommand(int num)
{
  String line = String(ircBuffers[num]);
  if (line.equals(""))  return;
  //Serial.print("IRC IN: ");
  //Serial.println(line);
  if (line.startsWith("USER "))
  {
    String userLine = line.substring(5, line.length());
    ircUsernames[num] = userLine.substring(0, userLine.indexOf(' '));
    if (!ircUsernames[num].equals("") && !ircNicknames[num].equals("") && !ircGreeted[num])
    {
      ircGreet(num);
    }
    return;
  } else if (line.equals("CAP LS")) {
    // do nothing
    return;
  } else if (line.startsWith("PRIVMSG TheReaper ")) {
    String text = line.substring(18, line.length());
    text.trim();
    if (text.startsWith(":"))
    {
      text = text.substring(1, text.length());
    }
    //Serial.println("T:" + text);
    int uid = ircUserId[num];
    if (uid >= 0)
    {
      if (text.startsWith("!FREQ ") || text.startsWith("!freq "))
      {
        float newFreq = text.substring(6, text.length()).toFloat();
        changeFrequency(ircNicknames[num], newFreq);
        return;
      } else {
        String line = ":TheReaper PRIVMSG " + ircNicknames[num] + " :\001ACTION doesn't know that command. (" + text + ")\001\r\n";
        ircClients[num].print(line);
      }
    } else {
      String line = ":TheReaper PRIVMSG " + ircNicknames[num] + " :\001ACTION is a bot for #lobby. You must join #lobby to issues commands.\001\r\n";
      ircClients[num].print(line);
    }
    return;
  } else if (line.startsWith("PRIVMSG #lobby ")) {
    String text = line.substring(16, line.length());
    text.trim();
    //Serial.println("T:" + text);
    int uid = ircUserId[num];
    if (uid >= 0)
    {
      bool isAction = false;
      String username = members[uid];
      lobbyPrivmsg(username, text);
      if (text.startsWith("\001ACTION"))
      {
        isAction = true;
        text = username + " " + text.substring(8, text.length()-1);
      }
      member_last_active[uid] = millis();
      String out;
      StaticJsonDocument<3072> jsonBuffer;
      jsonBuffer["username"] = username;
      jsonBuffer["text"] = text;
      if (isAction)
      {
        jsonBuffer["event"] = "info";
      } else {
        jsonBuffer["event"] = "chat";
      }
      jsonBuffer["rssi"] = WiFi.RSSI();
      jsonBuffer["utc"] = now();
      jsonBuffer["source"] = member_sources[uid];
      serializeJson(jsonBuffer, out);
      webSocketServer.broadcastTXT(out);
      flashMessage(username, text);
      if (isAction)
      {
        String xmitData = "\x1B[0;94m" + text + "\x1B[0m\r\n";
        streamToRadio(xmitData);
        Serial2.print(xmitData);
      } else {
        String xmitData = "\x1B[0;91m" + username + "\x1B[0m: " + text + "\r\n";
        streamToRadio(xmitData);
        Serial2.print(xmitData);
      }
      saveHistory(out);
    }
    return;
  } else if (line.startsWith("JOIN #lobby")) {
    String nickname = ircNicknames[num];
    String username = ircUsernames[num];
    if (ircUserId[num] == -1)
    {
      // user is not registered!
      String rmip = ircClients[num].remoteIP().toString();
      String source = "irc_" + rmip;
      ircUserId[num] = registerUser(-1, username, source, WiFi.RSSI());
      String ircJoin = ":" + nickname + "!~" + username + "@" + source + " JOIN :#lobby\r\n";
      ircClients[num].print(ircJoin);
      sendIrcServerResponse(num, "332", "#lobby :" + String(frequency) + " Mhz");
      String namesList = "= #lobby :";
      for(int i = 0; i < MAX_MEMBERS; i++)
      {
        if (members[i] != "")
        {
          namesList += members[i] + " ";
        }
      }
      namesList += "@TheReaper";
      sendIrcServerResponse(num, "353", namesList);
      sendIrcServerResponse(num, "366", "#lobby :End of NAMES List");
    }
    return;
  } else if (line.startsWith("PART #lobby")) {
    String nickname = ircNicknames[num];
    String username = ircUsernames[num];
    deleteUser(nickname);
    ircUserId[num] = -1;
    return;
  } else if (line.startsWith("WHO #lobby")) {
    for(int i = 0; i < MAX_MEMBERS; i++)
    {
      if (members[i] != "")
      {
        String whoResp = "#lobby " + members[i] + " " + member_sources[i] + " " + local_ip + " " + members[i] + " H :0 " + members[i];
        sendIrcServerResponse(num, "352", whoResp);
      }
    }
    sendIrcServerResponse(num, "315", "#lobby :End of WHO List");
    return;
  } else if (line.startsWith("LIST ") || line.equals("LIST")) {
    sendIrcServerResponse(num, "321", "Channel :Users  Name");
    String listResp = "#lobby " + String(userCount()) + " :" + String(frequency) + " Mhz" ;
    sendIrcServerResponse(num, "322", listResp);
    sendIrcServerResponse(num, "323", ":End of List");
    return;
  } else if (line.startsWith("NAMES")) {
    String namesList = "= #lobby :";
    for(int i = 0; i < MAX_MEMBERS; i++)
    {
      if (members[i] != "")
      {
        namesList += members[i] + " ";
      }
    }
    namesList += "@TheReaper";
    sendIrcServerResponse(num, "353", namesList);
    sendIrcServerResponse(num, "366", "#lobby :End of NAMES List");
    return;
  } else if (line.startsWith("PING")) {
    String out = "PONG " + line.substring(5, line.length()) + "\r\n";
    ircClients[num].print(out);
    int uid = ircUserId[num];
    if (uid >= 0)
    {
      member_last_active[uid] = millis();
    }
    return;
  } else if (line.startsWith("ISON ")) {
    String nicknames = line.substring(5, line.length());
    nicknames.trim();
    //Serial.println("ISON:" + nicknames);
    String nickname = "";
    uint8_t nick_idx = 0;
    String respList = "";
    while(get_token(nicknames, nickname, nick_idx, ' '))
    {
      //Serial.println("check:" + nickname);
      if (findUser(nickname) >= 0)
      {
        respList += nickname + " ";
      }
      nick_idx++;
    }
    respList.trim();
    sendIrcServerResponse(num, "303", ":" + respList);
    return;
  } else if (line.indexOf("NICK ") >= 0) {
    int nickLocation = line.indexOf("NICK ");
    String nickname = line.substring(5+nickLocation, line.length());
    if (nickname.startsWith(":"))
      nickname = nickname.substring(1, nickname.length());
    if (ircNicknames[num].equals(""))
    {
      ircClients[num].println(line);
    } else {
      userChangeName(ircNicknames[num], nickname, true);
    }
    ircNicknames[num] = nickname;
    if (!ircUsernames[num].equals("") && !ircNicknames[num].equals("") && !ircGreeted[num])
    {
      ircGreet(num);
    }
    return;
  } else {
    String command = line;
    String arg1 = "";
    String arg2 = "";
    int firstSpace = line.indexOf(' ');
    if (firstSpace >= 0)
    {
      command = line.substring(0, firstSpace);
      String laterHalf = line.substring(firstSpace+1, line.length());
      int secondSpace = laterHalf.indexOf(' ');
      if (secondSpace >= 0)
      {
        arg1 = laterHalf.substring(0, secondSpace);
        arg2 = laterHalf.substring(secondSpace+1, laterHalf.length());
      } else {
        arg1 = laterHalf;
      }
    }
    if (command.equals("PRIVMSG"))
    {
      if (arg2.startsWith(":"))
      {
        arg2 = arg2.substring(1, arg2.length());
      }
      int findIrcId = findIrcNick(arg1);
      if (findIrcId >= 0)
      {
        String line = ":" + ircNicknames[num] + " PRIVMSG " + ircNicknames[findIrcId] + " :" + arg2 + "\r\n";
        ircClients[findIrcId].print(line);
        return;
      } else {
        int findUserId = findUser(arg1);
        if (findUserId >= 0)
        {
          int wsNum = member_nums[findUserId];
          if (wsNum >= 0)
          {
            int uid = ircUserId[num];
            if (uid >=0)
            {
              String nickname = ircNicknames[num];
              bool isAction = false;
              if (arg2.startsWith("\001ACTION"))
              {
                isAction = true;
                arg2 = nickname + " " + arg2.substring(8, arg2.length()-1);
              }
              StaticJsonDocument<3072> jsonBuffer;
              jsonBuffer["username"] = nickname;
              jsonBuffer["text"] = arg2;
              jsonBuffer["event"] = "private";
              jsonBuffer["rssi"] = WiFi.RSSI();
              jsonBuffer["utc"] = now();
              jsonBuffer["action"] = isAction;
              jsonBuffer["source"] = member_sources[uid];
              jsonBuffer["to"] = members[findUserId];
              String out;
              serializeJson(jsonBuffer, out);
              //Serial.println(out);
              webSocketServer.sendTXT(wsNum, out);
            } else {
              String line = ":" + members[findUserId] + " PRIVMSG " + ircNicknames[num] + " :\001ACTION is a websocket user. You must join #lobby to send messages to websocket users\001\r\n";
              ircClients[num].print(line);
              return;
            }
          } else {
            String line = ":" + members[findUserId] + " PRIVMSG " + ircNicknames[num] + " :\001ACTION is not capable of receiving private messages. This is likely because they are connected from a remote radio device.\001\r\n";
            ircClients[num].print(line);
            return;
          }
        } else {
          sendIrcServerResponse(num, "401", arg1 + " :No suck nick/channel");
          return;
        }
      }
    } else if (command.equals("NOTICE")) {
      if (arg2.startsWith(":"))
      {
        arg2 = arg2.substring(1, arg2.length());
      }
      if (arg1.equals("#lobby"))
      {
        //String line = ":" + ircNicknames[num] + " NOTICE #lobby :" + arg2 + "\r\n";
        lobbyNotice(ircNicknames[num], arg2);
      } else {
        int findIrcId = findIrcNick(arg1);
        if (findIrcId >= 0)
        {
          String line = ":" + ircNicknames[num] + " NOTICE " + ircNicknames[findIrcId] + " :" + arg2 + "\r\n";
          ircClients[findIrcId].print(line);
          return;
        }
      }
    } else if (command.equals("MOTD") || command.equals("motd")) {
      ircMOTD(num);
      return;
    } else if (command.equals("AWAY")) {

    } else if (command.equals("WHO")) {
      for(int i = 0; i < MAX_MEMBERS; i++)
      {
        if (members[i] != "" && members[i].equals(arg1))
        {
          String whoResp = "#lobby " + members[i] + " " + member_sources[i] + " " + local_ip + " " + members[i] + " H :0 " + members[i];
          sendIrcServerResponse(num, "352", whoResp);
        }
      }
      sendIrcServerResponse(num, "315", "#lobby :End of WHO List");
      return;
    } else if (command.equals("WHOIS")) {
      int uid = findUser(arg1);
      if (uid >= 0)
      {
          sendIrcServerResponse(num, "311", members[uid] + " ~" + members[uid] + " " + member_sources[uid] + " * :RSSI " + String(member_rssi[uid]));
          sendIrcServerResponse(num, "312", members[uid] + " " + local_ip + " :IRC Server");
          sendIrcServerResponse(num, "319", members[uid] + " :@#lobby");
          sendIrcServerResponse(num, "378", members[uid] + " :is connecting from *@" + member_sources[uid] + " " + member_sources[uid]);
          //sendResponse("338", wi.getNick() + " 255.255.255.255 :actually using host");
          /*
          if (wi.getAway() != null)
          {
             sendResponse("301", wi.getNick() + " :" + wi.getAway());
          }*/
          int idle = ((millis() - member_last_active[uid]) / 1000);
          sendIrcServerResponse(num, "317", members[uid] + " " + String(idle) + " :Seconds Idle");
      }
      sendIrcServerResponse(num, "318", members[uid] + " :End of WHOIS list");
      return;
    } else if (command.equals("QUIT")) {
      int uid = ircUserId[num];
      ircUserId[num] = -1;
      ircUsernames[num] = "";
      ircNicknames[num] = "";
      ircBufferPos[num] = 0;
      ircBuffers[num][0] = '\0';
      if (uid >= 0)
        deleteUser(uid);
      ircClients[num].stop();
      return;
    } else if (command.equals("MODE")) {
      
    } else {
      sendIrcServerResponse(num, "421", command + " :Unknown Command");
      return;
    }
  }
}

void appendToIrcBuffer(int num, char c)
{
  if (ircBufferPos[num] < 2047)
  {
    if (c == '\r' || c == '\n')
    {
      handleIrcCommand(num);
      clearIrcBuffer(num);
    } else {
      ircBuffers[num][ircBufferPos[num]] = c;
      ircBufferPos[num]++;
      ircBuffers[num][ircBufferPos[num]] = '\0';
    }
  } else {
    clearIrcBuffer(num);
  }
}

int findIrcNick(String &nickname)
{
  for(int i = 0; i < MAX_IRC_CLIENTS; i++)
  {
    if (ircNicknames[i].equals(nickname))
    {
      return i;
    }
  }
  return -1;
}

void loopIrc()
{
  for (int i = 0; i < MAX_IRC_CLIENTS; i++)
  {
    // check for disconnections
    if (!ircClients[i] && (!ircUsernames[i].equals("") || !ircNicknames[i].equals("")))
    {
      int uid = ircUserId[i];
      ircUserId[i] = -1;
      ircUsernames[i] = "";
      ircNicknames[i] = "";
      ircBufferPos[i] = 0;
      ircBuffers[i][0] = '\0';
      ircGreeted[i] = false;
      if (uid >= 0)
        deleteUser(uid);
    }
  }
  if (ircServer.hasClient())
  {
    int i;
    for (i = 0; i < MAX_IRC_CLIENTS; i++)
    {
      if (!ircClients[i]) // equivalent to !serverClients[i].connected()
      {
        ircGreeted[i] = false;
        ircUsernames[i] = "";
        ircNicknames[i] = "";
        ircUserId[i] = -1;
        ircClients[i] = ircServer.available();
        //Serial.print("New IRC client: ");
        //Serial.println(ircClients[i].remoteIP());
        break;
      }
    }

    //no free/disconnected spot so reject
    if (i >= MAX_IRC_CLIENTS) 
    {
      ircServer.available().println("busy");
    }
  }
  for (int i = 0; i < MAX_IRC_CLIENTS; i++)
  {
    while (ircClients[i].available())
    {
      char c = ircClients[i].read();
      appendToIrcBuffer(i, c);
    }
  }
}

void lobbyPrivmsg(String &username, String &text)
{
  String line = ":" + username + " PRIVMSG #lobby :" + text;
  broadcastIrcExcept(username, line);
}

void lobbyNotice(String &username, String &text)
{
  String line = ":" + username + " NOTICE #lobby :" + text;
  broadcastIrcExcept(username, line);
}

void userChangeName(String &oldnick, String &newnick, bool radio)
{
  int uid = findUser(oldnick);
  if (uid >= 0)
  {
    String line = ":" + oldnick + " NICK :" + newnick;
    broadcastIrc(line);
    members[uid] = newnick;
    int wsNum = member_nums[uid];
    if (member_sources[uid].startsWith("irc"))
      wsNum = -1;
    String out;
    StaticJsonDocument<1024> jsonBuffer;
    jsonBuffer["username"] = oldnick;
    jsonBuffer["event"] = "name";
    jsonBuffer["rssi"] = member_rssi[uid];
    jsonBuffer["source"] = member_sources[uid];
    jsonBuffer["to"] = newnick;
    serializeJson(jsonBuffer, out);
    for(int i = 0; i < webSocketServer.connectedClients(); i++)
    {
      if (i != wsNum)
        webSocketServer.sendTXT(i, out);
    }
    String xmitData = "\x1B[0;94m" + oldnick + " is now known as " + newnick + "\x1B[0m\r\n";
    if (radio)
    {
      streamToRadio(xmitData);
    }
    Serial2.print(xmitData);
  }
}

void broadcastIrcExcept(String &username, String &line)
{
  //Serial.print("IRC OUT: ");
  //Serial.println(line);
  int uid = findUser(username); 
  for (int i = 0; i < MAX_IRC_CLIENTS; i++)
  {
    if (ircClients[i] && ircUserId[i] >= 0 && ircUserId[i] != uid)
    {
      ircClients[i].print(line);
      ircClients[i].print("\r\n");
    }
  }
}

void broadcastIrc(String &line)
{
  //Serial.print("IRC OUT ALL: ");
  //Serial.println(line);
  for (int i = 0; i < MAX_IRC_CLIENTS; i++)
  {
    if (ircClients[i] && ircUserId[i] >= 0)
    {
      ircClients[i].print(line);
      ircClients[i].print("\r\n");
    }
  }
}

void handleSerialString()
{
  String source = "serial";
  if (Serial2Buffer.startsWith("/NICK ") || Serial2Buffer.startsWith("/nick "))
  {
    if (Serial2Nickname.equals(""))
    {
      Serial2Nickname = Serial2Buffer.substring(6, Serial2Buffer.length());
      registerUser(-1, Serial2Nickname, source, 0);
    } else {
      String oldNick = Serial2Nickname;
      String newNick = Serial2Buffer.substring(6, Serial2Buffer.length());
      userChangeName(oldNick, newNick, true);
      Serial2Nickname = newNick;
    }    
    return;
  }
  if (Serial2Buffer.startsWith("/FREQ ") || Serial2Buffer.startsWith("/freq "))
  {
    float newFreq = Serial2Buffer.substring(6, Serial2Buffer.length()).toFloat();
    String username = Serial2Nickname;
    if (username.equals(""))
      username = "serial";
    changeFrequency(username, newFreq);
    return;
  }
  if (Serial2Buffer.equals("/advertise") || Serial2Buffer.equals("/ADVERTISE"))
  {
    advertiseGateway();
    return;
  }
  if (Serial2Buffer.equals("/history") || Serial2Buffer.equals("/HISTORY"))
  {
    replayHistorySerial2();
    return;
  }
  if (Serial2Buffer.equals("/status") || Serial2Buffer.equals("/STATUS"))
  {
    Serial2.print("*** " + hostname + " status ***\r\n");
    Serial2.print("UTC " + String(now()) + "\r\n");
    Serial2.print("SSID " + local_ssid + "\r\n");
    Serial2.print("IP " + local_ip + "\r\n");
    Serial2.print("Frequency " + String(frequency) + " Mhz\r\n");
    Serial2.print("Radio packets received " + String(radioRxCount) + "\r\n");
    Serial2.print("FlipperZero's - Online " + String(flipperCount) + ", Seen " + String(maxFlippers) + "\r\n");
    Serial2.print("Network Connections " + String(networkCount) + "\r\n");
    Serial2.print("Total Users " + String(userCount()) + "\r\n");
    Serial2.print("\r\n");
    for(int i = 0; i < MAX_MEMBERS; i++)
    {
      if (members[i] != "")
      {
        long idle = millis() - member_last_active[i];
        Serial2.print(members[i] + "@" + member_sources[i] + " IDLE " + String(idle) + "ms RSSI " + String(member_rssi[i]) + "\r\n");
      }
    }
    Serial2.print("\r\n");
    return;
  }
  if (Serial2Buffer.equals("/restart") || Serial2Buffer.equals("/RESTART"))
  {
    ESP.restart();
    return;
  }
  if (Serial2Buffer.equals("/part") || Serial2Buffer.equals("/quit") || Serial2Buffer.equals("/PART") || Serial2Buffer.equals("/QUIT"))
  {
    if (!Serial2Nickname.equals(""))
    {
      deleteUser(Serial2Nickname);
      Serial2Nickname = "";
    }
    return;
  }
  if (!Serial2Nickname.equals(""))
  {
    int uid = findUser(Serial2Nickname);
    if (uid == -1)
    {
      uid = registerUser(-1, Serial2Nickname, source, 0);
    } else {
      member_rssi[uid] = 0;
    }
    if (uid >= 0 && uid < MAX_MEMBERS)
    {
      member_last_active[uid] = millis();
    }
    String out;
    StaticJsonDocument<3072> jsonBuffer;
    jsonBuffer["username"] = Serial2Nickname;
    jsonBuffer["text"] = Serial2Buffer;
    jsonBuffer["event"] = "chat";
    jsonBuffer["rssi"] = 0;
    jsonBuffer["utc"] = now();
    jsonBuffer["source"] = source;
    serializeJson(jsonBuffer, out);
    webSocketServer.broadcastTXT(out);
    flashMessage(Serial2Nickname,Serial2Buffer);
    lobbyPrivmsg(Serial2Nickname,Serial2Buffer);
    saveHistory(out);
    String xmitData = "\x1B[0;91m" + Serial2Nickname + "\x1B[0m: " + Serial2Buffer + "\r\n";
    Serial2.print(xmitData);
    streamToRadio(xmitData);
  } else {
    String xmitData = "You Must set your nickname by typing \"/NICK yourname\" before you can send messages!\r\n";
    Serial2.print(xmitData);
  }
}

void loopSerial()
{
  while (Serial2.available())
  {
    // get the new byte:
    char inChar = (char)Serial2.read();
    // add it to the inputString:
    Serial2Buffer += inChar;
    // if the incoming character is a newline, set a flag so the main loop can
    // do something about it:
    if (inChar == '\n' || inChar == '\r') {
      Serial2Buffer.trim();
      handleSerialString();
      Serial2Buffer = "";
    }
  }
}

time_t getNTPTime()
{
  if (timeClient.isTimeSet())
  {
    return (time_t) timeClient.getEpochTime();
  } else {
    return 0;
  }
}

void loopNetwork()
{
  if (!apMode)
  {
    timeClient.update();
  }
  webSocketServer.loop();
  httpServer.handleClient();
  loopIrc();
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
    } else {
      if (last_wl_status != wl_status)
      {
        local_ip = "";
      }
    }
  }
  flipperCount = 0;
  networkCount = 0;
  for(int i = 0; i < MAX_MEMBERS; i++)
  {
    if (members[i] != "")
    {
      if (member_sources[i].equals("flipper"))
      {
        flipperCount++;
      } else if (member_sources[i].startsWith("irc_") || member_sources[i].startsWith("ws_")) {
        networkCount++;
      }
      if ((member_sources[i].equals("flipper") || member_sources[i].equals("radio") || member_sources[i].equals("serial")) && member_last_active[i] >= 0)
      {
        long idle = millis() - member_last_active[i];
        //Serial.println(members[i] + " IDLE: " + String(idle));
        // Lets get rid of radio users that have been quiet for over an hour
        if (idle > 3600000)
        {
          deleteUser(i);
        }
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
    tick++;
    if (tick >= 60)
      tick = 0;
  }
  loopSerial();
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
        jsonBuffer["event"] = "greet";
        jsonBuffer["clientIp"] = ip.toString();
        jsonBuffer["mhz"] = frequency;
        jsonBuffer["replayChatHistory"] = replayChatHistory;
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
    case WStype_TEXT: {
        if (lenght > 0)
        {
          if (payload[0] == '{') {
            processJSONPayload(num, payload);
          }
        }
        break;
      }
    }
}

void onNetworkConnect()
{
  local_ssid = WiFi.SSID();
  local_ip = WiFi.localIP().toString();
  Serial.print("SSID: ");
  Serial.println(local_ssid);
  Serial.print("Network IP: ");
  Serial.println(local_ip);
  tryMDNS();
  advertiseGateway();
}

void tryMDNS()
{
  if (!mdns_started)
  {
    if (MDNS.begin(hostname.c_str()))
    {
      MDNS.addService("ws", "tcp", 81);
      MDNS.addService("http", "tcp", 80);
      MDNS.addService("irc", "tcp", 6667);
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
    if (settings.containsKey("hostname"))
    {
      hostname = settings["hostname"].as<String>();
    }
    unsigned long Serial2_baud = 115200;
    if (settings.containsKey("Serial2Baud"))
    {
      Serial2_baud = settings["Serial2Baud"].as<unsigned long>();
    }
    Serial2.begin(Serial2_baud);
    Serial2.print("AT+NAME" + hostname + "\r\n");

    if (settings.containsKey("captiveDNS"))
    {
      captiveDNS = settings["captiveDNS"].as<bool>();
    }
    if (settings.containsKey("replayChatHistory"))
    {
      replayChatHistory = settings["replayChatHistory"].as<bool>();
    }
    if (apMode)
    {
      Serial.println("Access Point Mode!");
      WiFi.mode(WIFI_AP);
      WiFi.setHostname(hostname.c_str());
      WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
      local_ip = apIP.toString();
      local_ssid = settings["apSSID"].as<String>();
      advertiseGateway();
      if (settings.containsKey("apPassword"))
      {
        String password = settings["apPassword"].as<String>();
        if (password.equals("null"))
        {
          Serial.println("AP has no password");
          WiFi.softAP(local_ssid.c_str(), NULL);
        } else {
          Serial.print("AP password = ");
          Serial.println(password);
          WiFi.softAP(local_ssid.c_str(), password.c_str());
        }
      } else {
        Serial.println("AP has no password");
        WiFi.softAP(local_ssid.c_str(), NULL);
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
      WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
      WiFi.setHostname(hostname.c_str());
      timeClient.begin();
      setSyncProvider(getNTPTime);
      setSyncInterval(30);
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
    if (httpServer.method() == HTTP_POST)
    {
      if (httpServer.hasArg("body") && path.equals("/settings.json"))
      {
        Serial.println("Write settings.json");
        String body = httpServer.arg("body");
        File file = FFat.open("/settings.json", FILE_WRITE);
        file.print(body);
        file.flush();
        file.close();
      }
    }
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
