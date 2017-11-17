#include <FS.h>                                               // This needs to be first, or it all crashes and burns

#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>                                      // https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ESP8266mDNS.h>                                      // Useful to access to ESP by hostname.local

#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include "sha256.h"

#include <Ticker.h>                                           // For LED status
#include <NTPClient.h>

const int configpin = 10;                                     // GPIO10
const int ledpin = BUILTIN_LED;                               // Built in LED defined for WEMOS people
const char *wifi_config_name = "IR Controller Configuration";
const char serverName[] = "checkip.dyndns.org";
int port = 80;
char passcode[20] = "";
char host_name[20] = "";
char port_str[6] = "80";
char user_id[60] = "";
const char* fingerprint = "8D 83 C3 5F 0A 09 84 AE B0 64 39 23 8F 05 9E 4D 5E 08 60 06";

char static_ip[16] = "10.0.1.10";
char static_gw[16] = "10.0.1.1";
char static_sn[16] = "255.255.255.0";

DynamicJsonBuffer jsonBuffer;
JsonObject& deviceState = jsonBuffer.createObject();

ESP8266WebServer server(port);
HTTPClient http;
Ticker ticker;

bool shouldSaveConfig = false;                                // Flag for saving data
bool holdReceive = false;                                     // Flag to prevent IR receiving while transmitting

int pinr1 = 14;                                               // Receiving pin
int pins1 = 4;                                                // Transmitting preset 1
int pins2 = 5;                                                // Transmitting preset 2
int pins3 = 12;                                               // Transmitting preset 3
int pins4 = 13;                                               // Transmitting preset 4

IRrecv irrecv(pinr1);
IRsend irsend1(pins1);
IRsend irsend2(pins2);
IRsend irsend3(pins3);
IRsend irsend4(pins4);

const unsigned long resetfrequency = 259200000;                // 72 hours in milliseconds
const int timeOffset = -14400;                                 // Timezone offset in seconds
const char* poolServerName = "time.nist.gov";

const bool getTime = true;                                     // Set to false to disable querying for the time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, poolServerName, timeOffset, (int)resetfrequency);

const bool getExternalIP = true;                               // Set to false to disable querying external IP
String _ip = "";
unsigned long lastupdate = 0;

class Code {
  public:
    char encoding[14] = "";
    char address[20] = "";
    char command[40] = "";
    char data[40] = "";
    String raw = "";
    int bits = 0;
    char timestamp[40] = "";
    bool valid = false;
};

Code last_recv;
Code last_recv_2;
Code last_recv_3;
Code last_recv_4;
Code last_recv_5;
Code last_send;
Code last_send_2;
Code last_send_3;
Code last_send_4;
Code last_send_5;


//+=============================================================================
// Callback notifying us of the need to save config
//
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


//+=============================================================================
// Reenable IR receiving
//
void resetReceive() {
  if (holdReceive) {
    Serial.println("Reenabling receiving");
    irrecv.resume();
    holdReceive = false;
  }
}


//+=============================================================================
// Valid command request using HMAC
//
bool validateHMAC(String epid, String mid, String timestamp, String signature) {
    time_t timethen = timestamp.toInt();
    time_t timenow = timeClient.getEpochTime() - timeOffset;

    if (abs(timethen - timenow) > 30) {
      Serial.println("Failed security check, signature is too old");
      Serial.print("Server: ");
      Serial.println(timethen);
      Serial.print("Local: ");
      Serial.println(timenow);
      Serial.print("MID: ");
      Serial.println(mid);
      return false;
    }

    uint8_t *hash;
    String key = String(user_id);
    Sha256.initHmac((uint8_t*)key.c_str(), key.length()); // key, and length of key in bytes
    Sha256.print(epid);
    Sha256.print(mid);
    Sha256.print(timestamp);
    hash = Sha256.resultHmac();
    String computedSignature = bin2hex(hash, HASH_LENGTH);

    if (computedSignature != signature) {
      Serial.println("Failed security check, signatures do not match");
      Serial.print("1: ");
      Serial.println(signature);
      Serial.print("2: ");
      Serial.println(computedSignature);
      Serial.print("MID: ");
      Serial.println(mid);
      return false;
    }
    
    Serial.println("Passed security check");
    Serial.print("MID: ");
    Serial.println(mid);
    return true;
}


//+=============================================================================
// Get User_ID from Amazon Token (memory intensive and causes crashing)
//
String getUserID(String token)
{
  http.setTimeout(5000);
  String url = "https://api.amazon.com/user/profile?access_token=";  
  String uid = "";
  http.begin(url + token, fingerprint);
  int httpCode = http.GET();
  String payload = http.getString();
  Serial.println(url + token);
  Serial.println(httpCode);
  Serial.println(payload);
  if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.parseObject(payload);
    uid = json["user_id"].as<String>();
  } else {
    Serial.println("Error retrieving user_id");
    payload = "";
  }
  http.end();
  return uid;
}


//+=============================================================================
// Toggle state
//
void tick()
{
  int state = digitalRead(ledpin);  // get the current state of GPIO1 pin
  digitalWrite(ledpin, !state);     // set pin to the opposite state
}


//+=============================================================================
// Get External IP Address
//
String externalIP()
{
  if (!getExternalIP) {
    return "0.0.0.0"; // User doesn't want the external IP
  }

  if (_ip != "") {
    if (millis() - lastupdate > resetfrequency || lastupdate > millis()) {
      Serial.println("Reseting cached external IP address");
      _ip = ""; // Reset the cached external IP every 72 hours
    } else {
      return _ip;
    }
  }
  unsigned long start = millis();
  http.setTimeout(5000);
  http.begin(serverName, 8245);
  int httpCode = http.GET();

  if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    int pos_start = payload.indexOf("IP Address") + 12; // add 10 for "IP Address" and 2 for ":" + "space"
    int pos_end = payload.indexOf("</body>", pos_start); // add nothing
    _ip = payload.substring(pos_start, pos_end);
    Serial.print(F("External IP: "));
    Serial.println(_ip);
  } else {
    Serial.println("Error retrieving external IP");
  }

  http.end();
  Serial.print("External IP address request took ");
  Serial.print(millis() - start);
  Serial.println(" ms");

  return _ip;
}


//+=============================================================================
// Turn off the Led after timeout
//
void disableLed()
{
  Serial.println("Turning off the LED to save power.");
  digitalWrite(ledpin, HIGH);                           // Shut down the LED
  ticker.detach();                                      // Stopping the ticker
}


