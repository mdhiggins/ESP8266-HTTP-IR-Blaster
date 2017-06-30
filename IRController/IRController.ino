#include <FS.h>                   // This needs to be first, or it all crashes and burns

#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ESP8266mDNS.h>          // useful to access to ESP by hostname.local

#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>

#include <Ticker.h>               // For LED status
#include <NTPClient.h>

const int configpin = 13;         // GPIO13 (D7 on D1 Mini) to enable configuration (connect to ground)
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

bool shouldSaveConfig = false;    // Flag for saving data

int pinr1 = 5;                    // Receiving pin (GPIO5 = D1)
int pins1 = 4;                    // Transmitting preset 1
int pins2 = 12;                   // Transmitting preset 2
int pins3 = 16;                   // Transmitting preset 3
int pins4 = 15;                   // Transmitting preset 4

IRrecv irrecv(pinr1);
IRsend irsend1(pins1);
IRsend irsend2(pins2);
IRsend irsend3(pins3);
IRsend irsend4(pins4);

WiFiClient client;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

String _ip = "";
unsigned long lastupdate = 0;
unsigned long resetfrequency = 259200000; // 72 hours in milliseconds

//+=============================================================================
// Callback notifying us of the need to save config
//
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


//+=============================================================================
// Toggle state
//
void tick()
{
  int state = digitalRead(BUILTIN_LED);  // get the current state of GPIO1 pin
  digitalWrite(BUILTIN_LED, !state);     // set pin to the opposite state
}


//+=============================================================================
// Get External IP Address
//
String externalIP()
{
  if (_ip != "") {
    if (millis() - lastupdate > resetfrequency || lastupdate > millis()) {
      Serial.println("Reseting cached external IP address");
      _ip = ""; // Reset the cached external IP every 72 hours
    } else {
      return _ip;
    }
  }

  if(client.connect(serverName, 8245)) // you can use port 80, but this works faster
  {
    String readBuffer = "";
    // Make a HTTP request:
    client.println("GET / HTTP/1.0");
    client.println("Host: checkip.dyndns.org");
    client.println("Connection: close");
    client.println();

    while ( _ip == "" || millis() < 5000 ) {
      Serial.println("Retrieving external IP address");
      // read incoming bytes available from the server
      if (client.available()) {
        //give time to receive whole message to the buffer
        delay(10);
        while (client.available()) {
          char c = client.read();
          readBuffer += c;
          if (readBuffer.length() > 100) {
            readBuffer = readBuffer.substring(70, readBuffer.length());
          }
        }
        int pos_start = readBuffer.indexOf("IP Address") + 12; // add 10 for "IP Address" and 2 for ":" + "space"
        int pos_end = readBuffer.indexOf("</body>", pos_start); // add nothing
        _ip = readBuffer.substring(pos_start, pos_end);
        Serial.print(F("External IP: "));
        Serial.println(_ip);
        readBuffer = "";
        client.stop();
        lastupdate = millis();
      }
    }
  }
  return _ip;
}


