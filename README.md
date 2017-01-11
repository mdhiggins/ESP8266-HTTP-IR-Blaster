# ESP8266-HTTP-IR-Blaster V2
==============
ESP8266 Compatible IR Blaster that accepts HTTP commands for use with services like Amazon Echo


The purpose of this project was to create a Wi-Fi enabled IR blaster that could be controlled with Amazon Alexa and IFTTT
This program uses the ESP8266 board and the ESP8266Basic firmware to achieve these goals with minimal coding overhead

Version 2 of the project aimed to increase the reliability and expand the functionality to include many different types of devices
 in addition to transmitting raw formatted codes and more complex combination of codes

Hardware
--------------
*This is just example hardware that I used in creation of this project, other hardware is likely to work*
- ESP8266 NodeMCU Board https://www.amazon.com/gp/product/B01IK9GEQG/
- IR Led & Receiver https://www.amazon.com/gp/product/B00EFOQEUM/
- 2N2222 Transistor

Setup
--------------
1. Install Arduino IDE
2. Install [Arduino Core](https://github.com/esp8266/Arduino)
3. Install the following libraries from the Arduino IDE library manager: `ESP8266WebServer` `ESP8266WiFi` `ArduinoJson`
4. Manually install the [IRremoteESP8266 library](https://github.com/markszabo/IRremoteESP8266)
5. Load the IRController.ino blueprint
6. Customize your WiFi settings, ports, IP address, and passcode at the top of the blueprint
7. Load code on to your ESP8266. Monitor via serial at 115200 baud rate
8. Forward whichever port your ESP8266 web server is running on so that it can be accessed from outside your local network
9. Create an [IFTTT trigger](https://cloud.githubusercontent.com/assets/3608298/21053641/52b2131c-bdf8-11e6-931e-89e80e932d8a.PNG) using the Maker channel using the URL format below

Capturing Codes
---------------
While connected to the ESP via USB and monitoring via serial at 115200 baud, scan your remote code you wish to emulate. Most codes will be recognized and displayed in the format `A90:SONY:12`. Make a note of this code as you will need it for your maker channel URL. If your code is not recognized scroll down the JSON section of this readme.

Simple URL
--------------
For sending simple commands such as a single button press, or a repeating sequence of the same button press, use the logic below. This is unchanged from version 1.
Parameters
- `pass` - password required to execute IR command sending
- `code` - IR code such as `A90:SONY:12`
- `pulse` - (optional) specifies repeating the signal a number of times (pulses). Some TVs require a few pulses for the signal to be picked up
- `pdelay` - (optional) pulse delay in milliseconds. Default 100ms
- `repeat` - (optional) specifies longer repeats to simulate pressing the remote button multiple times. Useful for emulating things like the sleep timer
- `rdelay` - (optional) time to delay sending the signal again. Default 1000ms
Example:
`http://xxx.xxx.xxx.xxx/msg?code=A90:SONY:12&pulse=2&repeat=5`

JSON
--------------
For more complicated sequences of buttons, such a multiple button presses or sending RAW IR commands, you pay do an HTTP POST with a JSON object that contains an array of commands which the receiver will parse and transmit. Payload must be a JSON array of JSON objects.
Parameters
- `data` - IR code data, may be simple HEX code such as `"A90"` or an array of int values when transmitting a RAW sequence
- `type` - Type of signal transmitted. Example `"SONY"`, `"RAW"`, `Delay` or `"Roomba"` (and many others)
- `length` - Bit length, example `12`
- `pulse` - Repeat a signal rapidly. Example `2`. Sony based codes will not be recognized unless pulsed at least twice.
- `pdelay` - Delay between pulses in milliseconds. Default 100
- `repeat` - Number of times to send the signal. Useful for emulating multiple button presses for functions like large volume adjustments or sleep timer
- `rdelay` - Delay between repeats
- `khz` - Transmission frequency in kilohertz. Only needed when transmitting RAW signal. Default `38`

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

JSON and IFTTT
--------------
While the JSON functionality works fine with a command line based HTTP request using CURL, IFTTT's maker channel is not as robust.
To send the signal using the IFTTT Maker channel, simply take your JSON payload and remove spaces and line breaks so that entire packet is on a single line, then added it to the URL using the `plain` argument.

Sample URL
```
http://xxx.xxx.xxx.xxx/msg?pass=yourpass&plain=[{"type":"nec","data":"FF827D","length":32,"repeat":3,"rdelay":800}]
```