//+=============================================================================
// Gets called when WiFiManager enters configuration mode
//
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}


//+=============================================================================
// First setup of the Wifi.
// If return true, the Wifi is well connected.
// Should not return false if Wifi cannot be connected, it will loop
//
bool setupWifi(bool resetConf) {
  // set led pin as output
  pinMode(ledpin, OUTPUT);
  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);

  // WiFiManager
  // Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  // reset settings - for testing
  if (resetConf)
    wifiManager.resetSettings();

  // set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);
  // set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          if (json.containsKey("hostname")) strncpy(host_name, json["hostname"], 20);
          if (json.containsKey("passcode")) strncpy(passcode, json["passcode"], 20);
          if (json.containsKey("user_id")) strncpy(user_id, json["user_id"], 60);
          if (json.containsKey("port_str")) {
            strncpy(port_str, json["port_str"], 6);
            port = atoi(json["port_str"]);
          }
          if (json.containsKey("ip")) strncpy(static_ip, json["ip"], 16);
          if (json.containsKey("gw")) strncpy(static_gw, json["gw"], 16);
          if (json.containsKey("sn")) strncpy(static_sn, json["sn"], 16);
        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }

  WiFiManagerParameter custom_hostname("hostname", "Choose a hostname to this IR Controller", host_name, 20);
  wifiManager.addParameter(&custom_hostname);
  WiFiManagerParameter custom_passcode("passcode", "Choose a passcode", passcode, 20);
  wifiManager.addParameter(&custom_passcode);
  WiFiManagerParameter custom_port("port_str", "Choose a port", port_str, 6);
  wifiManager.addParameter(&custom_port);
  WiFiManagerParameter custom_userid("user_id", "Enter your Amazon user_id", user_id, 60);
  wifiManager.addParameter(&custom_userid);

  IPAddress sip, sgw, ssn;
  sip.fromString(static_ip);
  sgw.fromString(static_gw);
  ssn.fromString(static_sn);
  Serial.println("Using Static IP");
  wifiManager.setSTAStaticIPConfig(sip, sgw, ssn);

  // fetches ssid and pass and tries to connect
  // if it does not connect it starts an access point with the specified name
  // and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect(wifi_config_name)) {
    Serial.println("failed to connect and hit timeout");
    // reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  }

  // if you get here you have connected to the WiFi
  strncpy(host_name, custom_hostname.getValue(), 20);
  strncpy(passcode, custom_passcode.getValue(), 20);
  strncpy(port_str, custom_port.getValue(), 6);
  strncpy(user_id, custom_userid.getValue(), 60);
  port = atoi(port_str);

  if (port != 80) {
    Serial.println("Default port changed");
    server = ESP8266WebServer(port);
  }

  Serial.println("WiFi connected! User chose hostname '" + String(host_name) + String("' passcode '") + String(passcode) + "' and port '" + String(port_str) + "'");

  // save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println(" config...");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["hostname"] = host_name;
    json["passcode"] = passcode;
    json["port_str"] = port_str;
    json["user_id"] = user_id;
    json["ip"] = WiFi.localIP().toString();
    json["gw"] = WiFi.gatewayIP().toString();
    json["sn"] = WiFi.subnetMask().toString();

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    Serial.println("");
    json.printTo(configFile);
    configFile.close();
    jsonBuffer.clear();
  }
  ticker.detach();

  // keep LED on
  digitalWrite(ledpin, LOW);
  return true;
}


