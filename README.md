# aims-upsmon-client
Arduino / ESP8266 AIMS inverter charger monitoring client

Tested on: [AIMS Power 1250 Watt POWER INVERTER CHARGER - PICOGLF12W12V120AL](https://www.aimscorp.net/1250-watt-low-frequency-pure-sine-inverter-charger-12-vdc-to-120-vac.html)

used with: [ESP8266 WiFi Kit 8](http://www.heltec.cn/project/wifi-kit-8/?lang=en)

For use with upsmon logging server. (Server code yet to be released.  Build your own if you're in a hurry.)

Connecting: 
```
Wifi Kit 8 / ESP8266 <---> AIMS Inverter/Charger RJ45 "LCD" port
--------------------       -------------------------------------  
                     <---> Pin 1 (Not Used) 
GND                  <---> Pin 2 (GND)
                     <---> Pin 3 (SW1 - POWER ON)
**5V                 <---> Pin 4 (+5V)
                     <---> Pin 5 (SW2 - BAT-V)
GPIO15 RTS0 (TX swp) <---> Pin 6 (TTL COMM RXD)
                     <---> Pin 7 (SW1 - POWER SAVE ON)
GPIO13 CTS0 (RX swp) <---> Pin 8 (TTL COMM TXD)

** If your ESP8266 kit doesn't have a 5v to 3.3v converter, you'll need to step 
   5v down to 3.3v for powering the ESP8266, else you'll release its magic smoke.
** TXD fom AIMS Inverter/Charger is 5v, but the GPIO pins on ESP8266 are okay with 5v.  
   AIMS' RXD will work on 3.3v output from ESP8266.
** DON'T connect kit to AIMS' 5v supply while kit is plugged in to USB port!!!
```
