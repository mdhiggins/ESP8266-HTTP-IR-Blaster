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
#include <ArduinoOTA.h>
#include "sha256.h"

#include <Ticker.h>                                           // For LED status
#include <EasyNTPClient.h>

// User settings are below here

const bool getExternalIP = false;                              // Set to false to disable querying external IP

const bool getTime = true;                                    // Set to false to disable querying for the time
const int timeOffset = 0;                                     // Timezone offset in seconds

const bool enableMDNSServices = true;                         // Use mDNS services, must be enabled for ArduinoOTA

const unsigned int captureBufSize = 150;                      // Size of the IR capture buffer.

// WEMOS users may need to adjust pins for compatability
const int pinr1 = 14;                                         // Receiving pin
const int pins1 = 4;                                          // Transmitting preset 1
const int pins2 = 5;                                          // Transmitting preset 2
const int pins3 = 12;                                         // Transmitting preset 3
const int pins4 = 13;                                         // Transmitting preset 4
const int configpin = 10;                                     // Reset Pin

// User settings are above here
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

ESP8266WebServer *server = NULL;
Ticker ticker;

bool shouldSaveConfig = false;                                // Flag for saving data
bool holdReceive = false;                                     // Flag to prevent IR receiving while transmitting

IRrecv irrecv(pinr1, captureBufSize);
IRsend irsend1(pins1);
IRsend irsend2(pins2);
IRsend irsend3(pins3);
IRsend irsend4(pins4);

const unsigned long resetfrequency = 259200000;                // 72 hours in milliseconds
const char* poolServerName = "europe.pool.ntp.org";

WiFiUDP ntpUDP;
EasyNTPClient timeClient(ntpUDP, poolServerName, timeOffset);

char _ip[16] = "";

unsigned long lastupdate = 0;

bool authError = false;
time_t timeAuthError = 0;
bool externalIPError = false;
bool userIDError = false;
bool ntpError = false;