//+=============================================================================
// Setup web server and IR receiver/blaster
//
void setup() {

  // Initialize serial
  Serial.begin(115200);
  Serial.println("");
  Serial.println("ESP8266 IR Controller");
  pinMode(configpin, INPUT_PULLUP);
  Serial.print("Config pin GPIO");
  Serial.print(configpin);
  Serial.print(" set to: ");
  Serial.println(digitalRead(configpin));
  if (!setupWifi(digitalRead(configpin) == LOW))
    return;

  WiFi.hostname(host_name);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  wifi_set_sleep_type(LIGHT_SLEEP_T);
  digitalWrite(ledpin, LOW);
  // Turn off the led in 2s
  ticker.attach(2, disableLed);

  // Configure mDNS
  if (MDNS.begin(host_name)) {
    Serial.println("mDNS started. Hostname is set to " + String(host_name) + ".local");
  }
  Serial.print("Local IP: ");
  Serial.println(ipToString(WiFi.localIP()));
  MDNS.addService("http", "tcp", port); // Announce the ESP as an HTTP service
  Serial.println("URL to send commands: http://" + String(host_name) + ".local:" + port_str);

  if (getTime || strlen(user_id) != 0) timeClient.begin(); // Get the time

  // Configure the server
  server.on("/json", []() { // JSON handler for more complicated IR blaster routines
    Serial.println("Connection received - JSON");

    DynamicJsonBuffer jsonBuffer;
    JsonArray& root = jsonBuffer.parseArray(server.arg("plain"));

    int simple = 0;
    if (server.hasArg("simple")) simple = server.arg("simple").toInt();
    String signature = server.arg("auth");
    String epid = server.arg("epid");
    String mid = server.arg("mid");
    String timestamp = server.arg("time");
    
    if (!root.success()) {
      Serial.println("JSON parsing failed");
      if (simple) {
        server.send(400, "text/plain", "JSON parsing failed");
      } else {
        sendHomePage("JSON parsing failed", "Error", 3, 400); // 400
      }
      jsonBuffer.clear();
    } else if (server.arg("pass") != passcode) {
      Serial.println("Unauthorized access");
      if (simple) {
        server.send(401, "text/plain", "Unauthorized, invalid passcode");
      } else {
        sendHomePage("Invalid passcode", "Unauthorized", 3, 401); // 401
      }
      jsonBuffer.clear();
    } else if (strlen(user_id) != 0 && !validateHMAC(epid, mid, timestamp, signature)) {
      server.send(401, "text/plain", "Unauthorized, HMAC security authentication failed");
    } else {
      digitalWrite(ledpin, LOW);
      ticker.attach(0.5, disableLed);

      // Handle device state limitations for the global JSON command request
      if (server.hasArg("device")) {
        String device = server.arg("device");
        Serial.println("Device name detected " + device);
        int state = (server.hasArg("state")) ? server.arg("state").toInt() : 0;
        if (deviceState.containsKey(device)) {
          Serial.println("Contains the key!");
          Serial.println(state);
          int currentState = deviceState[device];
          Serial.println(currentState);
          if (state == currentState) {
            if (simple) {
              server.send(200, "text/html", "Not sending command to " + device + ", already in state " + state);
            } else {
              sendHomePage("Not sending command to " + device + ", already in state " + state, "Warning", 2); // 200
            }
            Serial.println("Not sending command to " + device + ", already in state " + state);
            return;
          } else {
            Serial.println("Setting device " + device + " to state " + state);
            deviceState[device] = state;
          }
        } else {
          Serial.println("Setting device " + device + " to state " + state);
          deviceState[device] = state;
        }
      }

      if (simple) {
        server.send(200, "text/html", "Success, code sent");
      }

      String message = "Code sent";

      for (int x = 0; x < root.size(); x++) {
        String type = root[x]["type"];
        String ip = root[x]["ip"];
        int rdelay = root[x]["rdelay"];
        int pulse = root[x]["pulse"];
        int pdelay = root[x]["pdelay"];
        int repeat = root[x]["repeat"];
        int out = root[x]["out"];

        if (pulse <= 0) pulse = 1; // Make sure pulse isn't 0
        if (repeat <= 0) repeat = 1; // Make sure repeat isn't 0
        if (pdelay <= 0) pdelay = 100; // Default pdelay
        if (rdelay <= 0) rdelay = 1000; // Default rdelay

        // Handle device state limitations on a per JSON object basis
        String device = root[x]["device"];
        if (device != "") {
          int state = root[x]["state"];
          if (deviceState.containsKey(device)) {
            int currentState = deviceState[device];
            if (state == currentState) {
              Serial.println("Not sending command to " + device + ", already in state " + state);
              message = "Code sent. Some components of the code were held because device was already in appropriate state";
              continue;
            } else {
              Serial.println("Setting device " + device + " to state " + state);
              deviceState[device] = state;  
            }
          } else {
            Serial.println("Setting device " + device + " to state " + state);
            deviceState[device] = state;
          }
        }

        if (type == "delay") {
          delay(rdelay);
        } else if (type == "raw") {
          JsonArray &raw = root[x]["data"]; // Array of unsigned int values for the raw signal
          int khz = root[x]["khz"];
          if (khz <= 0) khz = 38; // Default to 38khz if not set
          rawblast(raw, khz, rdelay, pulse, pdelay, repeat, pickIRsend(out));
        } else if (type == "roku") {
          String data = root[x]["data"];
          rokuCommand(ip, data);
        } else {
          String data = root[x]["data"];
          long address = root[x]["address"];
          int len = root[x]["length"];
          irblast(type, data, len, rdelay, pulse, pdelay, repeat, address, pickIRsend(out));
        }
      }

      if (!simple) {
        Serial.println("Sending home page");
        sendHomePage(message, "Success", 1); // 200
      }

      jsonBuffer.clear();
    }
  });

  // Setup simple msg server to mirror version 1.0 functionality
  server.on("/msg", []() {
    Serial.println("Connection received - MSG");
    
    int simple = 0;
    if (server.hasArg("simple")) simple = server.arg("simple").toInt();
    String signature = server.arg("auth");
    String epid = server.arg("epid");
    String mid = server.arg("mid");
    String timestamp = server.arg("time");

    if (server.arg("pass") != passcode) {
      Serial.println("Unauthorized access");
      if (simple) {
        server.send(401, "text/plain", "Unauthorized, invalid passcode");
      } else {
        sendHomePage("Invalid passcode", "Unauthorized", 3, 401); // 401
      }
    } else if (strlen(user_id) != 0 && !validateHMAC(epid, mid, timestamp, signature)) {
      server.send(401, "text/plain", "Unauthorized, HMAC security authentication");
    } else {
      digitalWrite(ledpin, LOW);
      ticker.attach(0.5, disableLed);
      String type = server.arg("type");
      String data = server.arg("data");
      String ip = server.arg("ip");

      // Handle device state limitations
      if (server.hasArg("device")) {
        String device = server.arg("device");
        Serial.println("Device name detected " + device);
        int state = (server.hasArg("state")) ? server.arg("state").toInt() : 0;
        if (deviceState.containsKey(device)) {
          Serial.println("Contains the key!");
          Serial.println(state);
          int currentState = deviceState[device];
          Serial.println(currentState);
          if (state == currentState) {
            if (simple) {
              server.send(200, "text/html", "Not sending command to " + device + ", already in state " + state);
            } else {
              sendHomePage("Not sending command to " + device + ", already in state " + state, "Warning", 2); // 200
            }
            Serial.println("Not sending command to " + device + ", already in state " + state);
            return;
          } else {
            Serial.println("Setting device " + device + " to state " + state);
            deviceState[device] = state;
          }
        } else {
          Serial.println("Setting device " + device + " to state " + state);
          deviceState[device] = state;
        }
      }
      
      int len = server.arg("length").toInt();
      long address = (server.hasArg("address")) ? server.arg("address").toInt() : 0;
      int rdelay = (server.hasArg("rdelay")) ? server.arg("rdelay").toInt() : 1000;
      int pulse = (server.hasArg("pulse")) ? server.arg("pulse").toInt() : 1;
      int pdelay = (server.hasArg("pdelay")) ? server.arg("pdelay").toInt() : 100;
      int repeat = (server.hasArg("repeat")) ? server.arg("repeat").toInt() : 1;
      int out = (server.hasArg("out")) ? server.arg("out").toInt() : 0;
      if (server.hasArg("code")) {
        String code = server.arg("code");
        char separator = ':';
        data = getValue(code, separator, 0);
        type = getValue(code, separator, 1);
        len = getValue(code, separator, 2).toInt();
      }

      if (simple) {
        server.send(200, "text/html", "Success, code sent");
      }

      if (type == "roku") {
        rokuCommand(ip, data);
      } else {
        irblast(type, data, len, rdelay, pulse, pdelay, repeat, address, pickIRsend(out));
      }

      if (!simple) {
        sendHomePage("Code Sent", "Success", 1); // 200
      }
    }
  });

  server.on("/received", []() {
    Serial.println("Connection received");
    int id = server.arg("id").toInt();
    String output;
    if (id == 1 && last_recv.valid) {
      sendCodePage(last_recv);
    } else if (id == 2 && last_recv_2.valid) {
      sendCodePage(last_recv_2);
    } else if (id == 3 && last_recv_3.valid) {
      sendCodePage(last_recv_3);
    } else if (id == 4 && last_recv_4.valid) {
      sendCodePage(last_recv_4);
    } else if (id == 5 && last_recv_5.valid) {
      sendCodePage(last_recv_5);
    } else {
      sendHomePage("Code does not exist", "Alert", 2, 404); // 404
    }
  });

  server.on("/", []() {
    Serial.println("Connection received");
    sendHomePage(); // 200
  });

  server.begin();
  Serial.println("HTTP Server started on port " + String(port));

  externalIP();

  irsend1.begin();
  irsend2.begin();
  irsend3.begin();
  irsend4.begin();
  irrecv.enableIRIn();
  Serial.println("Ready to send and receive IR signals");
}


