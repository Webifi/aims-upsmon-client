/**
 * Monitor Aims UPS (Inverter / Charger) stats and POST to specified server.
 * Tested on:
 * https://www.aimscorp.net/1250-watt-low-frequency-pure-sine-inverter-charger-12-vdc-to-120-vac.html
 * used with:
 * http://www.heltec.cn/project/wifi-kit-8/?lang=en
 * 
 * Connect: 
 * Wifi Kit 8 / ESP8266 <---> Aims Inverter/Charger RJ45 "LCD" port
 * --------------------       -------------------------------------  
 *                      <---> Pin 1 (Not Used) 
 * GND                  <---> Pin 2 (GND)
 *                      <---> Pin 3 (SW1 - POWER ON)
 * **5V                 <---> Pin 4 (+5V)
 *                      <---> Pin 5 (SW2 - BAT-V)
 * GPIO15 RTS0 (TX swp) <---> Pin 6 (TTL COMM RXD)
 *                      <---> Pin 7 (SW1 - POWER SAVE ON)
 * GPIO13 CTS0 (RX swp) <---> Pin 8 (TTL COMM TXD)
 * 
 * ** If your ESP8266 kit doesn't have a 5v to 3.3v converter, you'll need to step 
 *    5v down to 3.3v for powering the ESP8266, else you'll release its magic smoke.
 * ** TXD fom Aims Inverter/Charger is 5v, but the GPIO pins on ESP8266 are okay with 5v.  
 *    Aims' RXD will work on 3.3v output from ESP8266.
 * ** DON'T connect kit to Aims' 5v supply while kit is plugged in to USB port!!!
 */

#include <ESP8266WiFi.h>
#include <WiFiClient.h> 
#include <ESP8266HTTPClient.h>
#include <U8g2lib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
 
/* Set these to your desired credentials. */
const char *ssid = "YOUR_SSID";  // Set to your Wifi SSID
const char *password = "your-wifi-password"; // Set to your Wifi password
const char *clientCode = "upsmon-client-code"; // Set to the "client code" you created on your upsmon logging server
const String monitorServer = "http://your.upsmonserver.tld/upsmon.php"; // Set to the address of your upsmon logging server

// For some Wifi Kit 8's
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ 16, /* clock=*/ 5, /* data=*/ 4);
// For other wifi Kit 8's.  If you don't get output on your OLED display, use this.
// U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ 4, /* clock=*/ 14, /* data=*/ 2);

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 1800 * 1000);

String serialData = "";         // a String to hold incoming data

String rawQ1 = "";
boolean hasQ1 = false;
boolean didQ1 = false;
String inputVoltage = "0";
String inputFaultVoltage = "0";
String outputVoltage = "0";
String outputCurrent = "0";
String outputFrequency = "0";
String batteryVoltage = "0";
String batteryTemp = "0";
boolean utilityFail = false; // bit 7
boolean batteryLow = false; // bit 6
boolean isAvr = false; // bit 5  false = NORMAL, true = AVR
boolean upsFailed = false; // bit 4
boolean isLinenteractive = false; // bit 3 false = On_Line, true = line interactive
boolean testInProgress = false; // bit 2
boolean shutdownActive = false; // bit 1
boolean beeperOn = false; // bit 0

String rawD = "";
boolean hasD = false;
boolean didD = false;
boolean isCharging = false;

boolean triggerStateChange = false;

String rawF = "";
boolean hasF = false;
String ratingVoltage = "0";
String ratingCurrent = "0";
String ratingBatteryVoltage = "0";
String ratingFrequency = "0";

String unknownResp = "";
String encodedString = "";

WiFiEventHandler gotIpEventHandler, disconnectedEventHandler;

U8G2LOG u8g2log;
// assume 4x6 font
#define U8LOG_WIDTH 32
#define U8LOG_HEIGHT 5
uint8_t u8log_buffer[U8LOG_WIDTH*U8LOG_HEIGHT];
uint16_t barGlyfs[] = {0xe0b3, 0x0258, 0x0259, 0x025a, 0x025b, 0x025c};
unsigned long lastRssiCheck = 0;
unsigned long rssiInterval = 5 * 1000; // Check rssi (wifi strength) every 5 seconds
unsigned long lastStateCheck = 0;
unsigned long stateInterval = 500; // Check UPS state every 1/2 second
unsigned long lastStateSend = 0;
unsigned long sendInterval = 300 * 1000; // Send UPS state every 5 min, or unless state change
unsigned long waitForResponseUntil = 0;
unsigned long waitForResponseInterval = 10000; // Wait 10 seconds for UPS response.
long lastRssi = -100;
int readFails = 0;

