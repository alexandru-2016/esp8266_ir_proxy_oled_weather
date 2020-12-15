/**The MIT License (MIT)

Copyright (c) 2018 by Daniel Eichhorn - ThingPulse

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

See more at https://thingpulse.com
*/

#include <Arduino.h>

#include <ESPWiFi.h>
#include <ESPHTTPClient.h>
#include <JsonListener.h>

// time
#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval
#include <coredecls.h>                  // settimeofday_cb()

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeMonoBold12pt7b.h>

#include "OpenWeatherMapCurrent.h"
#include "TextFont.h"
#include "WeatherIconsFont.h"

#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>

#include "secrets.h"

/***************************
 * Begin Settings
 **************************/
#define TZ              2       // (utc+) TZ in hours
#define DST_MN          60      // use 60mn for summer time in some countries

// Setup
const int UPDATE_INTERVAL_SECS = 20 * 60; // Update every 20 minutes
const int SCREEN_SAVER_INTERVAL_SECS = 5; // Show screen saver after one minute
bool screenSaverActive = false;
int timeSinceLastIrReceive = 0;

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Infrared
uint16_t RECV_PIN = 13; // ESP8266 D7
IRrecv irrecv(RECV_PIN);

const uint16_t kIrLed = 16; // ESP8266 D0
IRsend irsend(kIrLed);  // Set the GPIO to be used to sending the message.

// Simulate speaker volume display
int currentVolume = 28;

// Pick a language code from this list:
// Arabic - ar, Bulgarian - bg, Catalan - ca, Czech - cz, German - de, Greek - el,
// English - en, Persian (Farsi) - fa, Finnish - fi, French - fr, Galician - gl,
// Croatian - hr, Hungarian - hu, Italian - it, Japanese - ja, Korean - kr,
// Latvian - la, Lithuanian - lt, Macedonian - mk, Dutch - nl, Polish - pl,
// Portuguese - pt, Romanian - ro, Russian - ru, Swedish - se, Slovak - sk,
// Slovenian - sl, Spanish - es, Turkish - tr, Ukrainian - ua, Vietnamese - vi,
// Chinese Simplified - zh_cn, Chinese Traditional - zh_tw.
String OPEN_WEATHER_MAP_LANGUAGE = "en";

const boolean IS_METRIC = true;

/***************************
 * End Settings
 **************************/

OpenWeatherMapCurrentData currentWeather;
OpenWeatherMapCurrent currentWeatherClient;

#define TZ_MN           ((TZ)*60)
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)
time_t now;

// flag changed in the ticker function every 10 minutes
bool readyForWeatherUpdate = false;

String lastUpdate = "--";

long timeSinceLastWUpdate = 0;

//declaring prototypes
void updateData();
void drawCurrentWeather();
void setReadyForWeatherUpdate();

const int WEATHER_X_START= 0;
const int WEATHER_X_START_DIRECTION = -1;
int weatherX;
int weatherXDirection;
int weatherY = 63;

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println();
  Serial.println();

  // initialize dispaly
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed"));
  }
  
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE, SSD1306_BLACK); // Draw white text

  WiFi.begin(WIFI_SSID, WIFI_PWD);

  int counter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    counter++;
  }
  // Get time from network time service
  configTime(TZ_SEC, DST_SEC, "pool.ntp.org");

  Serial.println("");
  updateData();
  
  Serial.println("Setup IR receiver ...");
  irrecv.enableIRIn();
  
  Serial.println("Setup IR sender ...");
  irsend.begin();

  resetScreenSaver();
  show_number(currentVolume);

  Serial.println("Finished setup");
  Serial.println();
  Serial.println();
}

void loop() {
  process_ir();

  int time = millis();
  
  if (time - timeSinceLastWUpdate > (1000L*UPDATE_INTERVAL_SECS)) {
    setReadyForWeatherUpdate();
    timeSinceLastWUpdate = millis();
  }

  if (readyForWeatherUpdate && screenSaverActive) {
    updateData();
  }

  if (!screenSaverActive && time - timeSinceLastIrReceive > (1000L*SCREEN_SAVER_INTERVAL_SECS)) {
    setScreenSaverActive();
  }
}

void send_ir_code(uint16_t buf[], uint16_t len, char* msg) {
  Serial.println(msg);
  irsend.sendRaw(buf, len, 38);
  resetScreenSaver();
}

