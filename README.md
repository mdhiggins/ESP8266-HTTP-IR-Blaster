# ESP8266-HTTP-IR-Blaster V3

ESP8266 Compatible IR Blaster that accepts HTTP commands for use with services like Amazon Echo

<img width="400" src="https://cloud.githubusercontent.com/assets/3608298/21854472/e2b3d824-d7e8-11e6-8439-a500b73fd57e.jpg">

The purpose of this project was to create a Wi-Fi enabled IR blaster that could be controlled with Amazon Alexa and IFTTT
This program uses the ESP8266 board and the ESP8266Basic firmware to achieve these goals with minimal coding overhead

Version 3 drastically improves the user interface and removes the need to check scanned IR codes over serial, this can now all be accessed through the web portal. Version 3 also includes further optimizations, stability improvements, and LED feedback. Version 3 additionally makes use of the new version of the IRremoteESP8266 library

Supported Signals
--------------
- NEC
- Sony
- Panasonic
- JVC
- Samsung
- Sharp
- Coolix
- Dish
- Wynter
- Roomba
- RC5/RC6
- RAW

Hardware
--------------
V3/V2 of the hardware includes the 2N2222 transistor for increased current and pulls directly off the USB port via the VIN pin (5V supply) to increase the overall current delivery to the IR LED to improve brightness and range. Appropriate current limiting resistors are also shown. V1 hardware still works with the new code but V2 is recommended for better performance and prolonged lifespan of your ESP8266 and LED.