int get_bars (long rssi) {
  if (rssi > -55) { 
    return 5;
  } else if (rssi < -55 & rssi > -65) {
    return 4;
  } else if (rssi < -65 & rssi > -70) {
    return 3;
  } else if (rssi < -70 & rssi > -78) {
    return 2;
  } else if (rssi < -78 & rssi > -82) {
    return 1;
  } else {
    return 0;
  }
}
 
 
//=======================================================================
//                    Power on setup
//=======================================================================
 
void setup() {
  
  Serial.begin(2400); // Aims UPS uses 2400bps, 8N1.
  Serial.swap(); // Swap UART0 RX (GPIO3) / TX (GPIO1) to GPIO13 / GPIO15 
                 // to avoid ESP8266 startup output hitting UPS and interference from kit's USB interface.
  Serial.setTimeout(5000);
  
  u8g2.begin();
  serialData.reserve(200);
  unknownResp.reserve(200);
  encodedString.reserve(400);
  
  u8g2.setFont(u8g2_font_tom_thumb_4x6_mf); // set the font for the terminal window
  u8g2log.begin(u8g2, U8LOG_WIDTH, U8LOG_HEIGHT, u8log_buffer);
  u8g2log.setLineHeightOffset(0); // set extra space between lines in pixel, this can be negative
  u8g2log.setRedrawMode(0);   // 0: Update screen with newline, 1: Update screen for every char

  u8g2log.print("UPSMON v0.1\n");

  gotIpEventHandler = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP& event)
  {
    u8g2log.print(ssid);
    u8g2log.print(" connected, IP: ");
    u8g2log.print(WiFi.localIP().toString() + "\n");
  });

  disconnectedEventHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& event)
  {
    u8g2log.print(ssid);
    u8g2log.print(" disconnected\n");
  });
  
  setup_wifi();

}

void setup_wifi() {
  delay(1000);
  WiFi.mode(WIFI_OFF);        //Prevents reconnection issue (taking too long to connect)
  delay(1000);
  WiFi.mode(WIFI_STA);        //This line hides the viewing of ESP as wifi hotspot
  
  WiFi.begin(ssid, password);     //Connect to your WiFi router 
 
  u8g2log.print("Connecting to ");
  u8g2log.print(ssid);
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    u8g2log.print(".");
  }
  timeClient.begin();
  timeClient.update();
}


boolean getAims_Q1(String sValue) {
  
  // Serial.print("Q1\r");
  //           1         2         3         4
  // 0123456789012345678901234567890123456789012345
  // (208.4 140.0 208.4 034 59.9 2.05 35.0 00110000   //46 char
  
  rawQ1 = sValue;
  if (rawQ1.length() != 46) {
    // u8g2log.print("BAD Q1: " + rawQ1 + "\n");
    hasQ1 = false;
    return false;
  }

  boolean wasBatteryLow = batteryLow;
  boolean wasShutdownActive = shutdownActive;
  boolean wasUtilityFail = utilityFail;
  
  inputVoltage = rawQ1.substring(1,6);
  inputFaultVoltage = rawQ1.substring(7,12);
  outputVoltage = rawQ1.substring(13,18);
  outputCurrent = rawQ1.substring(19,22);
  outputFrequency = rawQ1.substring(23,27);
  batteryVoltage = rawQ1.substring(28,32);
  batteryTemp = rawQ1.substring(33,37);
  utilityFail = rawQ1.substring(38,39) == "1"; // bit 7
  batteryLow = rawQ1.substring(39,40) == "1"; // bit 6
  isAvr = rawQ1.substring(40,41) == "1"; // bit 5  false = NORMAL, true = AVR
  upsFailed = rawQ1.substring(41,42) == "1"; // bit 4
  isLinenteractive = rawQ1.substring(42,43) == "1"; // bit 3 false = On_Line, true = line interactive
  testInProgress = rawQ1.substring(43,44) == "1"; // bit 2
  shutdownActive = rawQ1.substring(44,45) == "1"; // bit 1
  beeperOn = rawQ1.substring(45,46) == "1"; // bit 0

  triggerStateChange = triggerStateChange || 
      wasBatteryLow != batteryLow ||
      wasShutdownActive != shutdownActive ||
      wasUtilityFail != utilityFail;

  hasQ1 = true;
  didQ1 = true;
  readFails = 0;
  return true;
  
}