//+=============================================================================
// IP Address to String
//
String ipToString(IPAddress ip)
{
  String s="";
  for (int i=0; i<4; i++)
    s += i  ? "." + String(ip[i]) : String(ip[i]);
  return s;
}


//+=============================================================================
// Send command to local roku
//
int rokuCommand(String ip, String data) {
  String url = "http://" + ip + ":8060/" + data;
  http.begin(url);
  Serial.println(url);
  Serial.println("Sending roku command");

  copyCode(last_send_4, last_send_5);
  copyCode(last_send_3, last_send_4);
  copyCode(last_send_2, last_send_3);
  copyCode(last_send, last_send_2);

  strncpy(last_send.data, data.c_str(), 40);
  last_send.bits = 1;
  strncpy(last_send.encoding, "roku", 20);
  strncpy(last_send.address, ip.c_str(), 40);
  strncpy(last_recv.timestamp, String(timeClient.getFormattedTime()).c_str(), 40);
  last_send.valid = true;

  int output = http.POST("");
  http.end();
  return output;
}

//+=============================================================================
// Split string by character
//
String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }

  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}


//+=============================================================================
// Return which IRsend object to act on
//
IRsend pickIRsend (int out) {
  switch (out) {
    case 1: return irsend1;
    case 2: return irsend2;
    case 3: return irsend3;
    case 4: return irsend4;
    default: return irsend1;
  }
}


//+=============================================================================
// Display encoding type
//
// Display encoding type
//
String encoding(decode_results *results) {
  String output;
  switch (results->decode_type) {
    default:
    case UNKNOWN:      output = "UNKNOWN";            break;
    case NEC:          output = "NEC";                break;
    case SONY:         output = "SONY";               break;
    case RC5:          output = "RC5";                break;
    case RC6:          output = "RC6";                break;
    case DISH:         output = "DISH";               break;
    case SHARP:        output = "SHARP";              break;
    case JVC:          output = "JVC";                break;
    case SANYO:        output = "SANYO";              break;
    case SANYO_LC7461: output = "SANYO_LC7461";       break;
    case MITSUBISHI:   output = "MITSUBISHI";         break;
    case SAMSUNG:      output = "SAMSUNG";            break;
    case LG:           output = "LG";                 break;
    case WHYNTER:      output = "WHYNTER";            break;
    case AIWA_RC_T501: output = "AIWA_RC_T501";       break;
    case PANASONIC:    output = "PANASONIC";          break;
    case DENON:        output = "DENON";              break;
    case COOLIX:       output = "COOLIX";             break;
  }
  return output;
  if (results->repeat) Serial.print(" (Repeat)");
}

//+=============================================================================
// Uint64 to String
//
String Uint64toString(uint64_t input, uint8_t base) {
  char buf[8 * sizeof(input) + 1];  // Assumes 8-bit chars plus zero byte.
  char *str = &buf[sizeof(buf) - 1];

  *str = '\0';

  // prevent crash if called with base == 1
  if (base < 2) base = 10;

  do {
    char c = input % base;
    input /= base;

    *--str = c < 10 ? c + '0' : c + 'A' - 10;
  } while (input);

  std::string s(str);
  return s.c_str();
}

//+=============================================================================
// Code to string
//
void fullCode (decode_results *results)
{
  Serial.print("One line: ");
  serialPrintUint64(results->value, 16);
  Serial.print(":");
  Serial.print(encoding(results));
  Serial.print(":");
  Serial.print(results->bits, DEC);
  if (results->overflow)
    Serial.println("WARNING: IR code too long."
                   "Edit IRrecv.h and increase RAWBUF");
  Serial.println("");
}

//+=============================================================================
// Send header HTML
//
void sendHeader() {
  sendHeader(200);
}

void sendHeader(int httpcode) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(httpcode, "text/html; charset=utf-8", "");
  server.sendContent("<!DOCTYPE html PUBLIC '-//W3C//DTD XHTML 1.0 Strict//EN' 'http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd'>\n");
  server.sendContent("<html xmlns='http://www.w3.org/1999/xhtml' xml:lang='en'>\n");
  server.sendContent("  <head>\n");
  server.sendContent("    <meta name='viewport' content='width=device-width, initial-scale=.75' />\n");
  server.sendContent("    <link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/css/bootstrap.min.css' />\n");
  server.sendContent("    <style>@media (max-width: 991px) {.nav-pills>li {float: none; margin-left: 0; margin-top: 5px; text-align: center;}}</style>\n");
  server.sendContent("    <title>ESP8266 IR Controller (" + String(host_name) + ")</title>\n");
  server.sendContent("  </head>\n");
  server.sendContent("  <body>\n");
  server.sendContent("    <div class='container'>\n");
  server.sendContent("      <h1><a href='https://github.com/mdhiggins/ESP8266-HTTP-IR-Blaster'>ESP8266 IR Controller</a></h1>\n");
  server.sendContent("      <div class='row'>\n");
  server.sendContent("        <div class='col-md-12'>\n");
  server.sendContent("          <ul class='nav nav-pills'>\n");
  server.sendContent("            <li class='active'>\n");
  server.sendContent("              <a href='http://" + String(host_name) + ".local" + ":" + String(port) + "'>Hostname <span class='badge'>" + String(host_name) + ".local" + ":" + String(port) + "</span></a></li>\n");
  server.sendContent("            <li class='active'>\n");
  server.sendContent("              <a href='http://" + ipToString(WiFi.localIP()) + ":" + String(port) + "'>Local <span class='badge'>" + ipToString(WiFi.localIP()) + ":" + String(port) + "</span></a></li>\n");
  server.sendContent("            <li class='active'>\n");
  server.sendContent("              <a href='http://" + externalIP() + ":" + String(port) + "'>External <span class='badge'>" + externalIP() + ":" + String(port) + "</span></a></li>\n");
  server.sendContent("            <li class='active'>\n");
  server.sendContent("              <a href='#'>MAC <span class='badge'>" + String(WiFi.macAddress()) + "</span></a></li>\n");
  server.sendContent("          </ul>\n");
  server.sendContent("        </div>\n");
  server.sendContent("      </div><hr />\n");
}

