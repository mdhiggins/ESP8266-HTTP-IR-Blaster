#include <FS.h>                                               // This needs to be first, or it all crashes and burns

#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>                                      // https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ESP8266mDNS.h>                                      // Useful to access to ESP by hostname.local

#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>

#include <Ticker.h>                                           // For LED status
#include <NTPClient.h>

const int configpin = 13;                                     // GPIO13 (D7 on D1 Mini) to enable configuration (connect to ground)
const int ledpin = BUILTIN_LED;                               // Built in LED defined for WEMOS people
const char *wifi_config_name = "IRBlaster Configuration";
const char serverName[] = "checkip.dyndns.org";
int port = 80;
char passcode[40] = "";
char host_name[40] = "";
DynamicJsonBuffer jsonBuffer;
JsonObject& last_code = jsonBuffer.createObject();            // Stores last code
JsonObject& last_code_2 = jsonBuffer.createObject();          // Stores 2nd to last code
JsonObject& last_code_3 = jsonBuffer.createObject();          // Stores 3rd to last code
JsonObject& last_code_4 = jsonBuffer.createObject();          // Stores 4th to last code
JsonObject& last_code_5 = jsonBuffer.createObject();          // Stores 5th to last code
JsonObject& last_send = jsonBuffer.createObject();            // Stores last sent
JsonObject& last_send_2 = jsonBuffer.createObject();          // Stores 2nd last sent
JsonObject& last_send_3 = jsonBuffer.createObject();          // Stores 3rd last sent
JsonObject& last_send_4 = jsonBuffer.createObject();          // Stores 4th last sent
JsonObject& last_send_5 = jsonBuffer.createObject();          // Stores 5th last sent

ESP8266WebServer server(port);
HTTPClient http;
Ticker ticker;

bool shouldSaveConfig = false;                                // Flag for saving data
bool holdReceive = false;                                     // Flag to prevent IR receiving while transmitting