boolean getAims_F(String sValue) {

  // Serial.print("F\r");
  //           1         2
  // 012345678901234567890
  // #120.0 050 12.00 60.0   //21 char
  
  rawF = sValue;
  if (rawF.length() != 21) {
    // u8g2log.print("BAD F: " + rawF + "\n");
    return false;
    hasF = false;
  }

  hasF = true;
  readFails = 0;

  ratingVoltage = rawF.substring(1,6);
  ratingCurrent = rawF.substring(7,10);
  ratingBatteryVoltage = rawF.substring(11,16);
  ratingFrequency = rawF.substring(17,21);

  return true;
  
}

boolean getAims_D(String sValue) {
  // Doesn't seem to work on PICOGLF12W12V120AL
  // https://www.aimscorp.net/1250-watt-low-frequency-pure-sine-inverter-charger-12-vdc-to-120-vac.html
  // Serial.print("D\r"); 
  
  // ACK == charging
  // NAK == not charging
  
  rawD = sValue;
  if (rawD == "ACK") {
    hasD = true;
    didD = true;
    triggerStateChange = triggerStateChange || !isCharging;
    isCharging = true;
    readFails = 0;
    return true;
  } else if (rawD == "NAK") {
    hasD = true;
    didD = true;
    triggerStateChange = triggerStateChange || isCharging;
    isCharging = false;
    readFails = 0;
    return true;
  } else {
    hasD = false;
    isCharging = false;
    return false;
  }
  
}

String urlencode(String str)
{
    encodedString = "";
    char c;
    char code0;
    char code1;
    // char code2;
    for (int i =0; i < str.length(); i++){
      c=str.charAt(i);
      if (c == ' '){
        encodedString+= '+';
      } else if (isalnum(c)){
        encodedString+=c;
      } else{
        code1=(c & 0xf)+'0';
        if ((c & 0xf) >9){
            code1=(c & 0xf) - 10 + 'A';
        }
        c=(c>>4)&0xf;
        code0=c+'0';
        if (c > 9){
            code0=c - 10 + 'A';
        }
        // code2='\0';
        encodedString+='%';
        encodedString+=code0;
        encodedString+=code1;
        //encodedString+=code2;
      }
      // yield();
    }
    return encodedString;
    
}

void sendDataToServer () {

    lastStateSend = millis();

    u8g2log.print("POSTING DATA:\n");

    HTTPClient http;    //Declare object of class HTTPClient
   
    String clientCodeString, postData1;
    String postData2 = "";
    String postData3 = "";
    clientCodeString = String(clientCode);   //String to interger conversion
   
    //Post Data
    
    postData1 = "c=" + clientCodeString 
      + "&ep=" + String(timeClient.getEpochTime()) 
      + "&rssi=" + String(lastRssi) 
      + "&raw=" + urlencode(rawQ1) + ";" + urlencode(rawF) + ";" + urlencode(unknownResp);
    
    if (hasF) {
      postData2 =
        ("&rv=" + ratingVoltage
        + "&rc=" + ratingCurrent
        + "&rbv=" + ratingBatteryVoltage
        + "&rf=" + ratingFrequency);
    }
      
    if (hasQ1) {
      postData3 =
        ("&iv=" + inputVoltage
        + "&ifv=" + inputFaultVoltage
        + "&ov=" + outputVoltage
        + "&oc=" + outputCurrent
        + "&of=" + outputFrequency
        + "&bv=" + batteryVoltage
        + "&bt=" + batteryTemp
        + "&pf=" + (utilityFail ? "1" : "0")
        + "&uf=" + (upsFailed ? "1" : "0")
        + "&lb=" + (batteryLow ? "1" : "0")
        + "&al=" + (beeperOn ? "1" : "0")
        + "&avr=" + (isAvr ? "1" : "0")
        + "&tst=" + (testInProgress ? "1" : "0")
        + "&dwn=" + (shutdownActive ? "1" : "0"));
    }
    
    // u8g2log.print("POSTING DATA (BARS: " + String(bars) + ")\n");
    http.begin(monitorServer);              //Specify request destination
    // http.setTimeout(10000);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");    //Specify content-type header
    //
    int httpCode = http.POST(postData1 + postData2 + postData3);   //Send the request
    String payload = http.getString();    //Get the response payload
   
    u8g2log.print(httpCode);   //Print HTTP return code
    u8g2log.print(": ");
    u8g2log.print(payload);    //Print request response payload

    // Mark that we sent this so we don't do it again
    hasQ1 = false;
    rawQ1 = "";
    readFails = 0;
    unknownResp = "";
    triggerStateChange = false;
   
    http.end();  //Close connection
  
}

