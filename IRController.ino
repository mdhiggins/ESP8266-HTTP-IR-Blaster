#include <IRremoteESP8266.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

//+=============================================================================
// Please customize the following settings
//
const char* ssid = "WiFi SSID"; // WiFi SSID
const char* password = "WiFi pass"; // WiFi password
const char* passcode = "pass"; // Access code to send IR commands
const int port = 8081; //  Receiving HTTP port
IPAddress ip(10,0,1,10); // ESP8266 IP Address
IPAddress dns(10,0,1,1); // DNS Server
IPAddress gw(10,0,1,1); // Gateway
IPAddress subnet(255,255,255,0); // Subnet

IRrecv irrecv(5); // Receiving pin
IRsend irsend1(4); // Transmitting preset 1
IRsend irsend2(0); // Transmitting preset 2
IRsend irsend3(12); // Transmitting preset 3
IRsend irsend4(13); // Transmitting preset 4
//
// End configuration area
//+=============================================================================

ESP8266WebServer server(port);

//+=============================================================================
// Setup web server and IR receiver/blaster
//
void setup() {
  // Initialize serial
  Serial.begin(115200);
  Serial.println("ESP8266 IR Controller");

  // Begin WiFi
  WiFi.config(ip, dns, gw, subnet); // Enable to hardcode an IP address
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }

  Serial.println("WiFi connected");
  Serial.println(WiFi.localIP());

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
      int len = server.arg("length").toInt();
      long address = (server.hasArg("address")) ? server.arg("address").toInt():0;
      int rdelay = (server.hasArg("delay")) ? server.arg("rdelay").toInt():1000;
      int pulse = (server.hasArg("pulse")) ? server.arg("pulse").toInt():1;
      int pdelay = (server.hasArg("pdelay")) ? server.arg("pdelay").toInt():100;
      int repeat = (server.hasArg("repeat")) ? server.arg("repeat").toInt():1;
      int out = (server.hasArg("out")) ? server.arg("out").toInt():0;
      if (server.hasArg("code")) {
        String code = server.arg("code");
        char separator = ':';
        data = getValue(code, separator, 0);
        type = getValue(code, separator, 1);
        len = getValue(code, separator, 2).toInt();
      }

      irblast(type, data, len, rdelay, pulse, pdelay, repeat, address, pickIRsend(out));
      server.send(200, "text/html", "Sending code");
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
// Split string by character
//
String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length()-1;

  for(int i=0; i<=maxIndex && found<=index; i++){
    if(data.charAt(i)==separator || i==maxIndex){
        found++;
        strIndex[0] = strIndex[1]+1;
        strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }

  return found>index ? data.substring(strIndex[0], strIndex[1]) : "";
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
void  dumpInfo (decode_results *results)
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
  Serial.print(results->rawlen-1, DEC);
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
      if (i < results->rawlen-1) Serial.print(", "); //',' not needed for last one
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
  Serial.print("unsigned int  ");          // variable type
  Serial.print("rawData[");                // array name
  Serial.print(results->rawlen - 1, DEC);  // array size
  Serial.print("] = {");                   // Start declaration

  // Dump data
  for (int i = 1;  i < results->rawlen;  i++) {
    Serial.print(results->rawbuf[i] * USECPERTICK, DEC);
    if ( i < results->rawlen-1 ) Serial.print(","); // ',' not needed on last one
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
  for (i = h.length() -1; i >=0 ; i--)
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
          irsend.rc5(data, len);
      } else if (type == "rc6") {
          irsend.rc6(data, len);
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
  unsigned int raw[length*2];
  unsigned int one_pulse = 3000;
  unsigned int one_break = 1000;
  unsigned int zero_pulse = one_break;
  unsigned int zero_break = one_pulse;

  int arrayposition = 0;
  for (int counter = length-1; counter >= 0; --counter) {
    if(code & (1<<counter)) {
      raw[arrayposition] = one_pulse;
      raw[arrayposition+1] = one_break;
    }
    else {
      raw[arrayposition] = zero_pulse;
      raw[arrayposition+1] = zero_break;
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
    fullCode(&results);           // Print the singleline value
    //dumpInfo(&results);           // Output the results
    //dumpRaw(&results);            // Output the results in RAW format
    dumpCode(&results);           // Output the results as source code
    Serial.println("");           // Blank line between entries
    irrecv.resume();              // Prepare for the next value
  }
}