int pinr1 = 5;                                                // Receiving pin (GPIO5 = D1)
int pins1 = 4;                                                // Transmitting preset 1
int pins2 = 12;                                               // Transmitting preset 2
int pins3 = 16;                                               // Transmitting preset 3
int pins4 = 15;                                               // Transmitting preset 4

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
    http.end();
  } else {
    Serial.println("Error retrieving external IP");
  }

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

  char port_str[40] = "80";

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

          strncpy(host_name, json["hostname"], 40);
          strncpy(passcode, json["passcode"], 40);
          strncpy(port_str, json["port_str"], 40);
          port = atoi(json["port_str"]);
        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  // end read

  WiFiManagerParameter custom_hostname("hostname", "Choose a hostname to this IRBlaster", host_name, 40);
  wifiManager.addParameter(&custom_hostname);
  WiFiManagerParameter custom_passcode("passcode", "Choose a passcode", passcode, 40);
  wifiManager.addParameter(&custom_passcode);
  WiFiManagerParameter custom_port("port_str", "Choose a port", port_str, 40);
  wifiManager.addParameter(&custom_port);

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
  strncpy(host_name, custom_hostname.getValue(), 40);
  strncpy(passcode, custom_passcode.getValue(), 40);
  strncpy(port_str, custom_port.getValue(), 40);
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

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    Serial.println("");
    json.printTo(configFile);
    configFile.close();
    //e nd save
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
  MDNS.addService("http", "tcp", port); // Announce the ESP as an HTTP service
  String port_str((port == 80)? String("") : String(port));
  Serial.println("URL to send commands: http://" + String(host_name) + ".local:" + port_str);

  if (getTime) timeClient.begin(); // Get the time

  // Configure the server
  server.on("/json", []() { // JSON handler for more complicated IR blaster routines
    Serial.println("Connection received - JSON");

    DynamicJsonBuffer jsonBuffer;
    JsonArray& root = jsonBuffer.parseArray(server.arg("plain"));

    if (!root.success()) {
      Serial.println("JSON parsing failed");
      sendHomePage("JSON parsing failed", "Error", 3, 400); // 400
    } else if (server.arg("pass") != passcode) {
      Serial.println("Unauthorized access");
      sendHomePage("Invalid passcode", "Unauthorized", 3, 401); // 401
    } else {
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
      digitalWrite(ledpin, LOW);
      ticker.attach(0.5, disableLed);
      sendHomePage("Code sent", "Success", 1); // 200
    }
  });

  // Setup simple msg server to mirror version 1.0 functionality
  server.on("/msg", []() {
    Serial.println("Connection received - MSG");
    if (server.arg("pass") != passcode) {
      Serial.println("Unauthorized access");
      sendHomePage("Invalid passcode", "Unauthorized", 3, 401); // 401
    } else {
      String type = server.arg("type");
      String data = server.arg("data");
      String ip = server.arg("ip");
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

      if (type == "roku") {
        rokuCommand(ip, data);
      } else {
        irblast(type, data, len, rdelay, pulse, pdelay, repeat, address, pickIRsend(out));
      }
      digitalWrite(ledpin, LOW);
      ticker.attach(0.5, disableLed);
      sendHomePage("Code Sent", "Success", 1); // 200
    }
  });

  server.on("/received", []() {
    Serial.println("Connection received");
    int id = server.arg("id").toInt();
    String output;
    if (id == 1 && last_code.containsKey("time")) {
      sendCodePage(last_code);
    } else if (id == 2 && last_code_2.containsKey("time")) {
      sendCodePage(last_code_2);
    } else if (id == 3 && last_code_3.containsKey("time")) {
      sendCodePage(last_code_3);
    } else if (id == 4 && last_code_4.containsKey("time")) {
      sendCodePage(last_code_4);
    } else if (id == 5 && last_code_5.containsKey("time")) {
      sendCodePage(last_code_5);
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

  copyJsonSend(last_send_4, last_send_5);
  copyJsonSend(last_send_3, last_send_4);
  copyJsonSend(last_send_2, last_send_3);
  copyJsonSend(last_send, last_send_2);

  last_send["data"] = data;
  last_send["len"] = 0;
  last_send["type"] = "roku";
  last_send["address"] = ip;
  last_send["time"] = String(timeClient.getFormattedTime());
  return http.POST("");
  http.end();
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
void sendHeader(int httpcode = 200) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(httpcode, "text/html; charset=utf-8", "");
  server.sendContent("<!DOCTYPE html PUBLIC '-//W3C//DTD XHTML 1.0 Strict//EN' 'http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd'>\n");
  server.sendContent("<html xmlns='http://www.w3.org/1999/xhtml' xml:lang='en'>\n");
  server.sendContent("  <head>\n");
  server.sendContent("    <meta http-equiv='refresh' content='300' />\n");
  server.sendContent("    <meta name='viewport' content='width=device-width, initial-scale=.75' />\n");
  server.sendContent("    <link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/css/bootstrap.min.css' />\n");
  server.sendContent("    <title>ESP8266 IR Controller (" + String(host_name) + ")</title>\n");
  server.sendContent("  </head>\n");
  server.sendContent("  <body>\n");
  server.sendContent("    <div class='container'>\n");
  server.sendContent("      <h1>ESP8266 IR Controller</h1>\n");
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
  if (last_send.containsKey("time"))
  server.sendContent("              <tr class='text-uppercase'><td>" + last_send["time"].as<String>() + "</td><td><code>" + last_send["data"].as<String>() + "</code></td><td><code>" + last_send["type"].as<String>() + "</code></td><td><code>" + last_send["len"].as<String>() + "</code></td><td><code>" + last_send["address"].as<String>() + "</code></td></tr>\n");
  if (last_send_2.containsKey("time"))
  server.sendContent("              <tr class='text-uppercase'><td>" + last_send_2["time"].as<String>() + "</td><td><code>" + last_send_2["data"].as<String>() + "</code></td><td><code>" + last_send_2["type"].as<String>() + "</code></td><td><code>" + last_send_2["len"].as<String>() + "</code></td><td><code>" + last_send_2["address"].as<String>() + "</code></td></tr>\n");
  if (last_send_3.containsKey("time"))
  server.sendContent("              <tr class='text-uppercase'><td>" + last_send_3["time"].as<String>() + "</td><td><code>" + last_send_3["data"].as<String>() + "</code></td><td><code>" + last_send_3["type"].as<String>() + "</code></td><td><code>" + last_send_3["len"].as<String>() + "</code></td><td><code>" + last_send_3["address"].as<String>() + "</code></td></tr>\n");
  if (last_send_4.containsKey("time"))
  server.sendContent("              <tr class='text-uppercase'><td>" + last_send_4["time"].as<String>() + "</td><td><code>" + last_send_4["data"].as<String>() + "</code></td><td><code>" + last_send_4["type"].as<String>() + "</code></td><td><code>" + last_send_4["len"].as<String>() + "</code></td><td><code>" + last_send_4["address"].as<String>() + "</code></td></tr>\n");
  if (last_send_5.containsKey("time"))
  server.sendContent("              <tr class='text-uppercase'><td>" + last_send_5["time"].as<String>() + "</td><td><code>" + last_send_5["data"].as<String>() + "</code></td><td><code>" + last_send_5["type"].as<String>() + "</code></td><td><code>" + last_send_5["len"].as<String>() + "</code></td><td><code>" + last_send_5["address"].as<String>() + "</code></td></tr>\n");
  if (!last_send.containsKey("time") && !last_send_2.containsKey("time") && !last_send_3.containsKey("time") && !last_send_4.containsKey("time") && !last_send_5.containsKey("time"))
  server.sendContent("              <tr><td colspan='5' class='text-center'><em>No codes sent</em></td></tr>");
  server.sendContent("            </tbody></table>\n");
  server.sendContent("          </div></div>\n");
  server.sendContent("      <div class='row'>\n");
  server.sendContent("        <div class='col-md-12'>\n");
  server.sendContent("          <h3>Codes Received</h3>\n");
  server.sendContent("          <table class='table table-striped' style='table-layout: fixed;'>\n");
  server.sendContent("            <thead><tr><th>Time Sent</th><th>Command</th><th>Type</th><th>Length</th><th>Address</th></tr></thead>\n"); //Title
  server.sendContent("            <tbody>\n");
  if (last_code.containsKey("time"))
  server.sendContent("              <tr class='text-uppercase'><td><a href='/received?id=1'>" + last_code["time"].as<String>() + "</a></td><td><code>" + last_code["data"].as<String>() + "</code></td><td><code>" + last_code["encoding"].as<String>() + "</code></td><td><code>" + last_code["bits"].as<String>() + "</code></td><td><code>" + last_code["address"].as<String>() + "</code></td></tr>\n");
  if (last_code_2.containsKey("time"))
  server.sendContent("              <tr class='text-uppercase'><td><a href='/received?id=2'>" + last_code_2["time"].as<String>() + "</a></td><td><code>" + last_code_2["data"].as<String>() + "</code></td><td><code>" + last_code_2["encoding"].as<String>() + "</code></td><td><code>" + last_code_2["bits"].as<String>() + "</code></td><td><code>" + last_code_2["address"].as<String>() + "</code></td></tr>\n");
  if (last_code_3.containsKey("time"))
  server.sendContent("              <tr class='text-uppercase'><td><a href='/received?id=3'>" + last_code_3["time"].as<String>() + "</a></td><td><code>" + last_code_3["data"].as<String>() + "</code></td><td><code>" + last_code_3["encoding"].as<String>() + "</code></td><td><code>" + last_code_3["bits"].as<String>() + "</code></td><td><code>" + last_code_3["address"].as<String>() + "</code></td></tr>\n");
  if (last_code_4.containsKey("time"))
  server.sendContent("              <tr class='text-uppercase'><td><a href='/received?id=4'>" + last_code_4["time"].as<String>() + "</a></td><td><code>" + last_code_4["data"].as<String>() + "</code></td><td><code>" + last_code_4["encoding"].as<String>() + "</code></td><td><code>" + last_code_4["bits"].as<String>() + "</code></td><td><code>" + last_code_4["address"].as<String>() + "</code></td></tr>\n");
  if (last_code_5.containsKey("time"))
  server.sendContent("              <tr class='text-uppercase'><td><a href='/received?id=5'>" + last_code_5["time"].as<String>() + "</a></td><td><code>" + last_code_5["data"].as<String>() + "</code></td><td><code>" + last_code_5["encoding"].as<String>() + "</code></td><td><code>" + last_code_5["bits"].as<String>() + "</code></td><td><code>" + last_code_5["address"].as<String>() + "</code></td></tr>\n");
  if (!last_code.containsKey("time") && !last_code_2.containsKey("time") && !last_code_3.containsKey("time") && !last_code_4.containsKey("time") && !last_code_5.containsKey("time"))
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
void sendCodePage(JsonObject& selCode) {
  sendCodePage(selCode, 200);
}

void sendCodePage(JsonObject& selCode, int httpcode){
  sendHeader(httpcode);
  server.sendContent("      <div class='row'>\n");
  server.sendContent("        <div class='col-md-12'>\n");
  server.sendContent("          <h2><span class='label label-success'>" + selCode["data"].as<String>() + ":" + selCode["encoding"].as<String>() + ":" + selCode["bits"].as<String>() + "</span></h2><br/>\n");
  server.sendContent("          <dl class='dl-horizontal'>\n");
  server.sendContent("            <dt>Data</dt>\n");
  server.sendContent("            <dd><code>" + selCode["data"].as<String>()  + "</code></dd></dl>\n");
  server.sendContent("          <dl class='dl-horizontal'>\n");
  server.sendContent("            <dt>Type</dt>\n");
  server.sendContent("            <dd><code>" + selCode["encoding"].as<String>()  + "</code></dd></dl>\n");
  server.sendContent("          <dl class='dl-horizontal'>\n");
  server.sendContent("            <dt>Length</dt>\n");
  server.sendContent("            <dd><code>" + selCode["bits"].as<String>()  + "</code></dd></dl>\n");
  server.sendContent("          <dl class='dl-horizontal'>\n");
  server.sendContent("            <dt>Address</dt>\n");
  server.sendContent("            <dd><code>" + selCode["address"].as<String>()  + "</code></dd></dl>\n");
  server.sendContent("          <dl class='dl-horizontal'>\n");
  server.sendContent("            <dt>Raw</dt>\n");
  server.sendContent("            <dd><code>" + selCode["uint16_t"].as<String>()  + "</code></dd></dl>\n");
  server.sendContent("        </div></div>\n");
  server.sendContent("      <div class='row'>\n");
  server.sendContent("        <div class='col-md-12'>\n");
  server.sendContent("          <div class='alert alert-warning'>Don't forget to add your passcode to the URLs below if you set one</div>\n");
  server.sendContent("      </div></div>\n");
  if (selCode["encoding"] == "UNKNOWN") {
    server.sendContent("    <div class='row'>\n");
    server.sendContent("      <div class='col-md-12'>\n");
    server.sendContent("        <ul class='list-unstyled'>\n");
    server.sendContent("          <li>Hostname <span class='label label-default'>JSON</span></li>\n");
    server.sendContent("          <li><pre>http://" + String(host_name) + ".local:" + String(port) + "/json?plain=[{'data':[" + selCode["uint16_t"].as<String>() + "], 'type':'raw', 'khz':38}]</pre></li>\n");
    server.sendContent("          <li>Local IP <span class='label label-default'>JSON</span></li>\n");
    server.sendContent("          <li><pre>http://" + ipToString(WiFi.localIP()) + ":" + String(port) + "/json?plain=[{'data':[" + selCode["uint16_t"].as<String>() + "], 'type':'raw', 'khz':38}]</pre></li>\n");
    server.sendContent("          <li>External IP <span class='label label-default'>JSON</span></li>\n");
    server.sendContent("          <li><pre>http://" + externalIP() + ":" + String(port) + "/json?plain=[{'data':[" + selCode["uint16_t"].as<String>() + "], 'type':'raw', 'khz':38}]</pre></li></ul>\n");
  } else {
    server.sendContent("    <div class='row'>\n");
    server.sendContent("      <div class='col-md-12'>\n");
    server.sendContent("        <ul class='list-unstyled'>\n");
    server.sendContent("          <li>Hostname <span class='label label-default'>MSG</span></li>\n");
    server.sendContent("          <li><pre>http://" + String(host_name) + ".local:" + String(port) + "/msg?code=" + selCode["data"].as<String>() + ":" + selCode["encoding"].as<String>() + ":" + selCode["bits"].as<String>() + "</pre></li>\n");
    server.sendContent("          <li>Local IP <span class='label label-default'>MSG</span></li>\n");
    server.sendContent("          <li><pre>http://" + ipToString(WiFi.localIP()) + ":" + String(port) + "/msg?code=" + selCode["data"].as<String>() + ":" + selCode["encoding"].as<String>() + ":" + selCode["bits"].as<String>() + "</pre></li>\n");
    server.sendContent("          <li>External IP <span class='label label-default'>MSG</span></li>\n");
    server.sendContent("          <li><pre>http://" + externalIP() + ":" + String(port) + "/msg?code=" + selCode["data"].as<String>() + ":" + selCode["encoding"].as<String>() + ":" + selCode["bits"].as<String>() + "</pre></li></ul>\n");
    server.sendContent("        <ul class='list-unstyled'>\n");
    server.sendContent("          <li>Hostname <span class='label label-default'>JSON</span></li>\n");
    server.sendContent("          <li><pre>http://" + String(host_name) + ".local:" + String(port) + "/json?plain=[{'data':'" + selCode["data"].as<String>() + "', 'type':'" + selCode["encoding"].as<String>() + "', 'length':" + selCode["bits"].as<String>() + "}]</pre></li>\n");
    server.sendContent("          <li>Local IP <span class='label label-default'>JSON</span></li>\n");
    server.sendContent("          <li><pre>http://" + ipToString(WiFi.localIP()) + ":" + String(port) + "/json?plain=[{'data':'" + selCode["data"].as<String>() + "', 'type':'" + selCode["encoding"].as<String>() + "', 'length':" + selCode["bits"].as<String>() + "}]</pre></li>\n");
    server.sendContent("          <li>External IP <span class='label label-default'>JSON</span></li>\n");
    server.sendContent("          <li><pre>http://" + externalIP() + ":" + String(port) + "/json?plain=[{'data':'" + selCode["data"].as<String>() + "', 'type':'" + selCode["encoding"].as<String>() + "', 'length':" + selCode["bits"].as<String>() + "}]</pre></li></ul>\n");
  }
  server.sendContent("        </div>\n");
  server.sendContent("     </div>\n");
  sendFooter();
}

//+=============================================================================
// Code to JsonObject
//
void codeJson(JsonObject &codeData, decode_results *results)
{
  codeData["data"] = Uint64toString(results->value, 16);
  codeData["encoding"] = encoding(results);
  codeData["bits"] = results->bits;
  String r = "";
      for (uint16_t i = 1; i < results->rawlen; i++) {
      r += results->rawbuf[i] * USECPERTICK;
      if (i < results->rawlen - 1)
        r += ",";                           // ',' not needed on last one
      if (!(i & 1)) r += " ";
    }
  codeData["uint16_t"] = r;
  if (results->decode_type != UNKNOWN) {
    codeData["address"] = "0x" + String(results->address, HEX);
    codeData["command"] = "0x" + String(results->command, HEX);
  } else {
    codeData["address"] = "0x";
    codeData["command"] = "0x";
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
    uint32_t x = results->rawbuf[i] * USECPERTICK;
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
    Serial.print(results->rawbuf[i] * USECPERTICK, DEC);
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
      } else if (type == "sharp") {
        irsend.sendSharpRaw(data, len);
      } else if (type == "dish") {
        irsend.sendDISH(data, len);
      } else if (type == "rc5") {
        irsend.sendRC5(data, len);
      } else if (type == "rc6") {
        irsend.sendRC6(data, len);
      } else if (type == "roomba") {
        roomba_send(atoi(dataStr.c_str()), pulse, pdelay, irsend);
      }
      delay(pdelay);
    }
    delay(rdelay);
  }

  copyJsonSend(last_send_4, last_send_5);
  copyJsonSend(last_send_3, last_send_4);
  copyJsonSend(last_send_2, last_send_3);
  copyJsonSend(last_send, last_send_2);

  last_send["data"] = dataStr;
  last_send["len"] = len;
  last_send["type"] = type;
  last_send["address"] = address;
  last_send["time"] = String(timeClient.getFormattedTime());

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
      int first = raw[0];
      for (unsigned int i = 0; i < raw.size(); i++) {
        unsigned int val = raw[i];
        if (i & 1) irsend.space(val);
        else       irsend.mark(val);
      }
      irsend.space(0);
      delay(pdelay);
    }
    delay(rdelay);
  }

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