//+=============================================================================
// Send footer HTML
//
void sendFooter() {
  server.sendContent("      <div class='row'><div class='col-md-12'><em>" + String(millis()) + "ms uptime</em></div></div>\n");
  if (strlen(user_id) != 0)
  server.sendContent("      <div class='row'><div class='col-md-12'><em>Device secured with SHA256 authentication. Only commands sent and verified with Amazon Alexa and the IR Controller Skill will be processed</em></div></div>");
  server.sendContent("    </div>\n");
  server.sendContent("  </body>\n");
  server.sendContent("</html>\n");
  server.client().stop();
}

//+=============================================================================
// Stream home page HTML
//
void sendHomePage() {
  sendHomePage("", "");
}

void sendHomePage(String message, String header) {
  sendHomePage(message, header, 0);
}

void sendHomePage(String message, String header, int type) {
  sendHomePage(message, header, type, 200);
}

void sendHomePage(String message, String header, int type, int httpcode) {
  sendHeader(httpcode);
  if (type == 1)
  server.sendContent("      <div class='row'><div class='col-md-12'><div class='alert alert-success'><strong>" + header + "!</strong> " + message + "</div></div></div>\n");
  if (type == 2)
  server.sendContent("      <div class='row'><div class='col-md-12'><div class='alert alert-warning'><strong>" + header + "!</strong> " + message + "</div></div></div>\n");
  if (type == 3)
  server.sendContent("      <div class='row'><div class='col-md-12'><div class='alert alert-danger'><strong>" + header + "!</strong> " + message + "</div></div></div>\n");
  server.sendContent("      <div class='row'>\n");
  server.sendContent("        <div class='col-md-12'>\n");
  server.sendContent("          <h3>Codes Transmitted</h3>\n");
  server.sendContent("          <table class='table table-striped' style='table-layout: fixed;'>\n");
  server.sendContent("            <thead><tr><th>Sent</th><th>Command</th><th>Type</th><th>Length</th><th>Address</th></tr></thead>\n"); //Title
  server.sendContent("            <tbody>\n");
  if (last_send.valid)
  server.sendContent("              <tr class='text-uppercase'><td>" + String(last_send.timestamp) + "</td><td><code>" + String(last_send.data) + "</code></td><td><code>" + String(last_send.encoding) + "</code></td><td><code>" + String(last_send.bits) + "</code></td><td><code>" + String(last_send.address) + "</code></td></tr>\n");
  if (last_send_2.valid)
  server.sendContent("              <tr class='text-uppercase'><td>" + String(last_send_2.timestamp) + "</td><td><code>" + String(last_send_2.data) + "</code></td><td><code>" + String(last_send_2.encoding) + "</code></td><td><code>" + String(last_send_2.bits) + "</code></td><td><code>" + String(last_send_2.address) + "</code></td></tr>\n");
  if (last_send_3.valid)
  server.sendContent("              <tr class='text-uppercase'><td>" + String(last_send_3.timestamp) + "</td><td><code>" + String(last_send_3.data) + "</code></td><td><code>" + String(last_send_3.encoding) + "</code></td><td><code>" + String(last_send_3.bits) + "</code></td><td><code>" + String(last_send_3.address) + "</code></td></tr>\n");
  if (last_send_4.valid)
  server.sendContent("              <tr class='text-uppercase'><td>" + String(last_send_4.timestamp) + "</td><td><code>" + String(last_send_4.data) + "</code></td><td><code>" + String(last_send_4.encoding) + "</code></td><td><code>" + String(last_send_4.bits) + "</code></td><td><code>" + String(last_send_4.address) + "</code></td></tr>\n");
  if (last_send_5.valid)
  server.sendContent("              <tr class='text-uppercase'><td>" + String(last_send_5.timestamp) + "</td><td><code>" + String(last_send_5.data) + "</code></td><td><code>" + String(last_send_5.encoding) + "</code></td><td><code>" + String(last_send_5.bits) + "</code></td><td><code>" + String(last_send_5.address) + "</code></td></tr>\n");
  if (!last_send.valid && !last_send_2.valid && !last_send_3.valid && !last_send_4.valid && !last_send_5.valid)
  server.sendContent("              <tr><td colspan='5' class='text-center'><em>No codes sent</em></td></tr>");
  server.sendContent("            </tbody></table>\n");
  server.sendContent("          </div></div>\n");
  server.sendContent("      <div class='row'>\n");
  server.sendContent("        <div class='col-md-12'>\n");
  server.sendContent("          <h3>Codes Received</h3>\n");
  server.sendContent("          <table class='table table-striped' style='table-layout: fixed;'>\n");
  server.sendContent("            <thead><tr><th>Time Sent</th><th>Command</th><th>Type</th><th>Length</th><th>Address</th></tr></thead>\n"); //Title
  server.sendContent("            <tbody>\n");
  if (last_recv.valid)
  server.sendContent("              <tr class='text-uppercase'><td><a href='/received?id=1'>" + String(last_recv.timestamp) + "</a></td><td><code>" + String(last_recv.data) + "</code></td><td><code>" + String(last_recv.encoding) + "</code></td><td><code>" + String(last_recv.bits) + "</code></td><td><code>" + String(last_recv.address) + "</code></td></tr>\n");
  if (last_recv_2.valid)
  server.sendContent("              <tr class='text-uppercase'><td><a href='/received?id=2'>" + String(last_recv_2.timestamp) + "</a></td><td><code>" + String(last_recv_2.data) + "</code></td><td><code>" + String(last_recv_2.encoding) + "</code></td><td><code>" + String(last_recv_2.bits) + "</code></td><td><code>" + String(last_recv_2.address) + "</code></td></tr>\n");
  if (last_recv_3.valid)
  server.sendContent("              <tr class='text-uppercase'><td><a href='/received?id=2'>" + String(last_recv_3.timestamp) + "</a></td><td><code>" + String(last_recv_3.data) + "</code></td><td><code>" + String(last_recv_3.encoding) + "</code></td><td><code>" + String(last_recv_3.bits) + "</code></td><td><code>" + String(last_recv_3.address) + "</code></td></tr>\n");
  if (last_recv_4.valid)
  server.sendContent("              <tr class='text-uppercase'><td><a href='/received?id=2'>" + String(last_recv_4.timestamp) + "</a></td><td><code>" + String(last_recv_4.data) + "</code></td><td><code>" + String(last_recv_4.encoding) + "</code></td><td><code>" + String(last_recv_4.bits) + "</code></td><td><code>" + String(last_recv_4.address) + "</code></td></tr>\n");
  if (last_recv_5.valid)
  server.sendContent("              <tr class='text-uppercase'><td><a href='/received?id=2'>" + String(last_recv_5.timestamp) + "</a></td><td><code>" + String(last_recv_5.data) + "</code></td><td><code>" + String(last_recv_5.encoding) + "</code></td><td><code>" + String(last_recv_5.bits) + "</code></td><td><code>" + String(last_recv_5.address) + "</code></td></tr>\n");
  if (!last_recv.valid && !last_recv_2.valid && !last_recv_3.valid && !last_recv_4.valid && !last_recv_5.valid)
  server.sendContent("              <tr><td colspan='5' class='text-center'><em>No codes received</em></td></tr>");
  server.sendContent("            </tbody></table>\n");
  server.sendContent("          </div></div>\n");
  server.sendContent("      <div class='row'>\n");
  server.sendContent("        <div class='col-md-12'>\n");
  server.sendContent("          <ul class='list-unstyled'>\n");
  server.sendContent("            <li><span class='badge'>GPIO " + String(pinr1) + "</span> Receiving </li>\n");
  server.sendContent("            <li><span class='badge'>GPIO " + String(pins1) + "</span> Transmitter 1 </li>\n");
  server.sendContent("            <li><span class='badge'>GPIO " + String(pins2) + "</span> Transmitter 2 </li>\n");
  server.sendContent("            <li><span class='badge'>GPIO " + String(pins3) + "</span> Transmitter 3 </li>\n");
  server.sendContent("            <li><span class='badge'>GPIO " + String(pins4) + "</span> Transmitter 4 </li></ul>\n");
  server.sendContent("        </div>\n");
  server.sendContent("      </div>\n");
  sendFooter();
}