![irblaster](https://user-images.githubusercontent.com/3608298/30983611-f258c95a-a458-11e7-99c5-ba088727c928.PNG)

- [ESP8266 NodeMCU Board](https://www.amazon.com/gp/product/B01IK9GEQG/)
- [IR Receiver](https://www.amazon.com/gp/product/B00EFOQEUM/)
- [Super bright IR Led](https://www.amazon.com/gp/product/B00ULB0U44/)
- [2N2222 Transistor](https://www.amazon.com/gp/product/B00R1M3DA4/)
- [Resistors](https://www.amazon.com/gp/product/B00YX75O5M/)

*These are just quick Amazon references. Parts can likely be purchased cheaper elsewhere*

Drivers
--------------
Install the NodeMCU drivers for your respective operating system if they are not autodetected

https://www.silabs.com/products/mcu/Pages/USBtoUARTBridgeVCPDrivers.aspx

Setup
--------------
1. Install [Arduino IDE](https://www.arduino.cc/en/main/software)
2. Install [ESP8266 Arduino Core](https://github.com/esp8266/Arduino)
3. Install the following libraries from the Arduino IDE [Library Manager](https://www.arduino.cc/en/Guide/Libraries): `ESP8266WebServer` `ESP8266WiFi` `ArduinoJson` `WiFiManager` `NTPClient` `IRremoteESP8266`
4. Load the `IRController.ino` blueprint from this repository
5. Upload blueprint to your ESP8266. Monitor via serial at 115200 baud rate
6. Device will boot into WiFi access point mode initially with SSID `IRBlaster Configuration`, IP address `192.168.4.1`. Connect to this and configure your access point settings using WiFi Manager
7. Forward whichever port your ESP8266 web server is running on so that it can be accessed from outside your local network
8. If your router supports mDNS/Bonjour you can now access your device on your local network via the hostname you specified (`http://hostname.local:port/`)
9. Create an [IFTTT trigger](https://cloud.githubusercontent.com/assets/3608298/21918439/526b6ba0-d91f-11e6-9ef2-dcc8e41f7637.png) using the Maker channel using the URL format below. Make sure you use your external IP address and not your local IP address or local hostname

Server Info
---------------
<img width="250" src="https://user-images.githubusercontent.com/3608298/27726397-5a6f6d62-5d48-11e7-886b-1af2007d47b5.png"><img width="250" src="https://user-images.githubusercontent.com/3608298/27726396-5a6dd9f2-5d48-11e7-967f-4d76ecf479d4.png">

You may access basic device information at `http://xxx.xxx.xxx.xxx:port/` (webroot)

mDNS
---------------
mDNS/Bonjour service configured on port 80

Capturing Codes
---------------
Your last scanned code can be accessed via web at `http://xxx.xxx.xxx.xxx:port/` or via serial monitoring over USB at 115200 baud. Most codes will be recognized and displayed in the format `A90:SONY:12`. Make a note of the code displayed in the serial output as you will need it for your maker channel URL. If your code is not recognized scroll down the JSON section of this read me.

Basic Output
--------------
For sending simple commands such as a single button press, or a repeating sequence of the same button press, use the logic below. This is unchanged from version 1.
Parameters
- `pass` - password required to execute IR command sending
- `code` - IR code such as `A90:SONY:12`
- `pulse` - (optional) Repeat a signal rapidly. Default `1`
- `pdelay` - (optional) Delay between pulses in milliseconds. Default `100`
- `repeat` - (optional) Number of times to send the signal. Default `1`. Useful for emulating multiple button presses for functions like large volume adjustments or sleep timer
- `rdelay` - (optional) Delay between repeats in milliseconds. Default `1000`
- `out` - (optional) Set which IRsend present to transmit over. Default `1`. Choose between `1-4`. Corresponding output pins set in the blueprint. Useful for a single ESP8266 that needs multiple LEDs pointed in different directions to trigger different devices

Example:
`http://xxx.xxx.xxx.xxx:port/msg?code=A90:SONY:12&pulse=2&repeat=5&pass=yourpass`

JSON Scripting
--------------
For more complicated sequences of buttons, such a multiple button presses or sending RAW IR commands, you may do an HTTP POST with a JSON object that contains an array of commands which the receiver will parse and transmit. Payload must be a JSON array of JSON objects. Password should still be specified as the URL parameter `pass`.

Parameters
- `data` - IR code data, may be simple HEX code such as `"A90"` or an array of int values when transmitting a RAW sequence
- `type` - Type of signal transmitted. Example `"SONY"`, `"RAW"`, `"Delay"` or `"Roomba"` (and many others)
- `length` - (conditional) Bit length, example `12`. *Parameter does not need to be specified for RAW or Roomba signals*
- `pulse` - (optional) Repeat a signal rapidly. Default `1`
- `pdelay` - (optional) Delay between pulses in milliseconds. Default `100`
- `repeat` - (optional) Number of times to send the signal. Default `1`. *Useful for emulating multiple button presses for functions like large volume adjustments or sleep timer*
- `rdelay` - (optional) Delay between repeats in milliseconds. Default `1000`
- `khz` - (conditional) Transmission frequency in kilohertz. Default `38`. *Only required when transmitting RAW signal*
- `out` - (optional) Set which IRsend present to transmit over. Default `1`. Choose between `1-4`. Corresponding output pins set in the blueprint. Useful for a single ESP8266 that needs multiple LEDs pointed in different directions to trigger different devices.

3 Button Sequence Example JSON
```
[
    {
        "type":"nec",
        "data":"FF827D",
        "length":32,
        "repeat":3,
        "rdelay":800
    },
    {
        "type":"nec",
        "data":"FFA25D",
        "length":32,
        "repeat":3,
        "rdelay":800
    },
    {
        "type":"nec",
        "data":"FF12ED",
        "length":32,
        "rdelay": 1000
    }
]
```

Raw Example
```
[
    {
    "type":"raw",
    "data":[2450,600, 1300,600, 700,550, 1300,600, 700,550, 1300,550, 700,550, 700,600, 1300,600, 700,550, 700,550, 700,550, 700],
    "khz":38,
    "pulse":3
    }
]
```

Multiple LED Setup
--------------
If  you are setting up your ESP8266 IR Controller to handle multiple devices, for example in a home theater setup, and the IR receivers are in different directions, you may use the `out` parameter to transmit codes with different LEDs which can be arranged to face different directions. Simply wire additional LEDs to a different GPIO pin on the ESP8266 in a similar fashion to the default transmitting pin and set the corresponding pin to the `irsend1-4` objects created at the top of the blueprint. For example if you wired an additional LED to the GPIO0 pin and you wanted to send a signal via that LED instead of the primary, you would modify irsend2 in the blueprint to `IRsend irsend2(0)` corresponding to the GPIO pin. Then when sending your signal via the url simply add `&out=2` and the signal will be sent via irsend2 instead of the primary irsend1.

Default mapping
- irsend1: GPIO4
- irsend2: GPIO5
- irsend3: GPIO12
- irsend4: GPIO13
- irrecv: GPIO14
- config: GPIO15

Force WiFi Reconfiguration
---------------
Set GPIO15 to ground to force a WiFi configuration reset

Minimal Output
---------------
For configuring URLs to work with IFTTT or other automation services where the HTML output of the device will never be seen by a human, add `&simple=1` to the URL to simplify the data sent and speed up the loading process

Example:
`http://xxx.xxx.xxx.xxx:port/msg?code=A90:SONY:12&pulse=2&repeat=5&pass=yourpass&simple=1`

JSON and IFTTT
--------------
While the JSON functionality works fine with a command line based HTTP request like CURL, IFTTT's maker channel is not as robust.
To send the signal using the IFTTT Maker channel, simply take your JSON payload and remove spaces and line breaks so that entire packet is on a single line, then added it to the URL using the `plain` argument.

Sample URL using the same 3 button JSON sequence as above
```
http://xxx.xxx.xxx.xxx:port/json?pass=yourpass&plain=[{"type":"nec","data":"FF827D","length":32,"repeat":3,"rdelay":800},{"type":"nec","data":"FFA25D","length":32,"repeat":3,"rdelay":800},{"type":"nec","data":"FF12ED","length":32,"rdelay":1000}]
```

Smartthings
--------------
For a great write up and instructional on how to integrate this device with Smartthings please visit http://thingsthataresmart.wiki/index.php?title=How_to_Control_your_TV_through_Alexa_and_Smartthings

Thanks to @dham102

Roku
--------------
The Roku device supports sending commands via an API to simulate remote button presses over HTTP, but only allows connections via a local IP address. This blueprint supports sending these commands and acts as a bridge between IFTTT/Alexa to control the Roku with basic commands

Roku commands require 3 parameters that can be sent as a Simple URL or part of a JSON collection. Parameters include:
- `data` - [Roku code](https://sdkdocs.roku.com/display/sdkdoc/External+Control+Guide)
- `type` - Type of signal transmitted. Must be set to `roku`
- `ip` - Local IP address of your Roku device

Example Roku command to simulate pressing play button on a Roku with local IP `10.0.1.3`
```
http://xxx.xxx.xxx.xxx:port/msg?pass=yourpass&type=roku&data=keypress/play&ip=10.0.1.3
```