//+=============================================================================
// Turn off the Led after timeout
//
void disableLed()
{
  Serial.println("Turning off the LED to save power.");
  digitalWrite(BUILTIN_LED, HIGH);     // Shut down the LED
  ticker.detach();                     // Stopping the ticker
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
  pinMode(BUILTIN_LED, OUTPUT);
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
  digitalWrite(BUILTIN_LED, LOW);
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
  digitalWrite(BUILTIN_LED, LOW);
  // Turn off the led in 5s
  ticker.attach(5, disableLed);

  // Configure mDNS
  if (MDNS.begin(host_name)) {
    Serial.println("mDNS started. Hostname is set to " + String(host_name) + ".local");
  }
  MDNS.addService("http", "tcp", port); // Anounce the ESP as an HTTP service
  String port_str((port == 80)? String("") : String(port));
  Serial.println("URL to send commands: http://" + String(host_name) + ".local:" + port_str);

  timeClient.update(); // Get the time

  // Configure the server
  server.on("/json", []() { // JSON handler for more complicated IR blaster routines
    Serial.println("Connection received - JSON");

    DynamicJsonBuffer jsonBuffer;
    JsonArray& root = jsonBuffer.parseArray(server.arg("plain"));

    if (!root.success()) {
      Serial.println("JSON parsing failed");
      server.send(400, "text/html", getPage("JSON parsing failed", "Error", 2));
    } else if (server.arg("pass") != passcode) {
      Serial.println("Unauthorized access");
      server.send(401, "text/html", getPage("Invalid passcode", "Unauthorized", 2));
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
      server.send(200, "text/html", getPage("Code sent", "Success", 1));
    }
  });

  // Setup simple msg server to mirror version 1.0 functionality
  server.on("/msg", []() {
    Serial.println("Connection received - MSG");
    if (server.arg("pass") != passcode) {
      Serial.println("Unauthorized access");
      server.send(401, "text/html", getPage("Invalid passcode", "Unauthorized", 2));
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
      server.send(200, "text/html", getPage("Code Sent", "Success", 1));
    }
  });

  server.on("/received", []() {
    Serial.println("Connection received");
    int id = server.arg("id").toInt();
    String output;
    if (id == 1) {
      output = codePage(last_code);
    } else if (id == 2) {
      output = codePage(last_code_2);
    } else if (id == 3) {
      output = codePage(last_code_3);
    } else if (id == 4) {
      output = codePage(last_code_4);
    } else if (id == 5) {
      output = codePage(last_code_5);
    } else {
      output = "";
    }
    server.send(200, "text/html", output);
  });

  server.on("/", []() {
    Serial.println("Connection received");
    server.send(200, "text/html", getPage("", "", 0));
  });

  server.begin();
  Serial.println("HTTP Server started on port " + String(port));

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
// Generate info page HTML
//
String getPage(String message, String header, int type) {
  String page = "<html lang='fr'><head><meta http-equiv='refresh' content='300' name='viewport' content='width=device-width,initial-scale=1'/>";
  page += "<link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/css/bootstrap.min.css'></script>";
  page += "<title>ESP8266 IR Controller (" + String(host_name) + ")</title></head><body>";
  page += "<div class='container-fluid'>";
  page +=   "<div class='row'>";
  page +=     "<div class='col-md-12'>";
  page +=       "<h1>ESP8266 IR Controller</h1>";
  page +=       "<ul class='nav nav-pills'>";
  page +=         "<li class='active'>";
  page +=           "<a href='#'><span class='badge pull-right'>" + String(host_name) + ".local" + ":" + String(port) + "</span> Hostname</a></li>";
  page +=         "<li class='active'>";
  page +=           "<a href='#'><span class='badge pull-right'>" + ipToString(WiFi.localIP()) + ":" + String(port) + "</span> Local</a></li>";
  page +=         "<li class='active'>";
  page +=           "<a href='#'><span class='badge pull-right'>" + externalIP() + ":" + String(port) + "</span> External</a></li>";
  page +=         "<li class='active'>";
  page +=           "<a href='#'><span class='badge pull-right'>" + String(WiFi.macAddress()) + "</span> Mac Address</a></li>";
  page +=       "</ul>";
  if (type == 1)
  page +=       "<br /><div class='alert alert-success' role='alert'><strong>" + header + "</strong> " + message + "</div>";
  if (type == 2)
  page +=       "<br /><div class='alert alert-error' role='alert'><strong>" + header + "</strong> " + message + "</div>";
  page +=       "<h3>Codes Transmitted</h3>";
  page +=       "<table class='table table-striped' style='table-layout: fixed;'>";
  page +=         "<thead><tr><th>Time Sent</th><th>Command</th><th>Type</th><th>Length</th><th>Address</th></tr></thead>"; //Title
  page +=         "<tbody>";
  if (last_send.containsKey("time"))
  page +=           "<tr class='text-uppercase'><td>" + last_send["time"].as<String>() + "</td><td>" + last_send["data"].as<String>() + "</td><td>" + last_send["type"].as<String>() + "</td><td>" + last_send["len"].as<String>() + "</td><td>" + last_send["address"].as<String>() + "</td></tr>";
  if (last_send_2.containsKey("time"))
  page +=           "<tr class='text-uppercase'><td>" + last_send_2["time"].as<String>() + "</td><td>" + last_send_2["data"].as<String>() + "</td><td>" + last_send_2["type"].as<String>() + "</td><td>" + last_send_2["len"].as<String>() + "</td><td>" + last_send_2["address"].as<String>() + "</td></tr>";
  if (last_send_3.containsKey("time"))
  page +=           "<tr class='text-uppercase'><td>" + last_send_3["time"].as<String>() + "</td><td>" + last_send_3["data"].as<String>() + "</td><td>" + last_send_3["type"].as<String>() + "</td><td>" + last_send_3["len"].as<String>() + "</td><td>" + last_send_3["address"].as<String>() + "</td></tr>";
  if (last_send_4.containsKey("time"))
  page +=           "<tr class='text-uppercase'><td>" + last_send_4["time"].as<String>() + "</td><td>" + last_send_4["data"].as<String>() + "</td><td>" + last_send_4["type"].as<String>() + "</td><td>" + last_send_4["len"].as<String>() + "</td><td>" + last_send_4["address"].as<String>() + "</td></tr>";
  if (last_send_5.containsKey("time"))
  page +=           "<tr class='text-uppercase'><td>" + last_send_5["time"].as<String>() + "</td><td>" + last_send_5["data"].as<String>() + "</td><td>" + last_send_5["type"].as<String>() + "</td><td>" + last_send_5["len"].as<String>() + "</td><td>" + last_send_5["address"].as<String>() + "</td></tr>";
  page +=         "</tbody></table>";
  page +=       "<h3>Codes Received</h3>";
  page +=       "<table class='table table-striped' style='table-layout: fixed;'>";
  page +=         "<thead><tr><th>Time Sent</th><th>Command</th><th>Type</th><th>Length</th><th>Address</th></tr></thead>"; //Title
  page +=         "<tbody>";
  if (last_code.containsKey("time"))
  page +=           "<tr class='text-uppercase'><td>" + last_code["time"].as<String>() + "</td><td><a href='/received?id=1'>" + last_code["data"].as<String>() + "</a></td><td>" + last_code["encoding"].as<String>() + "</td><td>" + last_code["bits"].as<String>() + "</td><td>" + last_code["address"].as<String>() + "</td></tr>";
  if (last_code_2.containsKey("time"))
  page +=           "<tr class='text-uppercase'><td>" + last_code_2["time"].as<String>() + "</td><td><a href='/received?id=2'>" + last_code_2["data"].as<String>() + "</a></td><td>" + last_code_2["encoding"].as<String>() + "</td><td>" + last_code_2["bits"].as<String>() + "</td><td>" + last_code_2["address"].as<String>() + "</td></tr>";
  if (last_code_3.containsKey("time"))
  page +=           "<tr class='text-uppercase'><td>" + last_code_3["time"].as<String>() + "</td><td><a href='/received?id=3'>" + last_code_3["data"].as<String>() + "</a></td><td>" + last_code_3["encoding"].as<String>() + "</td><td>" + last_code_3["bits"].as<String>() + "</td><td>" + last_code_3["address"].as<String>() + "</td></tr>";
  if (last_code_4.containsKey("time"))
  page +=           "<tr class='text-uppercase'><td>" + last_code_4["time"].as<String>() + "</td><td><a href='/received?id=4'>" + last_code_4["data"].as<String>() + "</a></td><td>" + last_code_4["encoding"].as<String>() + "</td><td>" + last_code_4["bits"].as<String>() + "</td><td>" + last_code_4["address"].as<String>() + "</td></tr>";
  if (last_code_5.containsKey("time"))
  page +=           "<tr class='text-uppercase'><td>" + last_code_5["time"].as<String>() + "</td><td><a href='/received?id=5'>" + last_code_5["data"].as<String>() + "</a></td><td>" + last_code_5["encoding"].as<String>() + "</td><td>" + last_code_5["bits"].as<String>() + "</td><td>" + last_code_5["address"].as<String>() + "</td></tr>";
  page +=         "</tbody></table>";
  page +=       "<ul class='list-unstyled'>";
  page +=         "<li><span class='badge'>GPIO " + String(pinr1) + "</span> Receiving </li>";
  page +=         "<li><span class='badge'>GPIO " + String(pins1) + "</span> Transmitter 1 </li>";
  page +=         "<li><span class='badge'>GPIO " + String(pins2) + "</span> Transmitter 2 </li>";
  page +=         "<li><span class='badge'>GPIO " + String(pins3) + "</span> Transmitter 3 </li>";
  page +=         "<li><span class='badge'>GPIO " + String(pins4) + "</span> Transmitter 4 </li></ul>";
  page +=       "<footer><em>" + String(millis()) + "ms uptime</em></footer>";
  page += "</div></div></div>";
  page += "</body></html>";
  return page;
}

//+=============================================================================
// Generate full code datasheet
//
String codePage(JsonObject& selCode){
  String eip = externalIP();
  String page = "<html lang='fr'><head><meta name='viewport' content='width=device-width,initial-scale=1'/>";
  page += "<link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/css/bootstrap.min.css'>";
  page += "<title>ESP8266 IR Controller (" + String(host_name) + ")</title></head><body>";
  page += "<div class='container-fluid'>";
  page +=   "<div class='row'>";
  page +=     "<div class='col-md-12'>";
  page +=       "<h1>ESP8266 IR Controller</h1>";
  page +=       "<ul class='nav nav-pills'>";
  page +=         "<li class='active'>";
  page +=           "<a href='#'><span class='badge pull-right'>" + String(host_name) + ".local" + ":" + String(port) + "</span> Hostname</a></li>";
  page +=         "<li class='active'>";
  page +=           "<a href='#'><span class='badge pull-right'>" + ipToString(WiFi.localIP()) + ":" + String(port) + "</span> Local</a></li>";
  page +=         "<li class='active'>";
  page +=           "<a href='#'><span class='badge pull-right'>" + eip + ":" + String(port) + "</span> External</a></li>";
  page +=         "<li class='active'>";
  page +=           "<a href='#'><span class='badge pull-right'>" + String(WiFi.macAddress()) + "</span> Mac Address</a></li>";
  page +=       "</ul>";
  page +=       "<h3>Code ";
  page +=       "<code>" + selCode["data"].as<String>() + ":" + selCode["encoding"].as<String>() + ":" + selCode["bits"].as<String>() + "</code></h3>";
  page +=       "<dl class='dl-horizontal'>";
  page +=         "<dt>Data</dt>";
  page +=         "<dd><code>" + selCode["data"].as<String>()  + "</code></dd></dl>";
  page +=       "<dl class='dl-horizontal'>";
  page +=         "<dt>Type</dt>";
  page +=         "<dd><code>" + selCode["encoding"].as<String>()  + "</code></dd></dl>";
  page +=       "<dl class='dl-horizontal'>";
  page +=         "<dt>Length</dt>";
  page +=         "<dd><code>" + selCode["bits"].as<String>()  + "</code></dd></dl>";
  page +=       "<dl class='dl-horizontal'>";
  page +=         "<dt>Address</dt>";
  page +=         "<dd><code>" + selCode["address"].as<String>()  + "</code></dd></dl>";
  page +=       "<dl class='dl-horizontal'>";
  page +=         "<dt>Raw</dt>";
  page +=         "<dd><code>" + selCode["uint16_t"].as<String>()  + "</code></dd></dl>";
  page +=       "<div class='alert alert-warning' role='alert'>Don't forget to add your passcode to the URLs below if you set one</div>";
  if (selCode["encoding"] == "UNKNOWN") {
    page +=     "<ul class='list-unstyled'>";
    page +=       "<li>Hostname <span class='label label-default'>JSON</span></li>";
    page +=       "<li><pre>http://" + String(host_name) + ".local:" + String(port) + "/json?plan=[{'data':[" + selCode["uint16_t"].as<String>() + "], 'type':'raw', 'khz':38}]</pre></li>";
    page +=       "<li>Local IP <span class='label label-default'>JSON</span</li>";
    page +=       "<li><pre>http://" + ipToString(WiFi.localIP()) + ":" + String(port) + "/json?plan=[{'data':[" + selCode["uint16_t"].as<String>() + "], 'type':'raw', 'khz':38}]</pre></li>";
    page +=       "<li>External IP <span class='label label-default'>JSON</span</li>";
    page +=       "<li><pre>http://" + eip + ":" + String(port) + "/json?plan=[{'data':[" + selCode["uint16_t"].as<String>() + "], 'type':'raw', 'khz':38}]</pre></li>";
  } else {
    page +=     "<ul class='list-unstyled'>";
    page +=       "<li>Hostname <span class='label label-default'>MSG</span></li>";
    page +=       "<li><pre>http://" + String(host_name) + ".local:" + String(port) + "/msg?code=" + selCode["data"].as<String>() + ":" + selCode["encoding"].as<String>() + ":" + selCode["bits"].as<String>() + "</pre></li>";
    page +=       "<li>Local IP <span class='label label-default'>MSG</span</li>";
    page +=       "<li><pre>http://" + ipToString(WiFi.localIP()) + ":" + String(port) + "/msg?code=" + selCode["data"].as<String>() + ":" + selCode["encoding"].as<String>() + ":" + selCode["bits"].as<String>() + "</pre></li>";
    page +=       "<li>External IP <span class='label label-default'>MSG</span</li>";
    page +=       "<li><pre>http://" + eip + ":" + String(port) + "/msg?code=" + selCode["data"].as<String>() + ":" + selCode["encoding"].as<String>() + ":" + selCode["bits"].as<String>() + "</pre></li>";
    page +=     "<ul class='list-unstyled'>";
    page +=       "<li>Hostname <span class='label label-default'>JSON</span></li>";
    page +=       "<li><pre>http://" + String(host_name) + ".local:" + String(port) + "/json?plan=[{'data':'" + selCode["data"].as<String>() + "', 'type':'" + selCode["encoding"].as<String>() + "', 'length':" + selCode["bits"].as<String>() + "}]</pre></li>";
    page +=       "<li>Local IP <span class='label label-default'>JSON</span</li>";
    page +=       "<li><pre>http://" + ipToString(WiFi.localIP()) + ":" + String(port) + "/json?plan=[{'data':'" + selCode["data"].as<String>() + "', 'type':'" + selCode["encoding"].as<String>() + "', 'length':" + selCode["bits"].as<String>() + "}]</pre></li>";
    page +=       "<li>External IP <span class='label label-default'>JSON</span</li>";
    page +=       "<li><pre>http://" + eip + ":" + String(port) + "/json?plan=[{'data':'" + selCode["data"].as<String>() + "', 'type':'" + selCode["encoding"].as<String>() + "', 'length':" + selCode["bits"].as<String>() + "}]</pre></li>";
  }
  page +=       "<footer><em>" + String(millis()) + "ms uptime</em></footer>";
  page += "</div></div></div>";
  page += "</body></html>";
  return page;
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
}


void rawblast(JsonArray &raw, int khz, int rdelay, int pulse, int pdelay, int repeat, IRsend irsend) {
  Serial.println("Raw transmit");

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
}


void roomba_send(int code, int pulse, int pdelay, IRsend irsend)
{
  Serial.print("Sending Roomba code ");
  Serial.println(code);
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
}

void copyJson(JsonObject& j1, JsonObject& j2) {
  if (j1.containsKey("data"))     j2["data"] = j1["data"];
  if (j1.containsKey("encoding")) j2["encoding"] = j1["encoding"];
  if (j1.containsKey("bits"))     j2["bits"] = j1["bits"];
  if (j1.containsKey("address"))  j2["address"] = j1["address"];
  if (j1.containsKey("command"))  j2["command"] = j1["command"];
  if (j1.containsKey("time"))     j2["time"] = j1["time"];
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
  decode_results  results;                // Somewhere to store the results

  if (irrecv.decode(&results)) {          // Grab an IR code
    Serial.println("Signal received:");
    fullCode(&results);                   // Print the singleline value
    //dumpInfo(&results);                 // Output the results
    //dumpRaw(&results);                  // Output the results in RAW format
    dumpCode(&results);                   // Output the results as source code
    copyJson(last_code_4, last_code_5);   // Pass
    copyJson(last_code_3, last_code_4);   // Pass
    copyJson(last_code_2, last_code_3);   // Pass
    copyJson(last_code, last_code_2);     // Pass
    codeJson(last_code, &results);        // Store the results
    last_code["time"] = String(timeClient.getFormattedTime());
    Serial.println("");                   // Blank line between entries
    irrecv.resume();                      // Prepare for the next value
    digitalWrite(BUILTIN_LED, LOW);       // Turn on the LED
    ticker.attach(1, disableLed);
  }
  delay(200);
}
