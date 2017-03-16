#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <IRremoteESP8266.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ESP8266mDNS.h>          // useful to access to ESP by hostname.local

#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>

//for LED status
#include <Ticker.h>

// There is a jumper on pin 13. Take it to ground to force reconfiguration of Wifi
// If high, it reuses the same Wifi configuration as before.
// If automatic reconfiguration failed, a new Wifi network "irblaster" is created to
// configure the wifi settings.
const int configpin = 13; // GPIO13 (D7 on D1 Mini)
const char *wifi_config_name = "IRBlaster Configuration";
const int port = 80;
char passcode[40] = "";
char host_name[40] = "";
char last_codes[5][20];
char last_code_idx = 0;

ESP8266WebServer server(port);
HTTPClient http;
Ticker ticker;

//flag for saving data
bool shouldSaveConfig = false;

IRrecv irrecv(5); // Receiving pin (GPIO5 = D1)
IRsend irsend1(4); // Transmitting preset 1
IRsend irsend2(0); // Transmitting preset 2
IRsend irsend3(12); // Transmitting preset 3
IRsend irsend4(13); // Transmitting preset 4
//
// End configuration area
//+=============================================================================


//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


// Toggle state
void tick()
{
  int state = digitalRead(BUILTIN_LED);  // get the current state of GPIO1 pin
  digitalWrite(BUILTIN_LED, !state);     // set pin to the opposite state
}

// Turn off the Led after timeout
void disableLed()
{
  Serial.println("Turning off the LED to save power.");
  digitalWrite(BUILTIN_LED, HIGH);     // Shut down the LED
  ticker.detach();                     // Stopping the ticker
}


// Gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}