// normally the raw data for NEC should only be 67
// but the microlab speakers missbehave when receiving IR code from another remove of a near samsung TV
// the fix is to include an extra 4 elements which actually represent the repeat code
uint16_t rawDataDownExtra[71] = {9228, 4438,  632, 1614,  632, 508,  606, 536,  608, 504,  632, 506,  628, 486,  636, 506,  630, 510,  630, 484,  634, 1642,  626, 1616,  632, 1616,  658, 1618,  630, 1616,  632, 1616,  658, 1616,  606, 506,  636, 534,  598, 512,  602, 538,  630, 1614,  604, 510,  634, 534,  574, 534,  630, 1618,  628, 1618,  660, 1614,  630, 1618,  604, 536,  626, 1620,  628, 1616,  638, 1636,  628, 39862,  9178, 2196,  632};  // NEC 807F08F7
uint16_t rawDataUpExtra[71] = {9116, 4474,  568, 1676,  628, 510,  568, 570,  568, 540,  632, 508,  570, 570,  566, 542,  630, 508,  592, 572,  564, 1654,  566, 1676,  628, 1642,  570, 1702,  542, 1676,  628, 1642,  568, 1676,  594, 1654,  626, 508,  568, 570,  588, 520,  628, 1642,  590, 520,  628, 508,  568, 570,  566, 546,  626, 1668,  564, 1656,  590, 1652,  628, 512,  572, 1670,  630, 1642,  568, 1678,  588, 39930,  9098, 2234,  590};  // NEC 807F8877
uint16_t rawDataMuteExtra[71] = {9200, 4436,  626, 1618,  630, 510,  626, 514,  630, 508,  598, 538,  602, 482,  634, 506,  626, 510,  632, 482,  632, 1638,  628, 1616,  630, 1616,  658, 1612,  630, 1618,  628, 1616,  632, 1638,  630, 484,  656, 1614,  604, 536,  630, 480,  628, 508,  628, 482,  634, 504,  628, 514,  628, 1614,  628, 510,  604, 1642,  604, 1640,  632, 1640,  626, 1616,  630, 1616,  636, 1636,  624, 39856,  9160, 2194,  634};  // NEC 807F40BF


void process_ir() {
  decode_results results;
  
  if (irrecv.decode(&results)) {
    Serial.print("Received code: ");
    serialPrintUint64(results.value, 16);
    Serial.println();

    switch (results.value) {
      case 0xCF001EE1:
        send_ir_code(rawDataDownExtra, 71, "Volume Down");
        
        if (currentVolume > 0)
        {
          currentVolume--;
          show_number(currentVolume);
        }
        
        break;
        
      case 0xCF00EE11:
        send_ir_code(rawDataUpExtra, 71, "Volume Up");
        
        if (currentVolume < 60)
        {
          currentVolume++;
          show_number(currentVolume);
        }
        
        break;
        
      default:
        Serial.println("Ignore");
        break;
    }
    
    Serial.println();
    
    irrecv.resume();  // Receive the next value
  }
}

void show_number(int value) {
  display.stopscroll();
  display.clearDisplay();
//  display.drawPixel(0, 0, SSD1306_WHITE);
//  display.drawPixel(127, 0, SSD1306_WHITE);
//  display.drawPixel(0, 63, SSD1306_WHITE);
//  display.drawPixel(127, 63, SSD1306_WHITE);
  
  display.setTextSize(2);
  display.setFont(&Open_Sans_Bold_44);

  String text = String(value);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 63, &x1, &y1, &w, &h);

  display.setCursor(64 - (w / 2),63);
  display.print(text);

  display.display();
}

void updateData() {
  currentWeatherClient.setMetric(IS_METRIC);
  currentWeatherClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  currentWeatherClient.updateCurrentById(&currentWeather, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID);

  readyForWeatherUpdate = false;
  delay(1000);
}

void drawCurrentWeather() {  
  int x = 0;
  int y = 54;
  display.clearDisplay();
  
  display.setTextSize(2);
  display.setFont(&Meteocons_Regular_18);  
  display.setCursor(x, y);
  display.print(currentWeather.iconMeteoCon);
  
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(currentWeather.iconMeteoCon, x, y, &x1, &y1, &w, &h);
  x = x + w + 5;

  String temp = String(currentWeather.temp, 0);
  display.setTextSize(2);
  display.setFont(&Open_Sans_Condensed_Light_32);
  display.setCursor(x, y);
  
  display.print(temp);

  display.getTextBounds(temp, x, y, &x1, &y1, &w, &h);
  x = x + w + 5;

  display.drawCircle(x, 10, 3, SSD1306_WHITE);
  
  display.display();
  
  display.startscrollleft(0x00, 0x0F);
}

void setReadyForWeatherUpdate() {
  Serial.println("Setting readyForUpdate to true");
  readyForWeatherUpdate = true;
}

void setScreenSaverActive() {
  Serial.println("Setting screenSaverActive to true");
  screenSaverActive = true;
  drawCurrentWeather();
}

void resetScreenSaver() {  
  timeSinceLastIrReceive = millis();
  screenSaverActive = false;
}