void checkSerialData() {
  while (Serial.available()) {
    char inChar = (char)Serial.read(); // Input from GPIO13
    if (inChar == '\n' || inChar == '\r') {
      // u8g2log.print("\n");
      if (serialData == "") {
        // Nothing to do...
      } else if(serialData.startsWith("(") || serialData.length() == 46) {
        u8g2log.print("LOADING Q1\n");
        getAims_Q1(serialData);
      } else if(serialData.startsWith("#")) {
        u8g2log.print("LOADING F\n");
        getAims_F(serialData);
      } else if(serialData == "ACK" || serialData == "NAK") {
        u8g2log.print("LOADING D\n");
        getAims_D(serialData);
      } else {
        u8g2log.print("UNKNOWN: " + String(serialData.length()) +  "\n");
        u8g2log.print(serialData + "\n");
        if (unknownResp.length() > 0) {
          unknownResp += "\n";
        }
        if (unknownResp.length() < 200) {
          unknownResp += serialData;
        }
      }
      waitForResponseUntil = millis();
      serialData = "";
    } else {
      // u8g2log.print(String(inChar));
      serialData += inChar;
    }
  }
}

void sendSerialData(String sValue) {
  Serial.print(sValue); // Output on GPIO15
}

void loop() {

  unsigned long currentTime = millis();
  checkSerialData();

  if (!hasF && (waitForResponseUntil == 0 || currentTime >= waitForResponseUntil)) {
    if (currentTime >= waitForResponseUntil) {
      readFails ++;
    }
    waitForResponseUntil = currentTime + waitForResponseInterval;
    u8g2log.print("QUERY F\n");
    sendSerialData("F\r");
  } else if (!hasQ1 && !didQ1 && (waitForResponseUntil == 0 || currentTime >= waitForResponseUntil)) {
    if (currentTime >= waitForResponseUntil) {
      readFails ++;
    }
    waitForResponseUntil = currentTime + waitForResponseInterval;
    u8g2log.print("QUERY Q1\n");
    sendSerialData("Q1\r");
  } /* else if (!hasD && !didD && (waitForResponseUntil == 0 || currentTime >= waitForResponseUntil)) {
    if (currentTime >= waitForResponseUntil) {
      readFails ++;
    }
    waitForResponseUntil = currentTime + waitForResponseInterval;
    u8g2log.print("QUERY D\n");
    sendSerialData("D\r");
  } */
  
  if(WiFi.status() != WL_CONNECTED) {
    u8g2log.print("!");
    delay(5000); // Wait 5 seconds
  } else {
    timeClient.update();
    if (currentTime < lastRssiCheck || lastRssiCheck == 0 || (lastRssiCheck + rssiInterval) < currentTime) {
      lastRssiCheck = currentTime;
      lastRssi = WiFi.RSSI();
      // int bars = get_bars(lastRssi);
    }

    if (currentTime < lastStateCheck || lastStateCheck == 0 || (lastStateCheck + stateInterval) < currentTime) {

      lastStateCheck = millis();

      if (currentTime >= waitForResponseUntil && hasF && !hasQ1) {
        if (currentTime >= waitForResponseUntil) {
          readFails ++;
        }
        waitForResponseUntil = currentTime + waitForResponseInterval;
        u8g2log.print("QUERY Q1a\n");
        sendSerialData("Q1\r");
      }
      /*
      if (currentTime >= waitForResponseUntil && hasF && hasQ1 && !hasD) {
        if (currentTime >= waitForResponseUntil) {
          readFails ++;
        }
        waitForResponseUntil = currentTime + waitForResponseInterval;
        u8g2log.print("QUERY Da\n");
        sendSerialData("D\r");
      }
      */
      if (currentTime < lastStateSend || lastStateSend == 0 || 
          (lastStateSend + sendInterval) < currentTime || 
          triggerStateChange
          ) {
        if ((hasF && hasQ1) || currentTime >= waitForResponseUntil || readFails > 5) {
          sendDataToServer();
        }
      }

      if (hasQ1) {
        hasQ1 = false;
      }
      
    } else if (hasQ1 && hasD) {
      sendDataToServer();
    }

  }

}