// First setup of the Wifi.
// If return true, the Wifi is well connected.
// Should not return false if Wifi cannot be connected, it will loop
bool setupWifi(bool resetConf) {

  //set led pin as output
  pinMode(BUILTIN_LED, OUTPUT);
  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  //reset settings - for testing
  if (resetConf)
    wifiManager.resetSettings();

  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);
  //set config save notify callback
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

          strncpy(host_name, json["hostname"], 40);
          strncpy(passcode, json["passcode"], 40);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read

  WiFiManagerParameter custom_hostname("hostname", "Choose a hostname to this IRBlaster", host_name, 40);
  wifiManager.addParameter(&custom_hostname);
  WiFiManagerParameter custom_passcode("passcode", "Choose a passcode", passcode, 40);
  wifiManager.addParameter(&custom_passcode);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect(wifi_config_name)) {
    Serial.println("failed to connect and hit timeout");
    // reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  }

  //if you get here you have connected to the WiFi
  strncpy(host_name, custom_hostname.getValue(), 40);
  strncpy(passcode, custom_passcode.getValue(), 40);
  Serial.println("WiFi connected! User chose hostname '" + String(host_name) + String("' and passcode '") + String(passcode) + "'");

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println(" config...");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["hostname"] = host_name;
    json["passcode"] = passcode;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    Serial.println("");
    json.printTo(configFile);
    configFile.close();
    //end save
  }
  ticker.detach();

  //keep LED on
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
  Serial.println("URL to send commands: http://" + String(host_name) + ".local" + port_str);

  // Configure the server
  server.on("/json", []() { // JSON handler for more complicated IR blaster routines
    Serial.println("Connection received - JSON");

    DynamicJsonBuffer jsonBuffer;
    JsonArray& root = jsonBuffer.parseArray(server.arg("plain"));

    if (!root.success()) {
      Serial.println("JSON parsing failed");
      server.send(400, "text/html", "Failed");
    } else if (server.arg("pass") != passcode) {
      Serial.println("Unauthorized access");
      server.send(401, "text/html", "Unauthorized");
    } else {
      server.send(200, "text/json", "Valid JSON object received, sending sequence");
      for (int x = 0; x < root.size(); x++) {
        String type = root[x]["type"];
        String ip = root[x]["ip"];
        int rdelay = root[x]["rdelay"];
        int pulse = root[x]["pulse"];
        int pdelay = root[x]["pdelay"];
        int repeat = root[x]["repeat"];
        int out = root[x]["out"];

        if (pulse == 0) pulse = 1; // Make sure pulse isn't 0
        if (repeat == 0) repeat = 1; // Make sure repeat isn't 0
        if (pdelay == 0) pdelay = 100; // Default pdelay
        if (rdelay == 0) rdelay = 1000; // Default rdelay

        if (type == "delay") {
          delay(rdelay);
        } else if (type == "raw") {
          JsonArray &raw = root[x]["data"]; // Array of unsigned int values for the raw signal
          int khz = root[x]["khz"];
          if (khz == 0) khz = 38; // Default to 38khz if not set
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
    }
  });

  // Setup simple msg server to mirror version 1.0 functionality
  server.on("/msg", []() {
    Serial.println("Connection received - MSG");
    if (server.arg("pass") != passcode) {
      Serial.println("Unauthorized access");
      server.send(401, "text/html", "Unauthorized");
    } else {
      String type = server.arg("type");
      String data = server.arg("data");
      String ip = server.arg("ip");
      int len = server.arg("length").toInt();
      long address = (server.hasArg("address")) ? server.arg("address").toInt() : 0;
      int rdelay = (server.hasArg("delay")) ? server.arg("rdelay").toInt() : 1000;
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
      server.send(200, "text/html", "code sent");
    }
  });

  server.on("/", []() {
    Serial.println("Connection received");
    server.send(200, "text/html", "Server is running");
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
// Send command to local roku
//
int rokuCommand(String ip, String data) {
  String url = "http://" + ip + ":8060/" + data;
  http.begin(url);
  Serial.println(url);
  Serial.println("Sending roku command");
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
// Display IR code
//
void  ircode (decode_results *results)
{
  // Panasonic has an Address
  if (results->decode_type == PANASONIC) {
    Serial.print(results->panasonicAddress, HEX);
    Serial.print(":");
  }

  // Print Code
  Serial.print(results->value, HEX);
}


//+
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
void  encoding (decode_results *results)
{
  switch (results->decode_type) {
    default:
    case UNKNOWN:      Serial.print("UNKNOWN");       break ;
    case NEC:          Serial.print("NEC");           break ;
    case SONY:         Serial.print("SONY");          break ;
    case RC5:          Serial.print("RC5");           break ;
    case RC6:          Serial.print("RC6");           break ;
    case DISH:         Serial.print("DISH");          break ;
    case SHARP:        Serial.print("SHARP");         break ;
    case JVC:          Serial.print("JVC");           break ;
    case SANYO:        Serial.print("SANYO");         break ;
    case MITSUBISHI:   Serial.print("MITSUBISHI");    break ;
    case SAMSUNG:      Serial.print("SAMSUNG");       break ;
    case LG:           Serial.print("LG");            break ;
    case WHYNTER:      Serial.print("WHYNTER");       break ;
    case PANASONIC:    Serial.print("PANASONIC");     break ;
  }
}


//+=============================================================================
// Single line compact code
//
void fullCode (decode_results *results)
{
  Serial.print("One line: ");
  Serial.print(results->value, HEX);
  Serial.print(":");
  encoding(results);
  Serial.print(":");
  Serial.print(results->bits, DEC);
  Serial.println("");
}


//+=============================================================================
// Dump out the decode_results structure.
//
void dumpInfo (decode_results *results)
{
  // Show Encoding standard
  Serial.print("Encoding  : ");
  encoding(results);
  Serial.println("");

  // Show Code & length
  Serial.print("Code      : ");
  ircode(results);
  Serial.print(" (");
  Serial.print(results->bits, DEC);
  Serial.println(" bits)");
}


//+=============================================================================
// Dump out the decode_results structure.
//
void  dumpRaw (decode_results *results)
{
  // Print Raw data
  Serial.print("Timing[");
  Serial.print(results->rawlen - 1, DEC);
  Serial.println("]: ");

  for (int i = 1;  i < results->rawlen;  i++) {
    unsigned long  x = results->rawbuf[i] * USECPERTICK;
    if (!(i & 1)) {  // even
      Serial.print("-");
      if (x < 1000)  Serial.print(" ") ;
      if (x < 100)   Serial.print(" ") ;
      Serial.print(x, DEC);
    } else {  // odd
      Serial.print("     ");
      Serial.print("+");
      if (x < 1000)  Serial.print(" ") ;
      if (x < 100)   Serial.print(" ") ;
      Serial.print(x, DEC);
      if (i < results->rawlen - 1) Serial.print(", "); //',' not needed for last one
    }
    if (!(i % 8))  Serial.println("");
  }
  Serial.println("");                    // Newline
}


//+=============================================================================
// Dump out the decode_results structure.
//
void  dumpCode (decode_results *results)
{
  // Start declaration
  Serial.println("Raw (code):");
  Serial.print("unsigned int  ");          // variable type
  Serial.print("rawData[");                // array name
  Serial.print(results->rawlen - 1, DEC);  // array size
  Serial.print("] = {");                   // Start declaration

  // Dump data
  for (int i = 1;  i < results->rawlen;  i++) {
    Serial.print(results->rawbuf[i] * USECPERTICK, DEC);
    if ( i < results->rawlen - 1 ) Serial.print(","); // ',' not needed on last one
    if (!(i & 1))  Serial.print(" ");
  }

  // End declaration
  Serial.print("};");  //

  // Comment
  Serial.print("  // ");
  encoding(results);
  Serial.print(" ");
  ircode(results);

  // Newline
  Serial.println("");

  // Now dump "known" codes
  if (results->decode_type != UNKNOWN) {

    // Some protocols have an address
    if (results->decode_type == PANASONIC) {
      Serial.print("unsigned int  addr = 0x");
      Serial.print(results->panasonicAddress, HEX);
      Serial.println(";");
    }

    // All protocols have data
    Serial.print("unsigned int  data = ");
    Serial.print(results->value, HEX);
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
void irblast(String type, String dataStr, int len, int rdelay, int pulse, int pdelay, int repeat, long address, IRsend irsend) {
  Serial.println("Blasting off");
  type.toLowerCase();
  long data = HexToLongInt(dataStr);
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
  unsigned int raw[length * 2];
  unsigned int one_pulse = 3000;
  unsigned int one_break = 1000;
  unsigned int zero_pulse = one_break;
  unsigned int zero_break = one_pulse;

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
    irsend.sendRaw(raw, 15, 38);
    delay(pdelay);
  }
}


void loop() {
  server.handleClient();
  decode_results  results;        // Somewhere to store the results

  if (irrecv.decode(&results)) {  // Grab an IR code
    Serial.println("Signal received:");
     fullCode(&results);           // Print the singleline value
    //dumpInfo(&results);           // Output the results
    //dumpRaw(&results);            // Output the results in RAW format
    dumpCode(&results);           // Output the results as source code
    Serial.println("");           // Blank line between entries
    strncpy(last_codes[last_code_idx], "", 20);
    last_code_idx = (last_code_idx + 1) % 5;
    irrecv.resume();              // Prepare for the next value
  }
}