void copyJson(JsonObject& j1, JsonObject& j2) {
  if (j1.containsKey("data"))     j2["data"] = j1["data"];
  if (j1.containsKey("encoding")) j2["encoding"] = j1["encoding"];
  if (j1.containsKey("bits"))     j2["bits"] = j1["bits"];
  if (j1.containsKey("address"))  j2["address"] = j1["address"];
  if (j1.containsKey("command"))  j2["command"] = j1["command"];
  if (j1.containsKey("time"))     j2["time"] = j1["time"];
  if (j1.containsKey("uint16_t")) j2["uint16_t"] = j1["uint16_t"];
}

void copyJsonSend(JsonObject& j1, JsonObject& j2) {
  if (j1.containsKey("data"))    j2["data"] = j1["data"];
  if (j1.containsKey("type"))    j2["type"] = j1["type"];
  if (j1.containsKey("len"))     j2["len"] = j1["len"];
  if (j1.containsKey("address")) j2["address"] = j1["address"];
  if (j1.containsKey("time"))    j2["time"] = j1["time"];
}

void loop() {
  server.handleClient();
  decode_results  results;                                        // Somewhere to store the results
  if (getTime) timeClient.update();                               // Update the time

  if (irrecv.decode(&results) && !holdReceive) {                  // Grab an IR code
    Serial.println("Signal received:");
    fullCode(&results);                                           // Print the singleline value
    dumpCode(&results);                                           // Output the results as source code
    copyJson(last_code_4, last_code_5);                           // Pass
    copyJson(last_code_3, last_code_4);                           // Pass
    copyJson(last_code_2, last_code_3);                           // Pass
    copyJson(last_code, last_code_2);                             // Pass
    codeJson(last_code, &results);                                // Store the results
    last_code["time"] = String(timeClient.getFormattedTime());    // Set the new update time
    Serial.println("");                                           // Blank line between entries
    irrecv.resume();                                              // Prepare for the next value
    digitalWrite(ledpin, LOW);                                    // Turn on the LED for 0.5 seconds
    ticker.attach(0.5, disableLed);
  }
  delay(200);
}
