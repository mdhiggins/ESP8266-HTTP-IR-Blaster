# ESP8266-HTTP-IR-Blaster
==============
ESP8266 Compatible IR Blaster that accepts HTTP commands for use with services like Amazon Echo


The purpose of this project was to create a Wi-Fi enabled IR blaster that could be controlled with Amazon Alexa and IFTTT
This program uses the ESP8266 board and the ESP8266Basic firmware to achieve these goals with minimal coding overhead

This project is based off of the work by @mmiscool posted here
http://www.instructables.com/id/Easiest-ESP8266-Learning-IR-Remote-Control-Via-WIF/
Special thanks to him for his hard work on ESP8266Basic and the tutorial for the IR blaster

See his readme for basic instructions

**There is a long step by step walkthrough on the wiki of this repository**

Hardware
--------------
*This is just example hardware that I used in creation of this project, other hardware is likely to work*
- ESP8266 NodeMCU Board https://www.amazon.com/gp/product/B01IK9GEQG/
- IR Led & Receiver https://www.amazon.com/gp/product/B00EFOQEUM/

Setup
--------------
1. Flash your ESP8266 board with the ESP8266Basic 3.0 (for the board I used I had to make sure to set the baud rate to 9600)
2. Add the full text of default.bas to your code. Make sure to update line 4 to set your password `pword = "password"`
3. Wire up the ESP8266 using this image as your guide
![fwdyrhrittd0ofg](https://cloud.githubusercontent.com/assets/3608298/21053650/64b46402-bdf8-11e6-8198-a8f73761b194.png)
4. Save your IR codes to button 1-6 as needed using the IR receiver
5. Forward whichever port your ESP8266 web server is running on so that it can be accessed from outside your local network
6. Create an [IFTTT trigger](https://cloud.githubusercontent.com/assets/3608298/21053641/52b2131c-bdf8-11e6-931e-89e80e932d8a.PNG) using the Maker channel using the URL format below

URL
--------------
Parameters
- `pass` - password required to execute IR command sending
- `code` - value can be 1-6 and corresponds to the slot you saved your remove code under or the raw IR code such as `a90:SONY:19`
- `pulse` - (optional) specifies repeating the signal a number of times (pulses). Some TVs require a few pulses for the signal to be picked up
- `pdelay` - (optional) pulse delay in milliseconds. Default 10ms
- `repeat` - (optional) specifies longer repeats to simulate pressing the remote button multiple times. Useful for emulating things like the sleep timer
- `rdelay` - (optional) time to delay sending the signal again. Default 1000ms
Example I used to set a 30 minute sleep timer on my TV:
`http://xxx.xxx.xxx.xxx/msg?code=2&pulse=3&repeat=5`

Flashing your ESP8266
---------------
A good starting resource is to look at the ESP8266Basic website https://www.esp8266basic.com/flashing-instructions.html
I ran into some issues using the Windows application to flash the firmware (likely baud rate issue). I solved this by downloading the ESP8266Basic firmware from [Github](https://github.com/esp8266/Basic/tree/NewWebSockets/Flasher/Build/4M) and then flashing the board using the [NodeMCU-flasher software](https://github.com/nodemcu/nodemcu-flasher)

Requires firmware version 3.0

Notes
---------------
If you're using an IR Led with high level of current you may want to add a resistor to the circuit so as to not damage your ESP8266 over time. The pins are only rated for 12mA of current
The IR led I'm using in my setup draws 20mA of current but since its only on for small impulses rather than sustained like a typical LED I didn't bother with a resistor but its probably not the technically correct thing to do.