class Code {
  public:
    char encoding[14] = "";
    char address[20] = "";
    char command[40] = "";
    char data[40] = ""; 
    String raw = "";
    int bits = 0;
    time_t timestamp = 0;
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


// Smartthings Hub Information
IPAddress hubIP(10.10.10.10);                                   // smartthings hub ip
const unsigned int hubPort = 39500;                             // smartthings hub port
bool sendToSmartThings = false;

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
// Valid user_id formatting
//
bool validUID(char* user_id) {
  if (!String(user_id).startsWith("amzn1.account.")) {
      Serial.println("Warning, user_id appears to be in the wrong format, security check will most likely fail. Should start with amzn1.account.***");
      return false;
    }
    return true;
}


//+=============================================================================
// Valid EPOCH time retrieval
//
bool validEPOCH(time_t timenow) {
  if (timenow < 922838400) {
    Serial.println("Epoch time from timeServer is unexpectedly old, probably failed connection to the time server. Check your network settings");
    Serial.println(timenow);
    return false;
  }
  return true;
}

//+=============================================================================
// EPOCH time to String
//
String epochToString(time_t timenow) {
  unsigned long hours = (timenow % 86400L) / 3600;
  String hourStr = hours < 10 ? "0" + String(hours) : String(hours);

  unsigned long minutes = (timenow % 3600) / 60;
  String minuteStr = minutes < 10 ? "0" + String(minutes) : String(minutes);

  unsigned long seconds = (timenow % 60);
  String secondStr = seconds < 10 ? "0" + String(seconds) : String(seconds);
  return hourStr + ":" + minuteStr + ":" + secondStr;
}


//+=============================================================================
// Valid command request using HMAC
//
bool validateHMAC(String epid, String mid, String timestamp, String signature) {
    userIDError = false;
    authError = false;
    ntpError = false;
    timeAuthError = 0;

    userIDError = !(validUID(user_id));

    time_t timethen = timestamp.toInt();
    time_t timenow = timeClient.getUnixTime() - timeOffset;
    time_t timediff = abs(timethen - timenow);
    if (timediff > 30) {
      Serial.println("Failed security check, signature is too old");
      Serial.print("Server: ");
      Serial.println(timethen);
      Serial.print("Local: ");
      Serial.println(timenow);
      Serial.print("MID: ");
      Serial.println(mid);
      timeAuthError = timediff;
      validEPOCH(timenow);
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
      authError = true;
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
  HTTPClient http;
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

  if (strlen(_ip) > 0) {
    unsigned long delta = millis() - lastupdate;
    if (delta > resetfrequency || lastupdate == 0) {
      Serial.println("Reseting cached external IP address");
      strncpy(_ip, "", 16); // Reset the cached external IP every 72 hours
    } else {
      return String(_ip); // Return the cached external IP
    }
  }

  HTTPClient http;
  externalIPError = false;
  unsigned long start = millis();
  http.setTimeout(5000);
  http.begin(serverName, 8245);
  int httpCode = http.GET();

  if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    int pos_start = payload.indexOf("IP Address") + 12; // add 10 for "IP Address" and 2 for ":" + "space"
    int pos_end = payload.indexOf("</body>", pos_start); // add nothing
    strncpy(_ip, payload.substring(pos_start, pos_end).c_str(), 16);
    Serial.print(F("External IP: "));
    Serial.println(_ip);
    lastupdate = millis();
  } else {
    Serial.println("Error retrieving external IP");
    Serial.print("HTTP Code: ");
    Serial.println(httpCode);
    Serial.println(http.errorToString(httpCode));
    externalIPError = true;
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
// Gets called when device loses connection to the accesspoint
//
void lostWifiCallback (const WiFiEventStationModeDisconnected& evt) {
  Serial.println("Lost Wifi");
  // reset and try again, or maybe put it to deep sleep
  ESP.reset();
  delay(1000);
}


//+=============================================================================
// First setup of the Wifi.
// If return true, the Wifi is well connected.
// Should not return false if Wifi cannot be connected, it will loop
//
bool setupWifi(bool resetConf) {
  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.5, tick);

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

  // Reset device if on config portal for greater than 3 minutes
  wifiManager.setConfigPortalTimeout(180);

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
    Serial.println("Failed to connect and hit timeout");
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

  if (server != NULL) {
    delete server;
  }
  server = new ESP8266WebServer(port);

  // Reset device if lost wifi Connection
  WiFi.onStationModeDisconnected(&lostWifiCallback);

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
    Serial.println("Writing config file");
    json.printTo(configFile);
    configFile.close();
    jsonBuffer.clear();
    Serial.println("Config written successfully");
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

  // set led pin as output
  pinMode(ledpin, OUTPUT);

  Serial.println("");
  Serial.println("ESP8266 IR Controller");
  pinMode(configpin, INPUT_PULLUP);
  Serial.print("Config pin GPIO");
  Serial.print(configpin);
  Serial.print(" set to: ");
  Serial.println(digitalRead(configpin));
  if (!setupWifi(digitalRead(configpin) == LOW))
    return;

  Serial.println("WiFi configuration complete");

  if (strlen(host_name) > 0) {
    WiFi.hostname(host_name);
  } else {
    WiFi.hostname().toCharArray(host_name, 20);
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  wifi_set_sleep_type(LIGHT_SLEEP_T);
  digitalWrite(ledpin, LOW);
  // Turn off the led in 2s
  ticker.attach(2, disableLed);

  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP().toString());
  Serial.println("URL to send commands: http://" + String(host_name) + ".local:" + port_str);

  if (enableMDNSServices) {
    // Configure OTA Update
    ArduinoOTA.setPort(8266);
    ArduinoOTA.setHostname(host_name);
    ArduinoOTA.onStart([]() {
      Serial.println("Start");
    });
    ArduinoOTA.onEnd([]() {
      Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();
    Serial.println("ArduinoOTA started");

    // Configure mDNS
    MDNS.addService("http", "tcp", port); // Announce the ESP as an HTTP service
    Serial.println("MDNS http service added. Hostname is set to " + String(host_name) + ".local:" + String(port));
  }

  // Configure the server
  server->on("/json", []() { // JSON handler for more complicated IR blaster routines
    
    Serial.println("Connection received - JSON");
    int simple = 0;
    if (server->hasArg("simple")) simple = server->arg("simple").toInt();
    String signature = server->arg("auth");
    String epid = server->arg("epid");
    String mid = server->arg("mid");
    String timestamp = server->arg("time");
    String jsonBtn = server->arg("btn");
    String fromListPage = server->arg("fromListPage");
    int out = (server->hasArg("out")) ? server->arg("out").toInt() : 1;
    String jsonString;
    String jsonLookupError;
    
    if (server->hasArg("btn")) {    //JSON command received with button as argument, retreive button IR command from stored JSON
      String btn = server->arg("btn");
      DynamicJsonBuffer jsonBtnBuffer;
      JsonArray& rootbtn = jsonBtnBuffer.parseArray(btn);
      rootbtn.prettyPrintTo(Serial);      
      if (SPIFFS.begin()) {
        Serial.println("mounted file system");
        if (SPIFFS.exists("/codes.json")) {
          //file exists, reading and loading
          Serial.println("reading codes file");
          File configFileRead = SPIFFS.open("/codes.json", "r");
          if (configFileRead) {
            Serial.println("opened codes file");
            DynamicJsonBuffer jsonLoadBuffer;
            JsonObject& jsonRead = jsonLoadBuffer.parseObject(configFileRead);
            configFileRead.close();
            if (jsonRead.success()) {
              //for (auto value : arr) {
              //    Serial.println(value.as<char*>());
              //}
              for (int x = 0; x < rootbtn.size(); x++) {
                String currentBtn;
                String jsonStringtmp;
                rootbtn[x].printTo(currentBtn);
                currentBtn.toUpperCase();
                Serial.println("parsed current json, looking for " + currentBtn);
                jsonRead[currentBtn].printTo(jsonStringtmp);
                if (rootbtn.size() > 1 && x < rootbtn.size()-1) {
                  if (jsonStringtmp != "") { //if the string is empty we havent found a match so record the button name against the error string
                    jsonString = jsonString + jsonStringtmp + ",";
                  } else {
                    jsonLookupError = jsonLookupError + currentBtn + ", ";
                  }
                } else {
                  if (jsonStringtmp != "") {
                    jsonString = jsonString + jsonStringtmp;                      
                  } else {
                    jsonLookupError = jsonLookupError + currentBtn;
                  }
                }
              }
              if (jsonString != "") jsonString = "[" + jsonString + "]";
              if (jsonLookupError != "") jsonLookupError = "Some or all matches not found (" + jsonLookupError +"), check the Serial output";
            } else {
              jsonLookupError = "An error occured looking up the codes";
              Serial.println("failed to load json config");        
            }
            jsonLoadBuffer.clear();
          }
        }
      } else {
        jsonLookupError = "An error occured looking up the codes";
        Serial.println("failed to mount FS");
      }
      jsonBtnBuffer.clear();  
    } else {                          //Set jsonString to the plain argument
      jsonString = server->arg("plain");
    }
     
    DynamicJsonBuffer jsonBuffer;
    JsonArray& root = jsonBuffer.parseArray(jsonString);
    root.prettyPrintTo(Serial);
    Serial.println("");
    
    if (!root.success()) {
      Serial.println("JSON parsing failed");
      if (simple) {
        server->send(400, "text/plain", "JSON parsing failed");
      } else {
        if (jsonLookupError != "") {
          if (server->hasArg("fromListPage")) {
            listStoredCodes(jsonLookupError, "Error", 3, 400); // 400
          } else{
            sendHomePage(jsonLookupError, "Error", 3, 400); // 400            
          }
        } else {
          if (server->hasArg("fromListPage")) {
            listStoredCodes("JSON parsing failed", "Error", 3, 400); // 400            
          } else {
            sendHomePage("JSON parsing failed", "Error", 3, 400); // 400             
          }
        }
      }
      jsonBuffer.clear();

    } else if (strlen(passcode) != 0 && server->arg("pass") != passcode) {
      Serial.println("Unauthorized access");
      if (simple) {
        server->send(401, "text/plain", "Unauthorized, invalid passcode");
      } else {
        sendHomePage("Invalid passcode", "Unauthorized", 3, 401); // 401
      }
      jsonBuffer.clear();
    } else if (strlen(user_id) != 0 && !validateHMAC(epid, mid, timestamp, signature)) {
      server->send(401, "text/plain", "Unauthorized, HMAC security authentication failed");
    } else {
      digitalWrite(ledpin, LOW);
      ticker.attach(0.5, disableLed);

      // Handle device state limitations for the global JSON command request
      if (server->hasArg("device")) {
        String device = server->arg("device");
        Serial.println("Device name detected " + device);
        int state = (server->hasArg("state")) ? server->arg("state").toInt() : 0;
        if (deviceState.containsKey(device)) {
          Serial.println("Contains the key!");
          Serial.println(state);
          int currentState = deviceState[device];
          Serial.println(currentState);
          if (state == currentState) {
            if (simple) {
              server->send(200, "text/html", "Not sending command to " + device + ", already in state " + state);
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
        server->send(200, "text/html", "Success, code sent");
      }

      String message = "Code sent";

      for (int x = 0; x < root.size(); x++) {
        String type = root[x]["type"];
        String ip = root[x]["ip"];
        int rdelay = root[x]["rdelay"];
        int pulse = root[x]["pulse"];
        int pdelay = root[x]["pdelay"];
        int repeat = root[x]["repeat"];
        int xout = root[x]["out"];
        if (xout == 0) {
          xout = out;
        }
        int duty = root[x]["duty"];

        if (pulse <= 0) pulse = 1; // Make sure pulse isn't 0
        if (repeat <= 0) repeat = 1; // Make sure repeat isn't 0
        if (pdelay <= 0) pdelay = 100; // Default pdelay
        if (rdelay <= 0) rdelay = 1000; // Default rdelay
        if (duty <= 0) duty = 50; // Default duty

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
          rawblast(raw, khz, rdelay, pulse, pdelay, repeat, pickIRsend(xout),duty);
        } else if (type == "roku") {
          String data = root[x]["data"];
          rokuCommand(ip, data);
        } else if (type != "") {
          String data = root[x]["data"];
          String addressString = root[x]["address"];
          long address = strtoul(addressString.c_str(), 0, 0);
          int len = root[x]["length"];
          irblast(type, data, len, rdelay, pulse, pdelay, repeat, address, pickIRsend(xout));
        }
      }

      if (!simple) {
        Serial.println("Sending home page");
        if (jsonLookupError == "") {
          if (server->hasArg("fromListPage")) {
            listStoredCodes(message, "Success", 1); // 200           
          } else {
            sendHomePage(message, "Success", 1); // 200           
          }
        } else if (server->hasArg("fromListPage")) {
          listStoredCodes("", jsonLookupError, 2); // 200   
        } else {       
          sendHomePage("", jsonLookupError, 2); // 200
        }
      }
      jsonBuffer.clear();
    }
  });

  // Setup simple msg server to mirror version 1.0 functionality
  server->on("/msg", []() {
    Serial.println("Connection received - MSG");

    int simple = 0;
    if (server->hasArg("simple")) simple = server->arg("simple").toInt();
    String signature = server->arg("auth");
    String epid = server->arg("epid");
    String mid = server->arg("mid");
    String timestamp = server->arg("time");

    if (strlen(passcode) != 0 && server->arg("pass") != passcode) {
      Serial.println("Unauthorized access");
      if (simple) {
        server->send(401, "text/plain", "Unauthorized, invalid passcode");
      } else {
        sendHomePage("Invalid passcode", "Unauthorized", 3, 401); // 401
      }
    } else if (strlen(user_id) != 0 && !validateHMAC(epid, mid, timestamp, signature)) {
      server->send(401, "text/plain", "Unauthorized, HMAC security authentication");
    } else {
      digitalWrite(ledpin, LOW);
      ticker.attach(0.5, disableLed);
      String type = server->arg("type");
      String data = server->arg("data");
      String ip = server->arg("ip");

      // Handle device state limitations
      if (server->hasArg("device")) {
        String device = server->arg("device");
        Serial.println("Device name detected " + device);
        int state = (server->hasArg("state")) ? server->arg("state").toInt() : 0;
        if (deviceState.containsKey(device)) {
          Serial.println("Contains the key!");
          Serial.println(state);
          int currentState = deviceState[device];
          Serial.println(currentState);
          if (state == currentState) {
            if (simple) {
              server->send(200, "text/html", "Not sending command to " + device + ", already in state " + state);
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

      int len = server->arg("length").toInt();
      long address = 0;
      if (server->hasArg("address")) {
        String addressString = server->arg("address");
        address = strtoul(addressString.c_str(), 0, 0);
      }

      int rdelay = (server->hasArg("rdelay")) ? server->arg("rdelay").toInt() : 1000;
      int pulse = (server->hasArg("pulse")) ? server->arg("pulse").toInt() : 1;
      int pdelay = (server->hasArg("pdelay")) ? server->arg("pdelay").toInt() : 100;
      int repeat = (server->hasArg("repeat")) ? server->arg("repeat").toInt() : 1;
      int out = (server->hasArg("out")) ? server->arg("out").toInt() : 1;
      if (server->hasArg("code")) {
        String code = server->arg("code");
        char separator = ':';
        data = getValue(code, separator, 0);
        type = getValue(code, separator, 1);
        len = getValue(code, separator, 2).toInt();
      }

      if (simple) {
        server->send(200, "text/html", "Success, code sent");
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

  server->on("/received", []() {
    Serial.println("Connection received to received detail page");
    int id = server->arg("id").toInt();
    String output;
    if (id == 1 && last_recv.valid) {
      sendCodePage(last_recv, 1);
    } else if (id == 2 && last_recv_2.valid) {
      sendCodePage(last_recv_2, 2);
    } else if (id == 3 && last_recv_3.valid) {
      sendCodePage(last_recv_3, 3);
    } else if (id == 4 && last_recv_4.valid) {
      sendCodePage(last_recv_4, 4);
    } else if (id == 5 && last_recv_5.valid) {
      sendCodePage(last_recv_5, 5);
    } else {
      sendHomePage("Code does not exist", "Alert", 2, 404); // 404
    }
  });

  // Loads the store code page with the selected code, and if btn parameter send calls the routine to store the code to JSON file
  server->on("/store", []() {
    Serial.println("Connection received to store code page");
    int id = server->arg("id").toInt();
    String btn = server->arg("name_input");
    btn.toUpperCase();
    String pulse = server->arg("pulse");
    String pulse_delay = server->arg("pulse_delay");    
    String repeat = server->arg("repeat");
    String repeat_delay = server->arg("repeat_delay");          
    String output;

    if (btn != "") {
      if (id == 1 && last_recv.valid) {
        storeReceivedCode(last_recv, btn, pulse, pulse_delay, repeat, repeat_delay);
      } else if (id == 2 && last_recv_2.valid) {
        storeReceivedCode(last_recv_2, btn, pulse, pulse_delay, repeat, repeat_delay);
      } else if (id == 3 && last_recv_3.valid) {
        storeReceivedCode(last_recv_3, btn, pulse, pulse_delay, repeat, repeat_delay);
      } else if (id == 4 && last_recv_4.valid) {
        storeReceivedCode(last_recv_4, btn, pulse, pulse_delay, repeat, repeat_delay);
      } else if (id == 5 && last_recv_5.valid) {
        storeReceivedCode(last_recv_5, btn, pulse, pulse_delay, repeat, repeat_delay);     
      } else {
        sendHomePage("Code does not exist", "Alert", 2, 404); // 404
      }
    } else if (id == 1 && last_recv.valid) {
      storeCodePage(last_recv, 1);
    } else if (id == 2 && last_recv_2.valid) {
      storeCodePage(last_recv_2, 2);
    } else if (id == 3 && last_recv_3.valid) {
      storeCodePage(last_recv_3, 3);
    } else if (id == 4 && last_recv_4.valid) {
      storeCodePage(last_recv_4, 4);
    } else if (id == 5 && last_recv_5.valid) {
      storeCodePage(last_recv_5, 5);      
    } else {
      sendHomePage("Code does not exist", "Alert", 2, 404); // 404
    }

  });

  // Delete the JSON codes contents
  server->on("/clear", []() {
    Serial.println("Connection received - clear JSON");
    DeleteJSONFile(); // 200
    sendHomePage("JSON Cleared", "Alert", 2, 404); // 404    
  });

  // list the JSON codes contents
  server->on("/list", []() {
    String item = server->arg("item");
    if (server->hasArg("item")) {
      Serial.println("Connection received - delete item " + item);
      if (DeleteJSONitem(item)) listStoredCodes("Item removed", "Alert", 2, 404);    
    } else {
      listStoredCodes(); // 200
      //sendHomePage("JSON Cleared", "Alert", 2, 404); // 404         
    } 
  });

  server->on("/", []() {
    Serial.println("Connection received");
    sendHomePage(); // 200
  });

  server->begin();
  Serial.println("HTTP Server started on port " + String(port));

  externalIP();

  if (strlen(user_id) > 0) {
    userIDError = !validUID(user_id);
    if (!userIDError) {
      Serial.println("No errors detected with security configuration");
    }

    // Validation check time
    time_t timenow = timeClient.getUnixTime() - timeOffset;
    bool validEpoch = validEPOCH(timenow);
    if (validEpoch) {
      Serial.println("EPOCH time obtained for security checks");
    } else {
      Serial.println("Invalid EPOCH time, security checks may fail if unable to sync with NTP server");
    }
  }

  irsend1.begin();
  irsend2.begin();
  irsend3.begin();
  irsend4.begin();
  irrecv.enableIRIn();
  Serial.println("Ready to send and receive IR signals");
}


//+=============================================================================
// Send command to local roku
//
int rokuCommand(String ip, String data) {
  String url = "http://" + ip + ":8060/" + data;
  HTTPClient http;
  http.begin(url);
  Serial.println(url);
  Serial.println("Sending roku command");

  copyCode(last_send_4, last_send_5);
  copyCode(last_send_3, last_send_4);
  copyCode(last_send_2, last_send_3);
  copyCode(last_send, last_send_2);

  strncpy(last_send.data, data.c_str(), 40);
  last_send.bits = 1;
  strncpy(last_send.encoding, "roku", 14);
  strncpy(last_send.address, ip.c_str(), 20);
  last_send.timestamp = timeClient.getUnixTime();
  last_send.valid = true;

  int output = http.POST("");
  http.end();
  return output;
}


//+=============================================================================
// Send received command to webCoRe hub
//
int webCoreCommand (decode_results *results) {

  if (results->repeat == false && results->overflow == false){
    String value = uint64ToString(results->value, 16) + "|" + String(encoding(results)) + "|" + String(results->bits, DEC);    
    String message;
    message = String(host_name) + "|" + String(value);    
    Serial.println("Sending to Smarthings... " + message);
    WiFiClient st_client;
        if (st_client.connect(hubIP, hubPort)){
          st_client.println(F("POST / HTTP/1.1"));
          st_client.print(F("HOST: "));
          st_client.print(hubIP);
          st_client.print(F(":"));
          st_client.println(hubPort);
          st_client.println(F("CONTENT-TYPE: text/plain"));
          st_client.print(F("CONTENT-LENGTH: "));
          st_client.println(message.length());
          st_client.println();
          st_client.println(message);
          //while (st_client.connected()) {
          //  char c = st_client.read(); //gets byte from ethernet buffer
          //  Serial.print(c);
          //}
          delay(1);
          st_client.stop();
      }
  }
}


//+=============================================================================
// Split string by character
//
String getValue(String data, char separator, int index) {
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
    case GREE:         output = "GREE";               break;
  }
  return output;
}

//+=============================================================================
// Code to string
//
void fullCode (decode_results *results) {
  Serial.print("One line: ");
  serialPrintUint64(results->value, 16);
  Serial.print(":");
  Serial.print(encoding(results));
  Serial.print(":");
  Serial.print(results->bits, DEC);
  if (results->repeat) Serial.print(" (Repeat)");
  Serial.println("");
  if (results->overflow)
    Serial.println("WARNING: IR code too long. "
                   "Edit IRController.ino and increase captureBufSize");
}


//+=============================================================================
// Deletes the codes JSON file
//
void DeleteJSONFile () {
  if (SPIFFS.begin()) {
    Serial.println("  mounted file system");
    SPIFFS.remove("/codes.json");
  } else {
    Serial.println("  failed to mount FS");
  }
}

//+=============================================================================
// Stores the selected code to JSON file
//
void storeReceivedCode (Code code, String btn, String pulse, String pulse_delay, String repeat, String repeat_delay) {
  //generate the nested json from the code
  DynamicJsonBuffer jsonWriteBuffer;  
  JsonObject& jsonadd = jsonWriteBuffer.createObject();
  JsonObject& jsonbtn = jsonadd.createNestedObject(btn);          
  if (String(code.encoding) == "UNKNOWN") {
    jsonbtn["data"] = RawJson("[" + code.raw + "]"); //"[" + code.raw + "]";
    jsonbtn["type"] = "raw";
    jsonbtn["khz"] = 38;
  } else if (String(code.encoding) == "PANASONIC") {
    jsonbtn["type"] = code.encoding;
    jsonbtn["address"] = code.address;
    jsonbtn["data"] = code.data;
    jsonbtn["length"] = code.bits;
  } else {
    jsonbtn["type"] = code.encoding;
    jsonbtn["data"] = code.data;
    jsonbtn["length"] = code.bits;
  }
  if (pulse != "") jsonbtn["pulse"] = pulse;
  if (pulse_delay != "") jsonbtn["pdelay"] =  pulse_delay;
  if (repeat != "") jsonbtn["repeat"] =  repeat;
  if (repeat_delay != "") jsonbtn["rdelay"] =  repeat_delay;

  if (SPIFFS.begin()) {     //read current codes file
    Serial.println("  mounted file system");
    if (SPIFFS.exists("/codes.json")) {
      //file exists, reading and loading
      Serial.println("    reading code file");
      File codeFile = SPIFFS.open("/codes.json", "r+");
      if (codeFile) {
        Serial.println("    opened codes file");
        DynamicJsonBuffer jsonReadBuffer;
        JsonObject& json = jsonReadBuffer.parseObject(codeFile); //buf.get());
        codeFile.close();
        if (json.success()) {
          Serial.println("    parsed current json");
          merge(json, jsonadd); // add new code to existing codes
          json.prettyPrintTo(Serial);
          if (saveJSON(json))  sendHomePage("Code stored to " + btn, "Alert", 2, 404);
        }
        jsonReadBuffer.clear();  
      }
    } else {
      if (saveJSON(jsonadd)) sendHomePage("Code stored to " + btn, "Alert", 2, 404); // 404 Need to check it has been written before sending this (get saveJSON to return status);  //no existing code file so just store new code to new file
    }
  }
  jsonWriteBuffer.clear(); 

}

//+=============================================================================
// Delete the selected code
//
bool DeleteJSONitem(String item){
  if (SPIFFS.begin()) {     //read current codes file
    Serial.println("  mounted file system");
    if (SPIFFS.exists("/codes.json")) {
      //file exists, reading and loading
      Serial.println("    reading code file");
      File codeFile = SPIFFS.open("/codes.json", "r+");
      if (codeFile) {
        Serial.println("    opened codes file");
        DynamicJsonBuffer jsonReadBuffer;
        JsonObject& json = jsonReadBuffer.parseObject(codeFile); //buf.get());
        codeFile.close();
        if (json.success()) {
          Serial.println("    parsed current json, deleteing " + item);
          json.remove(item);
          json.prettyPrintTo(Serial);
          if (saveJSON(json)) return true;
        }
        jsonReadBuffer.clear();  
      }
    } else {
      return false;      
    }
  }
}

//+=============================================================================
// Updates the codes JSON file with added or deleted codes
//
bool saveJSON (JsonObject& json){
  if (SPIFFS.begin()) {     //read current codes file
    Serial.println("  mounted file system");
    File codeFileWrite = SPIFFS.open("/codes.json", "w+"); //writes the updated json to code file
    if (codeFileWrite) {
      Serial.println("    opened codes file to write");
      Serial.println("    Writing codes file");
      json.printTo(codeFileWrite);
      codeFileWrite.close();      
      Serial.println("    Codes written successfully");
      return true;
    }  
  }
}

//+=============================================================================
// Merges new and existing codes into final json for save
//
void merge(JsonObject& dest, const JsonObject& src) {
   for (auto kvp : src) {
     dest[kvp.key] = kvp.value;
   }
}


//+=============================================================================
// Send header HTML
//
void sendHeader() {
  sendHeader(200);
}
void sendHeader(int httpcode) {
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(httpcode, "text/html; charset=utf-8", "");
  server->sendContent("<!DOCTYPE html PUBLIC '-//W3C//DTD XHTML 1.0 Strict//EN' 'http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd'>\n");
  server->sendContent("<html xmlns='http://www.w3.org/1999/xhtml' xml:lang='en'>\n");
  server->sendContent("  <head>\n");
  server->sendContent("    <meta name='viewport' content='width=device-width, initial-scale=.75' />\n");
  server->sendContent("    <link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/css/bootstrap.min.css' />\n");
  server->sendContent("    <style>@media (max-width: 991px) {.nav-pills>li {float: none; margin-left: 0; margin-top: 5px; text-align: center;}}</style>\n");
  server->sendContent("    <title>ESP8266 IR Controller (" + String(host_name) + ")</title>\n");
  server->sendContent("  </head>\n");
  server->sendContent("  <body>\n");
  server->sendContent("    <div class='container'>\n");
  server->sendContent("      <h1><a href='https://github.com/mdhiggins/ESP8266-HTTP-IR-Blaster'>ESP8266 IR Controller</a></h1>\n");
  server->sendContent("      <div class='row'>\n");
  server->sendContent("        <div class='col-md-12'>\n");
  server->sendContent("          <ul class='nav nav-pills'>\n");
  server->sendContent("            <li class='active'>\n");
  server->sendContent("              <a href='http://" + String(host_name) + ".local" + ":" + String(port) + "'>Hostname <span class='badge'>" + String(host_name) + ".local" + ":" + String(port) + "</span></a></li>\n");
  server->sendContent("            <li class='active'>\n");
  server->sendContent("              <a href='http://" + WiFi.localIP().toString() + ":" + String(port) + "'>Local <span class='badge'>" + WiFi.localIP().toString() + ":" + String(port) + "</span></a></li>\n");
  server->sendContent("            <li class='active'>\n");
  if (!getExternalIP || externalIPError) {
    server->sendContent("              <a href='#'>External <span class='badge'>" + externalIP() + ":" + String(port) + "</span></a></li>\n");    
  } else {
    server->sendContent("              <a href='http://" + externalIP() + ":" + String(port) + "'>External <span class='badge'>" + externalIP() + ":" + String(port) + "</span></a></li>\n");
  }
  server->sendContent("            <li class='active'>\n");
  server->sendContent("              <a href='#'> MAC <span class='badge'>" + String(WiFi.macAddress()) + "</span></a></li>\n");
  server->sendContent("            <li class='active'>\n");
  server->sendContent("              <a href='/list'>Stored Codes</a></li>\n");
  server->sendContent("          </ul>\n");
  server->sendContent("        </div>\n");
  server->sendContent("      </div><hr />\n");
}

//+=============================================================================
// Send footer HTML
//
void sendFooter() {

  server->sendContent("      <div class='row'><div class='col-md-12'><em>" + String(millis()) + "ms uptime; EPOCH " + String(timeClient.getUnixTime() - timeOffset) + "</em> / <em id='jepoch'></em> ( <em id='jdiff'></em> )</div></div>\n");
  server->sendContent("      <script>document.getElementById('jepoch').innerHTML = Math.round((new Date()).getTime() / 1000)</script>");
  server->sendContent("      <script>document.getElementById('jdiff').innerHTML = Math.abs(Math.round((new Date()).getTime() / 1000) - " + String(timeClient.getUnixTime() - timeOffset) + ")</script>");  
  if (strlen(user_id) != 0)
  server->sendContent("      <div class='row'><div class='col-md-12'><em>Device secured with SHA256 authentication. Only commands sent and verified with Amazon Alexa and the IR Controller Skill will be processed</em></div></div>");
  if (authError)
  server->sendContent("      <div class='row'><div class='col-md-12'><em>Error - last authentication failed because HMAC signatures did not match, see serial output for debugging details</em></div></div>");
  if (timeAuthError > 0)
  server->sendContent("      <div class='row'><div class='col-md-12'><em>Error - last authentication failed because your timestamps are out of sync, see serial output for debugging details. Timediff: " + String(timeAuthError) + "</em></div></div>");
  if (externalIPError)
  server->sendContent("      <div class='row'><div class='col-md-12'><em>Error - unable to retrieve external IP address, this may be due to bad network settings. There is currently a bug with the latest versions of ESP8266 for Arduino, please use version 2.4.0 along with lwIP v1.4 Prebuilt to resolve this</em></div></div>");
  time_t timenow = timeClient.getUnixTime() - timeOffset;
  if (!validEPOCH(timenow))
  server->sendContent("      <div class='row'><div class='col-md-12'><em>Error - EPOCH time is inappropraitely low, likely connection to external time server has failed, check your network settings</em></div></div>");
  if (userIDError)
  server->sendContent("      <div class='row'><div class='col-md-12'><em>Error - your userID is in the wrong format and authentication will not work</em></div></div>");
  if (ntpError)
  server->sendContent("      <div class='row'><div class='col-md-12'><em>Error - last attempt to connect to the NTP server failed, check NTP settings and networking settings</em></div></div>");
  server->sendContent("    </div>\n");
  server->sendContent("  </body>\n");
  server->sendContent("</html>\n");
  server->client().stop();
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
  String jsonTest;
  sendHeader(httpcode);
  if (type == 1)
  server->sendContent("      <div class='row'><div class='col-md-12'><div class='alert alert-success'><strong>" + header + "!</strong> " + message + "</div></div></div>\n");
  if (type == 2)
  server->sendContent("      <div class='row'><div class='col-md-12'><div class='alert alert-warning'><strong>" + header + "!</strong> " + message + "</div></div></div>\n");
  if (type == 3)
  server->sendContent("      <div class='row'><div class='col-md-12'><div class='alert alert-danger'><strong>" + header + "!</strong> " + message + "</div></div></div>\n");
  server->sendContent("      <div class='row'>\n");
  server->sendContent("        <div class='col-md-12'>\n");
  server->sendContent("          <h3>Codes Transmitted</h3>\n");
  server->sendContent("          <table class='table table-striped' style='table-layout: fixed;'>\n");
  server->sendContent("            <thead><tr><th>Sent</th><th>Command</th><th>Type</th><th>Length</th><th>Address</th></tr></thead>\n"); //Title
  server->sendContent("            <tbody>\n");
  if (last_send.valid)
  server->sendContent("              <tr class='text-uppercase'><td>" + epochToString(last_send.timestamp) + "</td><td><code>" + String(last_send.data) + "</code></td><td><code>" + String(last_send.encoding) + "</code></td><td><code>" + String(last_send.bits) + "</code></td><td><code>" + String(last_send.address) + "</code></td></tr>\n");
  if (last_send_2.valid)
  server->sendContent("              <tr class='text-uppercase'><td>" + epochToString(last_send_2.timestamp) + "</td><td><code>" + String(last_send_2.data) + "</code></td><td><code>" + String(last_send_2.encoding) + "</code></td><td><code>" + String(last_send_2.bits) + "</code></td><td><code>" + String(last_send_2.address) + "</code></td></tr>\n");
  if (last_send_3.valid)
  server->sendContent("              <tr class='text-uppercase'><td>" + epochToString(last_send_3.timestamp) + "</td><td><code>" + String(last_send_3.data) + "</code></td><td><code>" + String(last_send_3.encoding) + "</code></td><td><code>" + String(last_send_3.bits) + "</code></td><td><code>" + String(last_send_3.address) + "</code></td></tr>\n");
  if (last_send_4.valid)
  server->sendContent("              <tr class='text-uppercase'><td>" + epochToString(last_send_4.timestamp) + "</td><td><code>" + String(last_send_4.data) + "</code></td><td><code>" + String(last_send_4.encoding) + "</code></td><td><code>" + String(last_send_4.bits) + "</code></td><td><code>" + String(last_send_4.address) + "</code></td></tr>\n");
  if (last_send_5.valid)
  server->sendContent("              <tr class='text-uppercase'><td>" + epochToString(last_send_5.timestamp) + "</td><td><code>" + String(last_send_5.data) + "</code></td><td><code>" + String(last_send_5.encoding) + "</code></td><td><code>" + String(last_send_5.bits) + "</code></td><td><code>" + String(last_send_5.address) + "</code></td></tr>\n");
  if (!last_send.valid && !last_send_2.valid && !last_send_3.valid && !last_send_4.valid && !last_send_5.valid)
  server->sendContent("              <tr><td colspan='5' class='text-center'><em>No codes sent</em></td></tr>");
  server->sendContent("            </tbody></table>\n");
  server->sendContent("          </div></div>\n");
  server->sendContent("      <div class='row'>\n");
  server->sendContent("        <div class='col-md-12'>\n");
  server->sendContent("          <h3>Codes Received</h3>\n");
  server->sendContent("          <table class='table table-striped' style='table-layout: fixed;'>\n");
  server->sendContent("            <thead><tr><th>Received</th><th>Command</th><th>Type</th><th>Length</th><th>Address</th><th>Action</th></tr></thead>\n"); //Title
  server->sendContent("            <tbody>\n");
  if (last_recv.valid) {
    if (String(last_recv.encoding) == "UNKNOWN") {
      jsonTest = "[{data:[" + String(last_recv.raw) + "],type:raw,khz:38}]";
    } else if (String(last_recv.encoding) == "PANASONIC") {
      jsonTest = "[{data:" + String(last_recv.data) + ",type:" + String(last_recv.encoding) + ",length:" + String(last_recv.bits) + ",address:" + String(last_recv.address) + "}]";
    } else {
      jsonTest = "[{data:" + String(last_recv.data) + ",type:" + String(last_recv.encoding) + ",length:" + String(last_recv.bits) + "}]";
    }
    server->sendContent("              <form style='display:inline-block; margin:0; padding:0' action='/json' method='post'><input type='hidden' name='pass' value='" + String(passcode) + "'><tr class='text-uppercase'><td><a href='/received?id=1'>" + epochToString(last_recv.timestamp) + "</a></td><td><code>" + String(last_recv.data) + "</code></td><td><code>" + String(last_recv.encoding) + "</code></td><td><code>" + String(last_recv.bits) + "</code></td><td><code>" + String(last_recv.address) + "</code></td><td><a class='btn btn-primary btn-xs' href='/store?id=1' role='button'>Store</a> <button name='plain' value='" + String(jsonTest) + "' class='btn btn-primary btn-xs' type='submit'>TEST</button></td></tr></form>\n");
  }  
  if (last_recv_2.valid) {
    if (String(last_recv_2.encoding) == "UNKNOWN") {
      jsonTest = "[{data:[" + String(last_recv_2.raw) + "],type:raw,khz:38}]";
    } else if (String(last_recv_2.encoding) == "PANASONIC") {
      jsonTest = "[{data:" + String(last_recv_2.data) + ",type:" + String(last_recv_2.encoding) + ",length:" + String(last_recv_2.bits) + ",address:" + String(last_recv_2.address) + "}]";
    } else {
      jsonTest = "[{data:" + String(last_recv_2.data) + ",type:" + String(last_recv_2.encoding) + ",length:" + String(last_recv_2.bits) + "}]";
    }
    server->sendContent("              <form style='display:inline-block; margin:0; padding:0' action='/json' method='post'><input type='hidden' name='pass' value='" + String(passcode) + "'><tr class='text-uppercase'><td><a href='/received?id=1'>" + epochToString(last_recv_2.timestamp) + "</a></td><td><code>" + String(last_recv_2.data) + "</code></td><td><code>" + String(last_recv_2.encoding) + "</code></td><td><code>" + String(last_recv_2.bits) + "</code></td><td><code>" + String(last_recv_2.address) + "</code></td><td><a class='btn btn-primary btn-xs' href='/store?id=1' role='button'>Store</a> <button name='plain' value='" + String(jsonTest) + "' class='btn btn-primary btn-xs' type='submit'>TEST</button></td></tr></form>\n");
    }
    if (last_recv_3.valid) {
    if (String(last_recv_3.encoding) == "UNKNOWN") {
      jsonTest = "[{data:[" + String(last_recv_3.raw) + "],type:raw,khz:38}]";
    } else if (String(last_recv_3.encoding) == "PANASONIC") {
      jsonTest = "[{data:" + String(last_recv_3.data) + ",type:" + String(last_recv_3.encoding) + ",length:" + String(last_recv_3.bits) + ",address:" + String(last_recv_3.address) + "}]";
    } else {
      jsonTest = "[{data:" + String(last_recv_3.data) + ",type:" + String(last_recv_3.encoding) + ",length:" + String(last_recv_3.bits) + "}]";
    }
    server->sendContent("              <form style='display:inline-block; margin:0; padding:0' action='/json' method='post'><input type='hidden' name='pass' value='" + String(passcode) + "'><tr class='text-uppercase'><td><a href='/received?id=1'>" + epochToString(last_recv_3.timestamp) + "</a></td><td><code>" + String(last_recv_3.data) + "</code></td><td><code>" + String(last_recv_3.encoding) + "</code></td><td><code>" + String(last_recv_3.bits) + "</code></td><td><code>" + String(last_recv_3.address) + "</code></td><td><a class='btn btn-primary btn-xs' href='/store?id=1' role='button'>Store</a> <button name='plain' value='" + String(jsonTest) + "' class='btn btn-primary btn-xs' type='submit'>TEST</button></td></tr></form>\n");
    }
    if (last_recv_4.valid) {
    if (String(last_recv_4.encoding) == "UNKNOWN") {
      jsonTest = "[{data:[" + String(last_recv_4.raw) + "],type:raw,khz:38}]";
    } else if (String(last_recv_4.encoding) == "PANASONIC") {
      jsonTest = "[{data:" + String(last_recv_4.data) + ",type:" + String(last_recv_4.encoding) + ",length:" + String(last_recv_4.bits) + ",address:" + String(last_recv_4.address) + "}]";
    } else {
      jsonTest = "[{data:" + String(last_recv_4.data) + ",type:" + String(last_recv_4.encoding) + ",length:" + String(last_recv_4.bits) + "}]";
    }
    server->sendContent("              <form style='display:inline-block; margin:0; padding:0' action='/json' method='post'><input type='hidden' name='pass' value='" + String(passcode) + "'><tr class='text-uppercase'><td><a href='/received?id=1'>" + epochToString(last_recv_4.timestamp) + "</a></td><td><code>" + String(last_recv_4.data) + "</code></td><td><code>" + String(last_recv_4.encoding) + "</code></td><td><code>" + String(last_recv_4.bits) + "</code></td><td><code>" + String(last_recv_4.address) + "</code></td><td><a class='btn btn-primary btn-xs' href='/store?id=1' role='button'>Store</a> <button name='plain' value='" + String(jsonTest) + "' class='btn btn-primary btn-xs' type='submit'>TEST</button></td></tr></form>\n");
    }
    if (last_recv_5.valid) {
    if (String(last_recv_5.encoding) == "UNKNOWN") {
      jsonTest = "[{data:[" + String(last_recv_5.raw) + "],type:raw,khz:38}]";
    } else if (String(last_recv_5.encoding) == "PANASONIC") {
      jsonTest = "[{data:" + String(last_recv_5.data) + ",type:" + String(last_recv_5.encoding) + ",length:" + String(last_recv_5.bits) + ",address:" + String(last_recv_5.address) + "}]";
    } else {
      jsonTest = "[{data:" + String(last_recv_5.data) + ",type:" + String(last_recv_5.encoding) + ",length:" + String(last_recv_5.bits) + "}]";
    }
    server->sendContent("              <form style='display:inline-block; margin:0; padding:0' action='/json' method='post'><input type='hidden' name='pass' value='" + String(passcode) + "'><tr class='text-uppercase'><td><a href='/received?id=1'>" + epochToString(last_recv_5.timestamp) + "</a></td><td><code>" + String(last_recv_5.data) + "</code></td><td><code>" + String(last_recv_5.encoding) + "</code></td><td><code>" + String(last_recv_5.bits) + "</code></td><td><code>" + String(last_recv_5.address) + "</code></td><td><a class='btn btn-primary btn-xs' href='/store?id=1' role='button'>Store</a> <button name='plain' value='" + String(jsonTest) + "' class='btn btn-primary btn-xs' type='submit'>TEST</button></td></tr></form>\n");
    }
  if (!last_recv.valid && !last_recv_2.valid && !last_recv_3.valid && !last_recv_4.valid && !last_recv_5.valid)
  server->sendContent("              <tr><td colspan='6' class='text-center'><em>No codes received</em></td></tr>");
  server->sendContent("            </tbody></table>\n");
  server->sendContent("          </div></div>\n");
  server->sendContent("      <div class='row'>\n");
  server->sendContent("        <div class='col-md-12'>\n");
  server->sendContent("          <ul class='list-unstyled'>\n");
  server->sendContent("            <li><span class='badge'>GPIO " + String(pinr1) + "</span> Receiving </li>\n");
  server->sendContent("            <li><span class='badge'>GPIO " + String(pins1) + "</span> Transmitter 1 </li>\n");
  server->sendContent("            <li><span class='badge'>GPIO " + String(pins2) + "</span> Transmitter 2 </li>\n");
  server->sendContent("            <li><span class='badge'>GPIO " + String(pins3) + "</span> Transmitter 3 </li>\n");
  server->sendContent("            <li><span class='badge'>GPIO " + String(pins4) + "</span> Transmitter 4 </li></ul>\n");
  server->sendContent("        </div>\n");
  server->sendContent("      </div>\n");
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
  server->sendContent("      <div class='row'>\n");
  server->sendContent("        <div class='col-md-12'>\n");
  server->sendContent("          <h2><span class='label label-success'>" + String(selCode.data) + ":" + String(selCode.encoding) + ":" + String(selCode.bits) + "</span></h2><br/>\n");
  server->sendContent("          <dl class='dl-horizontal'>\n");
  server->sendContent("            <dt>Data</dt>\n");
  server->sendContent("            <dd><code>" + String(selCode.data)  + "</code></dd></dl>\n");
  server->sendContent("          <dl class='dl-horizontal'>\n");
  server->sendContent("            <dt>Type</dt>\n");
  server->sendContent("            <dd><code>" + String(selCode.encoding)  + "</code></dd></dl>\n");
  server->sendContent("          <dl class='dl-horizontal'>\n");
  server->sendContent("            <dt>Length</dt>\n");
  server->sendContent("            <dd><code>" + String(selCode.bits)  + "</code></dd></dl>\n");
  server->sendContent("          <dl class='dl-horizontal'>\n");
  server->sendContent("            <dt>Address</dt>\n");
  server->sendContent("            <dd><code>" + String(selCode.address)  + "</code></dd></dl>\n");
  server->sendContent("          <dl class='dl-horizontal'>\n");
  server->sendContent("            <dt>Raw</dt>\n");
  server->sendContent("            <dd><code>" + String(selCode.raw)  + "</code></dd></dl>\n");
  server->sendContent("          <dl class='dl-horizontal'>\n");
  server->sendContent("            <dt>Timestamp</dt>\n");
  server->sendContent("            <dd><code>" + epochToString(selCode.timestamp)  + "</code></dd></dl>\n");  
  server->sendContent("        </div></div>\n");
  server->sendContent("      <div class='row'>\n");
  server->sendContent("        <div class='col-md-12'>\n");
  server->sendContent("          <div class='alert alert-warning'>Don't forget to add your passcode to the URLs below if you set one</div>\n");
  server->sendContent("      </div></div>\n");
  if (String(selCode.encoding) == "UNKNOWN") {
  server->sendContent("      <div class='row'>\n");
  server->sendContent("        <div class='col-md-12'>\n");
  server->sendContent("          <ul class='list-unstyled'>\n");
  server->sendContent("            <li>Hostname <span class='label label-default'>JSON</span></li>\n");
  server->sendContent("            <li><pre>http://" + String(host_name) + ".local:" + String(port) + "/json?plain=[{'data':[" + String(selCode.raw) + "],'type':'raw','khz':38}]</pre></li>\n");
  server->sendContent("            <li>Local IP <span class='label label-default'>JSON</span></li>\n");
  server->sendContent("            <li><pre>http://" + WiFi.localIP().toString() + ":" + String(port) + "/json?plain=[{'data':[" + String(selCode.raw) + "],'type':'raw','khz':38}]</pre></li>\n");
  server->sendContent("            <li>External IP <span class='label label-default'>JSON</span></li>\n");
  server->sendContent("            <li><pre>http://" + externalIP() + ":" + String(port) + "/json?plain=[{'data':[" + String(selCode.raw) + "],'type':'raw','khz':38}]</pre></li></ul>\n");
  } else if (String(selCode.encoding) == "PANASONIC") {
  //} else if (strtoul(selCode.address, 0, 0) > 0) {
  server->sendContent("      <div class='row'>\n");
  server->sendContent("        <div class='col-md-12'>\n");
  server->sendContent("          <ul class='list-unstyled'>\n");
  server->sendContent("            <li>Hostname <span class='label label-default'>MSG</span></li>\n");
  server->sendContent("            <li><pre>http://" + String(host_name) + ".local:" + String(port) + "/msg?code=" + String(selCode.data) + ":" + String(selCode.encoding) + ":" + String(selCode.bits) + "&address=" + String(selCode.address) + "</pre></li>\n");
  server->sendContent("            <li>Local IP <span class='label label-default'>MSG</span></li>\n");
  server->sendContent("            <li><pre>http://" + WiFi.localIP().toString() + ":" + String(port) + "/msg?code=" + String(selCode.data) + ":" + String(selCode.encoding) + ":" + String(selCode.bits) + "&address=" + String(selCode.address) + "</pre></li>\n");
  server->sendContent("            <li>External IP <span class='label label-default'>MSG</span></li>\n");
  server->sendContent("            <li><pre>http://" + externalIP() + ":" + String(port) + "/msg?code=" + selCode.data + ":" + String(selCode.encoding) + ":" + String(selCode.bits) + "&address=" + String(selCode.address) + "</pre></li></ul>\n");
  server->sendContent("          <ul class='list-unstyled'>\n");
  server->sendContent("            <li>Hostname <span class='label label-default'>JSON</span></li>\n");
  server->sendContent("            <li><pre>http://" + String(host_name) + ".local:" + String(port) + "/json?plain=[{'data':'" + String(selCode.data) + "','type':'" + String(selCode.encoding) + "','length':" + String(selCode.bits) + ",'address':'" + String(selCode.address) + "'}]</pre></li>\n");
  server->sendContent("            <li>Local IP <span class='label label-default'>JSON</span></li>\n");
  server->sendContent("            <li><pre>http://" + WiFi.localIP().toString() + ":" + String(port) + "/json?plain=[{'data':'" + String(selCode.data) + "','type':'" + String(selCode.encoding) + "','length':" + String(selCode.bits) + ",'address':'" + String(selCode.address) + "'}]</pre></li>\n");
  server->sendContent("            <li>External IP <span class='label label-default'>JSON</span></li>\n");
  server->sendContent("            <li><pre>http://" + externalIP() + ":" + String(port) + "/json?plain=[{'data':'" + String(selCode.data) + "','type':'" + String(selCode.encoding) + "','length':" + String(selCode.bits) + ",'address':'" + String(selCode.address) + "'}]</pre></li></ul>\n");
  } else {
  server->sendContent("      <div class='row'>\n");
  server->sendContent("        <div class='col-md-12'>\n");
  server->sendContent("          <ul class='list-unstyled'>\n");
  server->sendContent("            <li>Hostname <span class='label label-default'>MSG</span></li>\n");
  server->sendContent("            <li><pre>http://" + String(host_name) + ".local:" + String(port) + "/msg?code=" + String(selCode.data) + ":" + String(selCode.encoding) + ":" + String(selCode.bits) + "</pre></li>\n");
  server->sendContent("            <li>Local IP <span class='label label-default'>MSG</span></li>\n");
  server->sendContent("            <li><pre>http://" + WiFi.localIP().toString() + ":" + String(port) + "/msg?code=" + String(selCode.data) + ":" + String(selCode.encoding) + ":" + String(selCode.bits) + "</pre></li>\n");
  server->sendContent("            <li>External IP <span class='label label-default'>MSG</span></li>\n");
  server->sendContent("            <li><pre>http://" + externalIP() + ":" + String(port) + "/msg?code=" + selCode.data + ":" + String(selCode.encoding) + ":" + String(selCode.bits) + "</pre></li></ul>\n");
  server->sendContent("          <ul class='list-unstyled'>\n");
  server->sendContent("            <li>Hostname <span class='label label-default'>JSON</span></li>\n");
  server->sendContent("            <li><pre>http://" + String(host_name) + ".local:" + String(port) + "/json?plain=[{'data':'" + String(selCode.data) + "','type':'" + String(selCode.encoding) + "','length':" + String(selCode.bits) + "}]</pre></li>\n");
  server->sendContent("            <li>Local IP <span class='label label-default'>JSON</span></li>\n");
  server->sendContent("            <li><pre>http://" + WiFi.localIP().toString() + ":" + String(port) + "/json?plain=[{'data':'" + String(selCode.data) + "','type':'" + String(selCode.encoding) + "','length':" + String(selCode.bits) + "}]</pre></li>\n");
  server->sendContent("            <li>External IP <span class='label label-default'>JSON</span></li>\n");
  server->sendContent("            <li><pre>http://" + externalIP() + ":" + String(port) + "/json?plain=[{'data':'" + String(selCode.data) + "','type':'" + String(selCode.encoding) + "','length':" + String(selCode.bits) + "}]</pre></li></ul>\n");
  }
  server->sendContent("        </div>\n");
  server->sendContent("     </div>\n");
  sendFooter();
}


//+=============================================================================
// Stream store code page HTML
//
void storeCodePage(Code selCode, int id) {
  storeCodePage(selCode, id, 200);
}
void storeCodePage(Code selCode, int id, int httpcode){
  sendHeader(httpcode);
  server->sendContent("      <div class='row'>\n");
  server->sendContent("        <div class='col-md-12'>\n");  
  server->sendContent("          <h2><span class='label label-success'>" + String(selCode.data) + ":" + String(selCode.encoding) + ":" + String(selCode.bits) + "</span></h2>\n");
  server->sendContent("          <dl class='dl-horizontal'>\n");
  server->sendContent("            <dt>Data</dt>\n");
  server->sendContent("            <dd><code>" + String(selCode.data)  + "</code></dd></dl>\n");
  server->sendContent("          <dl class='dl-horizontal'>\n");
  server->sendContent("            <dt>Type</dt>\n");
  server->sendContent("            <dd><code>" + String(selCode.encoding)  + "</code></dd></dl>\n");
  server->sendContent("          <dl class='dl-horizontal'>\n");
  server->sendContent("            <dt>Length</dt>\n");
  server->sendContent("            <dd><code>" + String(selCode.bits)  + "</code></dd></dl>\n");
  server->sendContent("          <dl class='dl-horizontal'>\n");
  server->sendContent("            <dt>Address</dt>\n");
  server->sendContent("            <dd><code>" + String(selCode.address)  + "</code></dd></dl>\n");
  server->sendContent("          <dl class='dl-horizontal'>\n");
  server->sendContent("            <dt>Raw</dt>\n");
  server->sendContent("            <dd><code>" + String(selCode.raw)  + "</code></dd></dl>\n");
  server->sendContent("        </div></div>\n");
  server->sendContent("      <div class='row'>\n");
  server->sendContent("        <div class='col-md-12'>\n");
  server->sendContent("        </div>\n");
  server->sendContent("     </div>\n");
  server->sendContent("          <form action='http://"+ WiFi.localIP().toString() +"/store' method='POST'>\n");
  server->sendContent("          <div class='form-group row'>\n");
  server->sendContent("              <label for='name_input' class='col-sm-2 col-form-label col-form-label-sm'>Enter the remote button name:</label>\n");
  server->sendContent("              <div class='col-sm-10'>\n");
  server->sendContent("               <input type='text' class='form-control form-control-sm' name='name_input' placeholder='Remote control button name'><input type='hidden' name='id' value=" + String(id) + ">");
  server->sendContent("              </div>\n");
  server->sendContent("          </div>\n");
  server->sendContent("          <div class='form-group row'>\n");
  server->sendContent("              <label for='pulse' class='col-sm-2 col-form-label col-form-label-sm'>Specify the number of pulses:</label>\n");
  server->sendContent("              <div class='col-sm-10'>\n");
  server->sendContent("               <input type='text' class='form-control form-control-sm' name='pulse' placeholder='Pulses (default - single pulse)'>\n");
  server->sendContent("              </div>\n");
  server->sendContent("          </div>\n");
  server->sendContent("          <div class='form-group row'>\n");
  server->sendContent("              <label for='pulse_delay' class='col-sm-2 col-form-label col-form-label-sm'>Specify the pulse delay:</label>\n");
  server->sendContent("              <div class='col-sm-10'>\n");
  server->sendContent("               <input type='text' class='form-control form-control-sm' name='pulse_delay' placeholder='Pulse delay (milliseconds - default 100)'>\n");
  server->sendContent("              </div>\n");
  server->sendContent("          </div>\n");
  server->sendContent("          <div class='form-group row'>\n");
  server->sendContent("              <label for='repeat' class='col-sm-2 col-form-label col-form-label-sm'>Specify the number of repeats:</label>\n");
  server->sendContent("              <div class='col-sm-10'>\n");
  server->sendContent("               <input type='text' class='form-control form-control-sm' name='repeat' placeholder='Repeat (default - no repeat)'>\n");
  server->sendContent("              </div>\n");
  server->sendContent("          </div>\n");
  server->sendContent("          <div class='form-group row'>\n");
  server->sendContent("              <label for='repeat_delay' class='col-sm-2 col-form-label col-form-label-sm'>Specify the repeat delay:</label>\n");
  server->sendContent("              <div class='col-sm-10'>\n");
  server->sendContent("               <input type='text' class='form-control form-control-sm' name='repeat_delay' placeholder='Repeat delay (milliseconds - default 1000)'>\n");
  server->sendContent("              </div>\n");
  server->sendContent("          </div>\n");
  server->sendContent("          <div class='form-group row'>\n");
  server->sendContent("              <div class='col-sm-10'>\n");
  server->sendContent("              <button type='submit' formaction='/store' class='btn btn-primary'>Store</button>\n");    
  server->sendContent("              </div>\n");
  server->sendContent("          </div>\n");     
  server->sendContent("          </form>\n");
  sendFooter();
}


void listStoredCodes() {
  listStoredCodes("", "");
}
void listStoredCodes(String message, String header) {
  listStoredCodes(message, header, 0);
}
void listStoredCodes(String message, String header, int type) {
  listStoredCodes(message, header, type, 200);
}
void listStoredCodes(String message, String header, int type, int httpcode) {
  sendHeader(httpcode);
  if (type == 1)
  server->sendContent("      <div class='row'><div class='col-md-12'><div class='alert alert-success'><strong>" + header + "!</strong> " + message + "</div></div></div>\n");
  if (type == 2)
  server->sendContent("      <div class='row'><div class='col-md-12'><div class='alert alert-warning'><strong>" + header + "!</strong> " + message + "</div></div></div>\n");
  if (type == 3)
  server->sendContent("      <div class='row'><div class='col-md-12'><div class='alert alert-danger'><strong>" + header + "!</strong> " + message + "</div></div></div>\n");
  server->sendContent("      <div class='row'>\n");
  server->sendContent("        <div class='col-md-12'>\n");
  server->sendContent("          <h3>Codes Stored</h3>\n");
  server->sendContent("          <table class='table table-striped' style='table-layout: fixed;'>\n");
  server->sendContent("            <thead><tr><th>Button Name</th><th>Type</th><th>Action</th></tr></thead>\n"); //Title
  server->sendContent("            <tbody>\n");
  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/codes.json")) {
      //file exists, reading and loading
      Serial.println("reading codes file");
      File configFileRead = SPIFFS.open("/codes.json", "r");
      if (configFileRead) {
        Serial.println("opened codes file");
        DynamicJsonBuffer jsonLoadBuffer;
        JsonObject& jsonRead = jsonLoadBuffer.parseObject(configFileRead);
        configFileRead.close();
        for (auto kv : jsonRead) {
          String typekeyvalue;
          Serial.println(kv.key);
          Serial.println(kv.value.as<char*>());
          JsonObject& jsonReadFinger = jsonRead[kv.key];
          for (auto kvf : jsonReadFinger) {
            if (String(kvf.key) == "type") {
              typekeyvalue = kvf.value.as<char*>();
            }
          }
          server->sendContent("              <tr class='text-uppercase'><td>" + String(kv.key) + "</td><td>" + String(typekeyvalue) + "</td><td><a class='btn btn-primary btn-xs' href='/json?btn=[" + String(kv.key) + "]&pass=" + String(passcode) + "&fromListPage=true' role='button'>Test</a> <a class='btn btn-danger btn-xs' href='/list?item=" + String(kv.key) + "' role='button'>Delete</a></td></tr>\n"); 
        }
        jsonLoadBuffer.clear();
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  server->sendContent("            </tbody></table>\n");
  server->sendContent("          </div></div>\n");
  server->sendContent("          <form>\n");
  server->sendContent("          <div class='form-group row'>\n");
  server->sendContent("              <div class='col-sm-10'>\n");
  server->sendContent("              <button type='submit' formaction='/clear' class='btn btn-danger'>Clear ALL stored codes</button>  \n");  
  server->sendContent("              <span class='badge badge-warning'>Warning:</span>  Will clear all stored codes\n"); 
  server->sendContent("              </div>\n");
  server->sendContent("          </div>\n");   
  server->sendContent("          </form>\n");  
  sendFooter();
}


//+=============================================================================
// Code to JsonObject
//
void cvrtCode(Code& codeData, decode_results *results)
{
  strncpy(codeData.data, uint64ToString(results->value, 16).c_str(), 40);
  strncpy(codeData.encoding, encoding(results).c_str(), 14);
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
    strncpy(codeData.address, ("0x" + String(results->address, HEX)).c_str(), 20);
    strncpy(codeData.command, ("0x" + String(results->command, HEX)).c_str(), 40);
  } else {
    strncpy(codeData.address, "0x0", 20);
    strncpy(codeData.command, "0x0", 40);
  }

}


//+=============================================================================
// Dump out the decode_results structure.
//
void dumpInfo(decode_results *results) {
  if (results->overflow)
    Serial.println("WARNING: IR code too long. "
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
// Send IR codes to variety of sources
//
void irblast(String type, String dataStr, unsigned int len, int rdelay, int pulse, int pdelay, int repeat, long address, IRsend irsend) {
  Serial.println("Code transmit");
  type.toLowerCase();
  uint64_t data = strtoull(("0x" + dataStr).c_str(), 0, 0);
  holdReceive = true;
  Serial.println("Blocking incoming IR signals");
  // Repeat Loop
  for (int r = 0; r < repeat; r++) {
    // Pulse Loop
    for (int p = 0; p < pulse; p++) {
      serialPrintUint64(data, HEX);
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
        Serial.print("Address: ");
        Serial.println(address);
        irsend.sendPanasonic(address, data);
      } else if (type == "jvc") {
        irsend.sendJVC(data, len, 0);
      } else if (type == "samsung") {
        irsend.sendSAMSUNG(data, len);
      } else if (type == "sharpraw") {
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
      } else if (type == "gree") {
        irsend.sendGree(data, len);
      } else if (type == "roomba") {
        roomba_send(atoi(dataStr.c_str()), pulse, pdelay, irsend);
      }
      if (p + 1 < pulse) delay(pdelay);
    }
    if (r + 1 < repeat) delay(rdelay);
  }

  Serial.println("Transmission complete");

  copyCode(last_send_4, last_send_5);
  copyCode(last_send_3, last_send_4);
  copyCode(last_send_2, last_send_3);
  copyCode(last_send, last_send_2);

  strncpy(last_send.data, dataStr.c_str(), 40);
  last_send.bits = len;
  strncpy(last_send.encoding, type.c_str(), 14);
  strncpy(last_send.address, ("0x" + String(address, HEX)).c_str(), 20);
  last_send.timestamp = timeClient.getUnixTime();
  last_send.valid = true;

  resetReceive();
}

void rawblast(JsonArray &raw, int khz, int rdelay, int pulse, int pdelay, int repeat, IRsend irsend,int duty) {
  Serial.println("Raw transmit");
  holdReceive = true;
  Serial.println("Blocking incoming IR signals");
  // Repeat Loop
  for (int r = 0; r < repeat; r++) {
    // Pulse Loop
    for (int p = 0; p < pulse; p++) {
      Serial.println("Sending code");
      irsend.enableIROut(khz,duty);
      for (unsigned int i = 0; i < raw.size(); i++) {
        int val = raw[i];
        if (i & 1) irsend.space(std::max(0, val));
        else       irsend.mark(val);
      }
      irsend.space(0);
      if (p + 1 < pulse) delay(pdelay);
    }
    if (r + 1 < repeat) delay(rdelay);
  }

  Serial.println("Transmission complete");

  copyCode(last_send_4, last_send_5);
  copyCode(last_send_3, last_send_4);
  copyCode(last_send_2, last_send_3);
  copyCode(last_send, last_send_2);

  strncpy(last_send.data, "", 40);
  last_send.bits = raw.size();
  strncpy(last_send.encoding, "RAW", 14);
  strncpy(last_send.address, "0x0", 20);
  last_send.timestamp = timeClient.getUnixTime();
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
  strncpy(c2.encoding, c1.encoding, 14);
  //strncpy(c2.timestamp, c1.timestamp, 40);
  strncpy(c2.address, c1.address, 20);
  strncpy(c2.command, c1.command, 40);
  c2.bits = c1.bits;
  c2.raw = c1.raw;
  c2.timestamp = c1.timestamp;
  c2.valid = c1.valid;
}


void loop() {
  server->handleClient();
  ArduinoOTA.handle();
  decode_results  results;                                        // Somewhere to store the results

  if (irrecv.decode(&results) && !holdReceive) {                  // Grab an IR code
    Serial.println("Signal received:");
    fullCode(&results);                                           // Print the singleline value
    dumpCode(&results);                                           // Output the results as source code
    if (sendToSmartThings) webCoreCommand(&results);              // Send received code to Smartthings
    copyCode(last_recv_4, last_recv_5);                           // Pass
    copyCode(last_recv_3, last_recv_4);                           // Pass
    copyCode(last_recv_2, last_recv_3);                           // Pass
    copyCode(last_recv, last_recv_2);                             // Pass
    cvrtCode(last_recv, &results);                                // Store the results
    last_recv.timestamp = timeClient.getUnixTime();               // Set the new update time
    last_recv.valid = true;
    Serial.println("");                                           // Blank line between entries
    irrecv.resume();                                              // Prepare for the next value
    digitalWrite(ledpin, LOW);                                    // Turn on the LED for 0.5 seconds
    ticker.attach(0.5, disableLed);
  }
  delay(200);
}
