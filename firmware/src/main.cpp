/*
    VFD clock firmware (c) 2018 David Pye <davidmpye@gmail.com>

    http://www.davidmpye.com/VFD

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <Arduino.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ButtonHandler.h>
#include <Time.h>
#include <Timezone.h>
#include <RTClib.h>
#include "Display.h"
#include "Config.h"
#include <NtpClientLib.h>

const int NETWORKS = 4;
const char *SSID[NETWORKS] = {"AP1","","AP2"}; // Add multiple APs
const char *PASSWORD[NETWORKS] = {"key1","key2"}; // Add multiple keys
const char *ota_hostname="espvfd";
const char *ota_password="";

//#define IP_STATIC
#ifdef IP_STATIC
  IPAddress ip(192, 168, 1, 20);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);
#endif

Display display;
RTC_DS3231 rtc;

ButtonHandler buttonHandler;

void updateBrightness() {
  int b = analogRead(A0) / 4;
  if (b > 255) b = 255;  //In case b is 1024...
  display.setBrightness(255 - b);
}

void setupOTA() {
  //These are the OTA callbacks.
  ArduinoOTA.onStart([]() {
    display.clear();
  });
  ArduinoOTA.onEnd([]() {
    ESP.restart();
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    int percent = progress / (total / 100);
      display.setTubeChar(1, percent/ 10);
      display.setTubeChar(0, percent %10);
      display.update();
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(ota_hostname);
  ArduinoOTA.begin();
}

void setRTC() {
  DateTime t = rtc.now(); //This will get the default time from above.
  tmElements_t y = buttonHandler.getDate(&t);
  tmElements_t x  = buttonHandler.getTime(&t);
  //Set the time
  rtc.adjust(DateTime(y.Year + 1970, y.Month, y.Day, x.Hour, x.Minute, x.Second));
}

void setup() {
  //Button 1
  pinMode(D0, INPUT);
  //Button 2
  pinMode(D5, INPUT);
  //Button 3
  pinMode(3, INPUT);
  //Button 4
  pinMode(1, INPUT);

  display.begin();
  Wire.begin(D2,D1);

  WiFi.mode(WIFI_STA);
  #ifdef IP_STATIC
    WiFi.config(ip, gateway, subnet);
  #endif
  // Connect to WiFi AP
  int ssids=WiFi.scanNetworks();
  for(int i=0; i<ssids; i++){
    for(int j=0; j<NETWORKS; j++){
      // Loop through SSIDs, attempt connection.
      display.setTubeChar(0, 0xA);
      display.setTubeChar(1, i);
      display.update();
      if(!strcmp(WiFi.SSID(i).c_str(),SSID[j])){
        int retryCount=3;
        for(int k=0; k<retryCount; k++){
          WiFi.begin(SSID[j], PASSWORD[j]);
          delay(3333);
          // Successful connection, end loops.
          if(WiFi.status()==WL_CONNECTED){
            i=ssids;
            j=NETWORKS;
            k=retryCount;
          }
        }
      }
    }
  }
  // Display IP
  if(WiFi.status()==WL_CONNECTED){
    IPAddress localIP=WiFi.localIP();
    for(int i=0; i<4; i++){
      int digitTO=666, digitGapTO=111, groupGapTO=444;
      display.clear();display.update();
      display.setTubeChar(0, localIP[i]/100);display.update();delay(digitTO);
      display.clear();display.update();delay(digitGapTO);
      display.setTubeChar(0, (localIP[i]%100)/10);display.update();delay(digitTO);
      display.clear();display.update();delay(digitGapTO);
      display.setTubeChar(0, (localIP[i]%100)%10);display.update();delay(digitTO);
      display.clear();display.update();delay(groupGapTO);
    }
  }

  setupOTA();

  buttonHandler.begin(D0, D5, 3, 1, &display);
  display.setBrightness(0xFF);

  if (! rtc.begin()) {
    //Display error code 0xFF if RTC not available.
    display.clear();
    display.setTubeChar(1, 0xF);
    display.setTubeChar(0, 0xF);
    while(1) {
      display.update();
      delay(100);
    }
  }
  //Check to see if the RTC has lost time - if so, set the time
  if (rtc.lostPower()) {
    time_t timeT=0;
    int retryCount=3;
    // Connect to NTP server
    for(int i=0; i<retryCount; i++){
      if(NTP.begin("pool.ntp.org", 0, true, 0)) i=retryCount;
      delay(999);
    }
    NTP.setInterval(63);
    for(int i=0; i<retryCount; i++){
      timeT=NTP.getTime();
      delay(999);
      // Set time by NTP
      if(timeT){
        rtc.adjust(DateTime(year(timeT), month(timeT), day(timeT),
          hour(timeT), minute(timeT), second(timeT)));
        i=retryCount;
      }
    }
    // Set the time to midday jan 2001
    if(!timeT) rtc.adjust(DateTime(2001, 1, 1, 12, 0, 0));
  }

  display.hello();
  display.update();
  delay(1000);
}

void handleButtonEvent(BUTTON_EVENT e) {
  switch(e) {
    case BUTTON_A_SHORTPRESS:
      display.displayDate(rtc.now());
      display.update();
      delay(2000);
      break;

    case BUTTON_A_LONGPRESS:
      setRTC();
      break;

    case BUTTON_C_SHORTPRESS:
      switch (display.getTimeMode()) {
        case TWENTYFOURHR_MODE:
          display.setTimeMode(AMPM_MODE);
          break;
        case AMPM_MODE:
          display.setTimeMode(EPOCH_MODE);
          break;
        case EPOCH_MODE:
          display.setTimeMode(TWENTYFOURHR_MODE);
          break;
        }
        break;

      case BUTTON_B_SHORTPRESS:
        switch(display.getLEDMode()) {
          case RAINBOW_MODE:
            display.setLEDMode(COL_PER_NUM_MODE);
            break;
          case COL_PER_NUM_MODE:
            display.setLEDMode(COL_BY_TIME_MODE);
            break;
          case COL_BY_TIME_MODE:
            display.setLEDMode(STEALTH_MODE);
            break;
          case STEALTH_MODE:
            display.setLEDMode(RAINBOW_MODE);
            break;
          }
          break;

      case BUTTON_C_LONGPRESS:
        display.displayInt(analogRead(A0));
        display.update();
        delay(1000);
        break;

      case BUTTON_D_LONGPRESS:
        display.test();
        display.update();
        delay(1000);
        break;

        default:
          break;
    }
}

byte dash = false;
void loop() {
  static int lastSec = -1;

  DateTime t = rtc.now();
  //If the time has moved forward, we will update the display:
  if (t.second() != lastSec) {
     lastSec = t.second();
     display.displayTime(t);
  }

  updateBrightness();
  display.update();
  //Handle any button presses.
  handleButtonEvent(buttonHandler.poll());
  //process any outstanding OTA events
  ArduinoOTA.handle();
  delay(100);
}