//+=============================================================================
// Stream code page HTML
//
void sendCodePage(Code selCode) {
  sendCodePage(selCode, 200);
}

void sendCodePage(Code selCode, int httpcode){
  sendHeader(httpcode);
  server.sendContent("      <div class='row'>\n");
  server.sendContent("        <div class='col-md-12'>\n");
  server.sendContent("          <h2><span class='label label-success'>" + String(selCode.data) + ":" + String(selCode.encoding) + ":" + String(selCode.bits) + "</span></h2><br/>\n");
  server.sendContent("          <dl class='dl-horizontal'>\n");
  server.sendContent("            <dt>Data</dt>\n");
  server.sendContent("            <dd><code>" + String(selCode.data)  + "</code></dd></dl>\n");
  server.sendContent("          <dl class='dl-horizontal'>\n");
  server.sendContent("            <dt>Type</dt>\n");
  server.sendContent("            <dd><code>" + String(selCode.encoding)  + "</code></dd></dl>\n");
  server.sendContent("          <dl class='dl-horizontal'>\n");
  server.sendContent("            <dt>Length</dt>\n");
  server.sendContent("            <dd><code>" + String(selCode.bits)  + "</code></dd></dl>\n");
  server.sendContent("          <dl class='dl-horizontal'>\n");
  server.sendContent("            <dt>Address</dt>\n");
  server.sendContent("            <dd><code>" + String(selCode.address)  + "</code></dd></dl>\n");
  server.sendContent("          <dl class='dl-horizontal'>\n");
  server.sendContent("            <dt>Raw</dt>\n");
  server.sendContent("            <dd><code>" + String(selCode.raw)  + "</code></dd></dl>\n");
  server.sendContent("        </div></div>\n");
  server.sendContent("      <div class='row'>\n");
  server.sendContent("        <div class='col-md-12'>\n");
  server.sendContent("          <div class='alert alert-warning'>Don't forget to add your passcode to the URLs below if you set one</div>\n");
  server.sendContent("      </div></div>\n");
  if (selCode.encoding == "UNKNOWN") {
  server.sendContent("      <div class='row'>\n");
  server.sendContent("        <div class='col-md-12'>\n");
  server.sendContent("          <ul class='list-unstyled'>\n");
  server.sendContent("            <li>Hostname <span class='label label-default'>JSON</span></li>\n");
  server.sendContent("            <li><pre>http://" + String(host_name) + ".local:" + String(port) + "/json?plain=[{'data':[" + String(selCode.raw) + "], 'type':'raw', 'khz':38}]</pre></li>\n");
  server.sendContent("            <li>Local IP <span class='label label-default'>JSON</span></li>\n");
  server.sendContent("            <li><pre>http://" + ipToString(WiFi.localIP()) + ":" + String(port) + "/json?plain=[{'data':[" + String(selCode.raw) + "], 'type':'raw', 'khz':38}]</pre></li>\n");
  server.sendContent("            <li>External IP <span class='label label-default'>JSON</span></li>\n");
  server.sendContent("            <li><pre>http://" + externalIP() + ":" + String(port) + "/json?plain=[{'data':[" + String(selCode.raw) + "], 'type':'raw', 'khz':38}]</pre></li></ul>\n");
  } else {
  server.sendContent("      <div class='row'>\n");
  server.sendContent("        <div class='col-md-12'>\n");
  server.sendContent("          <ul class='list-unstyled'>\n");
  server.sendContent("            <li>Hostname <span class='label label-default'>MSG</span></li>\n");
  server.sendContent("            <li><pre>http://" + String(host_name) + ".local:" + String(port) + "/msg?code=" + String(selCode.data) + ":" + String(selCode.encoding) + ":" + String(selCode.bits) + "</pre></li>\n");
  server.sendContent("            <li>Local IP <span class='label label-default'>MSG</span></li>\n");
  server.sendContent("            <li><pre>http://" + ipToString(WiFi.localIP()) + ":" + String(port) + "/msg?code=" + String(selCode.data) + ":" + String(selCode.encoding) + ":" + String(selCode.bits) + "</pre></li>\n");
  server.sendContent("            <li>External IP <span class='label label-default'>MSG</span></li>\n");
  server.sendContent("            <li><pre>http://" + externalIP() + ":" + String(port) + "/msg?code=" + selCode.data + ":" + String(selCode.encoding) + ":" + String(selCode.bits) + "</pre></li></ul>\n");
  server.sendContent("          <ul class='list-unstyled'>\n");
  server.sendContent("            <li>Hostname <span class='label label-default'>JSON</span></li>\n");
  server.sendContent("            <li><pre>http://" + String(host_name) + ".local:" + String(port) + "/json?plain=[{'data':'" + String(selCode.data) + "', 'type':'" + String(selCode.encoding) + "', 'length':" + String(selCode.bits) + "}]</pre></li>\n");
  server.sendContent("            <li>Local IP <span class='label label-default'>JSON</span></li>\n");
  server.sendContent("            <li><pre>http://" + ipToString(WiFi.localIP()) + ":" + String(port) + "/json?plain=[{'data':'" + String(selCode.data) + "', 'type':'" + String(selCode.encoding) + "', 'length':" + String(selCode.bits) + "}]</pre></li>\n");
  server.sendContent("            <li>External IP <span class='label label-default'>JSON</span></li>\n");
  server.sendContent("            <li><pre>http://" + externalIP() + ":" + String(port) + "/json?plain=[{'data':'" + String(selCode.data) + "', 'type':'" + String(selCode.encoding) + "', 'length':" + String(selCode.bits) + "}]</pre></li></ul>\n");
  }
  server.sendContent("        </div>\n");
  server.sendContent("     </div>\n");
  sendFooter();
}

//+=============================================================================
// Code to JsonObject
//
void cvrtCode(Code& codeData, decode_results *results)
{
  strncpy(codeData.data, Uint64toString(results->value, 16).c_str(), 40);
  strncpy(codeData.encoding, encoding(results).c_str(), 20);
  codeData.bits = results->bits;
  String r = "";
      for (uint16_t i = 1; i < results->rawlen; i++) {
      r += results->rawbuf[i] * RAWTICK;
      if (i < results->rawlen - 1)
        r += ",";                           // ',' not needed on last one
      //if (!(i & 1)) r += " ";
    }
  codeData.raw = r;
  if (results->decode_type != UNKNOWN) {
    strncpy(codeData.address, ("0x" + String(results->address, HEX)).c_str(), 40);
    strncpy(codeData.command, ("0x" + String(results->command, HEX)).c_str(), 40);
  } else {
    strncpy(codeData.address, "0x", 40);
    strncpy(codeData.command, "0x", 40);
  }
}

//+=============================================================================
// Dump out the decode_results structure.
//
void dumpInfo(decode_results *results) {
  if (results->overflow)
    Serial.println("WARNING: IR code too long."
                   "Edit IRrecv.h and increase RAWBUF");

  // Show Encoding standard
  Serial.print("Encoding  : ");
  Serial.print(encoding(results));
  Serial.println("");

  // Show Code & length
  Serial.print("Code      : ");
  serialPrintUint64(results->value, 16);
  Serial.print(" (");
  Serial.print(results->bits, DEC);
  Serial.println(" bits)");
}


//+=============================================================================
// Dump out the decode_results structure.
//
void dumpRaw(decode_results *results) {
  // Print Raw data
  Serial.print("Timing[");
  Serial.print(results->rawlen - 1, DEC);
  Serial.println("]: ");

  for (uint16_t i = 1;  i < results->rawlen;  i++) {
    if (i % 100 == 0)
      yield();  // Preemptive yield every 100th entry to feed the WDT.
    uint32_t x = results->rawbuf[i] * RAWTICK;
    if (!(i & 1)) {  // even
      Serial.print("-");
      if (x < 1000) Serial.print(" ");
      if (x < 100) Serial.print(" ");
      Serial.print(x, DEC);
    } else {  // odd
      Serial.print("     ");
      Serial.print("+");
      if (x < 1000) Serial.print(" ");
      if (x < 100) Serial.print(" ");
      Serial.print(x, DEC);
      if (i < results->rawlen - 1)
        Serial.print(", ");  // ',' not needed for last one
    }
    if (!(i % 8)) Serial.println("");
  }
  Serial.println("");  // Newline
}


//+=============================================================================
// Dump out the decode_results structure.
//
void dumpCode(decode_results *results) {
  // Start declaration
  Serial.print("uint16_t  ");              // variable type
  Serial.print("rawData[");                // array name
  Serial.print(results->rawlen - 1, DEC);  // array size
  Serial.print("] = {");                   // Start declaration

  // Dump data
  for (uint16_t i = 1; i < results->rawlen; i++) {
    Serial.print(results->rawbuf[i] * RAWTICK, DEC);
    if (i < results->rawlen - 1)
      Serial.print(",");  // ',' not needed on last one
    if (!(i & 1)) Serial.print(" ");
  }

  // End declaration
  Serial.print("};");  //

  // Comment
  Serial.print("  // ");
  Serial.print(encoding(results));
  Serial.print(" ");
  serialPrintUint64(results->value, 16);

  // Newline
  Serial.println("");

  // Now dump "known" codes
  if (results->decode_type != UNKNOWN) {
    // Some protocols have an address &/or command.
    // NOTE: It will ignore the atypical case when a message has been decoded
    // but the address & the command are both 0.
    if (results->address > 0 || results->command > 0) {
      Serial.print("uint32_t  address = 0x");
      Serial.print(results->address, HEX);
      Serial.println(";");
      Serial.print("uint32_t  command = 0x");
      Serial.print(results->command, HEX);
      Serial.println(";");
    }

    // All protocols have data
    Serial.print("uint64_t  data = 0x");
    serialPrintUint64(results->value, 16);
    Serial.println(";");
  }
}


//+=============================================================================
// Binary value to hex
//
String bin2hex(const uint8_t* bin, const int length) {
  String hex = "";
  
  for (int i = 0; i < length; i++) {
    if (bin[i] < 16) {
      hex += "0";
    }
    hex += String(bin[i], HEX);
  }
  
  return hex;
}


//+=============================================================================
// Convert string to hex, borrowed from ESPBasic
//
unsigned long HexToLongInt(String h)
{
  // this function replace the strtol as this function is not able to handle hex numbers greather than 7fffffff
  // I'll take char by char converting from hex to char then shifting 4 bits at the time
  int i;
  unsigned long tmp = 0;
  unsigned char c;
  int s = 0;
  h.toUpperCase();
  for (i = h.length() - 1; i >= 0 ; i--)
  {
    // take the char starting from the right
    c = h[i];
    // convert from hex to int
    c = c - '0';
    if (c > 9)
      c = c - 7;
    // add and shift of 4 bits per each char
    tmp += c << s;
    s += 4;
  }
  return tmp;
}


//+=============================================================================
// Send IR codes to variety of sources
//
void irblast(String type, String dataStr, unsigned int len, int rdelay, int pulse, int pdelay, int repeat, long address, IRsend irsend) {
  Serial.println("Blasting off");
  type.toLowerCase();
  unsigned long data = HexToLongInt(dataStr);
  holdReceive = true;
  Serial.println("Blocking incoming IR signals");
  // Repeat Loop
  for (int r = 0; r < repeat; r++) {
    // Pulse Loop
    for (int p = 0; p < pulse; p++) {
      Serial.print(data, HEX);
      Serial.print(":");
      Serial.print(type);
      Serial.print(":");
      Serial.println(len);
      if (type == "nec") {
        irsend.sendNEC(data, len);
      } else if (type == "sony") {
        irsend.sendSony(data, len);
      } else if (type == "coolix") {
        irsend.sendCOOLIX(data, len);
      } else if (type == "whynter") {
        irsend.sendWhynter(data, len);
      } else if (type == "panasonic") {
        Serial.println(address);
        irsend.sendPanasonic(address, data);
      } else if (type == "jvc") {
        irsend.sendJVC(data, len, 0);
      } else if (type == "samsung") {
        irsend.sendSAMSUNG(data, len);
      } else if (type == "sharpRaw") {
        irsend.sendSharpRaw(data, len);
      } else if (type == "dish") {
        irsend.sendDISH(data, len);
      } else if (type == "rc5") {
        irsend.sendRC5(data, len);
      } else if (type == "rc6") {
        irsend.sendRC6(data, len);
      } else if (type == "denon") {
        irsend.sendDenon(data, len);
      } else if (type == "lg") {
        irsend.sendLG(data, len);
      } else if (type == "sharp") {
        irsend.sendSharpRaw(data, len);
      } else if (type == "rcmm") {
        irsend.sendRCMM(data, len);
      } else if (type == "roomba") {
        roomba_send(atoi(dataStr.c_str()), pulse, pdelay, irsend);
      }
      if (p + 1 < pdelay) delay(pdelay);
    }
    if (r + 1 < rdelay) delay(rdelay);
  }

  Serial.println("Transmission complete");

  copyCode(last_send_4, last_send_5);
  copyCode(last_send_3, last_send_4);
  copyCode(last_send_2, last_send_3);
  copyCode(last_send, last_send_2);

  strncpy(last_send.data, dataStr.c_str(), 40);
  last_send.bits = len;
  strncpy(last_send.encoding, type.c_str(), 20);
  strncpy(last_send.address, String(address).c_str(), 20);
  strncpy(last_send.timestamp, String(timeClient.getFormattedTime()).c_str(), 40);
  last_send.valid = true;
  
  resetReceive();
}


void rawblast(JsonArray &raw, int khz, int rdelay, int pulse, int pdelay, int repeat, IRsend irsend) {
  Serial.println("Raw transmit");
  holdReceive = true;
  Serial.println("Blocking incoming IR signals");
  // Repeat Loop
  for (int r = 0; r < repeat; r++) {
    // Pulse Loop
    for (int p = 0; p < pulse; p++) {
      Serial.println("Sending code");
      irsend.enableIROut(khz);
      for (unsigned int i = 0; i < raw.size(); i++) {
        int val = raw[i];
        if (i & 1) irsend.space(std::max(0, val));
        else       irsend.mark(val);
      }
      irsend.space(0);
      if (p + 1 < pdelay) delay(pdelay);
    }
    if (r + 1 < rdelay) delay(rdelay);
  }

  Serial.println("Transmission complete");

  copyCode(last_send_4, last_send_5);
  copyCode(last_send_3, last_send_4);
  copyCode(last_send_2, last_send_3);
  copyCode(last_send, last_send_2);

  strncpy(last_send.data, "", 40);
  last_send.bits = raw.size();
  strncpy(last_send.encoding, "RAW", 20);
  strncpy(last_send.address, "0", 40);
  strncpy(last_send.timestamp, String(timeClient.getFormattedTime()).c_str(), 40);
  last_send.valid = true;

  resetReceive();
}


void roomba_send(int code, int pulse, int pdelay, IRsend irsend)
{
  Serial.print("Sending Roomba code ");
  Serial.println(code);
  holdReceive = true;
  Serial.println("Blocking incoming IR signals");

  int length = 8;
  uint16_t raw[length * 2];
  unsigned int one_pulse = 3000;
  unsigned int one_break = 1000;
  unsigned int zero_pulse = one_break;
  unsigned int zero_break = one_pulse;
  uint16_t len = 15;
  uint16_t hz = 38;

  int arrayposition = 0;
  for (int counter = length - 1; counter >= 0; --counter) {
    if (code & (1 << counter)) {
      raw[arrayposition] = one_pulse;
      raw[arrayposition + 1] = one_break;
    }
    else {
      raw[arrayposition] = zero_pulse;
      raw[arrayposition + 1] = zero_break;
    }
    arrayposition = arrayposition + 2;
  }
  for (int i = 0; i < pulse; i++) {
    irsend.sendRaw(raw, len, hz);
    delay(pdelay);
  }

  resetReceive();
}

void copyCode (Code& c1, Code& c2) {
  strncpy(c2.data, c1.data, 40);
  strncpy(c2.encoding, c1.encoding, 20);
  strncpy(c2.timestamp, c1.timestamp, 40);
  strncpy(c2.address, c1.address, 40);
  strncpy(c2.command, c1.command, 40);
  c2.bits = c1.bits;
  c2.raw = c1.raw;
  c2.valid = c1.valid;
}

void loop() {
  server.handleClient();
  decode_results  results;                                        // Somewhere to store the results
  if (getTime || strlen(user_id) != 0) timeClient.update();                               // Update the time

  if (irrecv.decode(&results) && !holdReceive) {                  // Grab an IR code
    Serial.println("Signal received:");
    fullCode(&results);                                           // Print the singleline value
    dumpCode(&results);                                           // Output the results as source code
    copyCode(last_recv_4, last_recv_5);                           // Pass
    copyCode(last_recv_3, last_recv_4);                           // Pass
    copyCode(last_recv_2, last_recv_3);                           // Pass
    copyCode(last_recv, last_recv_2);                             // Pass
    cvrtCode(last_recv, &results);                                // Store the results
    strncpy(last_recv.timestamp, String(timeClient.getFormattedTime()).c_str(), 40);  // Set the new update time
    last_recv.valid = true;
    Serial.println("");                                           // Blank line between entries
    irrecv.resume();                                              // Prepare for the next value
    digitalWrite(ledpin, LOW);                                    // Turn on the LED for 0.5 seconds
    ticker.attach(0.5, disableLed);
  }
  delay(200);
}
