/*
This is the code for the AirGradient DIY Air Quality Sensor with an ESP8266 Microcontroller.

It is a high quality sensor showing PM2.5, CO2, Temperature and Humidity on a small display and can send data over Wifi.

For build instructions please visit https://www.airgradient.com/diy/

Compatible with the following sensors:
Plantower PMS5003 (Fine Particle Sensor)
SenseAir S8 (CO2 Sensor)
SHT30/31 (Temperature/Humidity Sensor)

Please install ESP8266 board manager (tested with version 3.0.0)

The codes needs the following libraries installed:
"WifiManager by tzapu, tablatronix" tested with Version 2.0.3-alpha
"ESP8266 and ESP32 OLED driver for SSD1306 displays by ThingPulse, Fabrice Weinberg" tested with Version 4.1.0

If you have any questions please visit our forum at https://forum.airgradient.com/

Configuration:
Please set in the code below which sensor you are using and if you want to connect it to WiFi.
You can also switch PM2.5 from ug/m3 to US AQI and Celcius to Fahrenheit

If you are a school or university contact us for a free trial on the AirGradient platform.
https://www.airgradient.com/schools/

Kits with all required components are available at https://www.airgradient.com/diyshop/

MIT License
*/

#include <AirGradient.h>

#include <WiFiManager.h>

#include <ESP8266WiFi.h>

#include <ESP8266HTTPClient.h>

#include <Wire.h>

#include "SSD1306Wire.h"

AirGradient ag = AirGradient();

SSD1306Wire display(0x3c, SDA, SCL, GEOMETRY_64_48);

// set sensors that you do not use to false
boolean hasPM = true;
boolean hasCO2 = true;
boolean hasSHT = true;

// set to true to switch PM2.5 from ug/m3 to US AQI
boolean inUSaqi = false;

// set to true to switch from Celcius to Fahrenheit
boolean inF = false;

// set to true if you want to connect to wifi. The display will show values only when the sensor has wifi connection
boolean connectWIFI = false;

// change if you want to send the data to another server
String APIROOT = "http://hw.airgradient.com/";

// create structures for storing historical data to graph
struct reading_history {
  // history data
  char* str_name;
  int history_len;
  int history_cur_pos;
  int* history_table;
  // sets scale for drawing graph
  int y_range_max;
  int y_range_min;
  // sets color cutoffs
  int y_range_warn;
  int y_range_alarm;
};
struct reading_history pms_hist = {0};
struct reading_history co2_hist = {0};

void setup() {
  Serial.begin(9600);

  display.init();
  display.flipScreenVertically();
  showTextRectangle("Init", String(ESP.getChipId(), HEX), true);

  if (hasPM) {
    asprintf(&(pms_hist.str_name), "PM2.5");
    pms_hist.history_cur_pos = 0;
    pms_hist.history_len = display.width();
    pms_hist.history_table = (int*) malloc(sizeof(*(pms_hist.history_table)) *
                                           pms_hist.history_len);
    pms_hist.y_range_max = 50;
    pms_hist.y_range_min = -2;
    pms_hist.y_range_warn = 5;
    pms_hist.y_range_alarm = 20;
    ag.PMS_Init();
  }
  if (hasCO2) {
    co2_hist.str_name = "CO2";
    co2_hist.history_cur_pos = 0;
    co2_hist.history_len = display.width();
    co2_hist.history_table = (int*) malloc(sizeof(*(co2_hist.history_table)) *
                                           co2_hist.history_len);
    co2_hist.y_range_max = 2000;
    co2_hist.y_range_min = 300;
    co2_hist.y_range_warn = 800;
    co2_hist.y_range_alarm = 1100;
    ag.CO2_Init();
  }
  if (hasSHT) {
    ag.TMP_RH_Init(0x44);
  }

  if (connectWIFI) connectToWifi();
  delay(2000);
}

void loop() {
  // create payload
  String payload = "{\"wifi\":" + String(WiFi.RSSI()) + ",";
  String overall_stats = "";

  if (hasCO2) {
    int CO2 = ag.getCO2_Raw();
    payload = payload + "\"rco2\":" + String(CO2);
    overall_stats += "CO2 " + String(CO2) + "\n";
    if (co2_hist.history_table != NULL && co2_hist.history_len != 0) {
      co2_hist.history_cur_pos = (co2_hist.history_cur_pos + 1) %
                                  co2_hist.history_len;
      co2_hist.history_table[co2_hist.history_cur_pos] = CO2;
      showGraph(&co2_hist);
    }

    delay(5000);
  }

  if (hasPM) {
    if (hasCO2) payload = payload + ",";
    int PM2 = ag.getPM2_Raw();
    payload = payload + "\"pm02\":" + String(PM2);
    overall_stats += "PM " + String(PM2) + "\n";
    if (inUSaqi) {
      PM2 = PM_TO_AQI_US(PM2);
    }
    if (pms_hist.history_table != NULL && pms_hist.history_len != 0) {
      pms_hist.history_cur_pos = (pms_hist.history_cur_pos + 1) %
                                  pms_hist.history_len;
      pms_hist.history_table[pms_hist.history_cur_pos] = PM2;
      showGraph(&pms_hist);
    }

    delay(5000);
  }

  if (hasSHT) {
    if (hasCO2 || hasPM) payload = payload + ",";
    TMP_RH result = ag.periodicFetchData();
    payload = payload + "\"atmp\":" + String(result.t) + ",\"rhum\":" + String(result.rh);
    overall_stats += "Temp " + String(result.t) + "\n";
    overall_stats += "Humid " + String(result.rh) + "\n";
  }

  payload = payload + "}";

  // send payload
  if (connectWIFI) {
    Serial.println(payload);
    String POSTURL = APIROOT + "sensors/airgradient:" + String(ESP.getChipId(), HEX) + "/measures";
    Serial.println(POSTURL);
    WiFiClient client;
    HTTPClient http;
    http.begin(client, POSTURL);
    http.addHeader("content-type", "application/json");
    int httpCode = http.POST(payload);
    String response = http.getString();
    Serial.println(httpCode);
    Serial.println(response);
    http.end();
  }

  // show overall stats of last payload on screen
  showTextSmall(overall_stats);
  delay(20000);
}

// DISPLAY
void showTextRectangle(String ln1, String ln2, boolean small) {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  if (small) {
    display.setFont(ArialMT_Plain_16);
  } else {
    display.setFont(ArialMT_Plain_24);
  }
  display.drawString(0, 0, ln1);
  display.drawString(0, 16, ln2);
  display.display();
}

void showTextSmall(String str) {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, str);
  display.display();
}

void showGraph(struct reading_history* hist) {
  int i;
  int cur_pos;
  int raw_y_value;
  int disp_y_value;
  int prev_disp_y_value;
  int line_len;
  int display_cur_pos;
  float raw_y_to_disp_y_ratio;
  char str_buf[32];

  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawStringf(0, 0, str_buf,
                      "%s\n%d",
                      hist->str_name,
                      hist->history_table[hist->history_cur_pos]);

  display_cur_pos = (display.width() - 1);
  raw_y_to_disp_y_ratio = (hist->y_range_max - hist->y_range_min) /
                          display.height();
  display.setColor(WHITE);
  for (i = hist->history_len; i > 0; i--) {
    // get raw y_value from table
    cur_pos = (i + hist->history_cur_pos) % hist->history_len;
    raw_y_value = hist->history_table[cur_pos];

    // process y value into range
    if (raw_y_value > hist->y_range_max) {
      raw_y_value = hist->y_range_max;
    }
    if (raw_y_value < hist->y_range_min) {
      raw_y_value = hist->y_range_min;
    }

    // process y value and line length
    disp_y_value = (int)((raw_y_value - hist->y_range_min) /
                         raw_y_to_disp_y_ratio);
    disp_y_value = display.height() - disp_y_value;
    if (i == hist->history_len) {
      prev_disp_y_value = disp_y_value;
    }
    line_len = prev_disp_y_value - disp_y_value;
    // draw line
    display.drawLine(
        display_cur_pos,
        disp_y_value,
        display_cur_pos,
        disp_y_value + line_len);
    display_cur_pos--;
    if (display_cur_pos < 0) {
      // prevent from going off display..
      break;
    }
    prev_disp_y_value = disp_y_value;
  }

  display.display();
}

// Wifi Manager
void connectToWifi() {
  WiFiManager wifiManager;
  //WiFi.disconnect(); //to delete previous saved hotspot
  String HOTSPOT = "AIRGRADIENT-" + String(ESP.getChipId(), HEX);
  wifiManager.setTimeout(120);
  if (!wifiManager.autoConnect((const char * ) HOTSPOT.c_str())) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
    delay(5000);
  }
}

// Calculate PM2.5 US AQI
int PM_TO_AQI_US(int pm02) {
  if (pm02 <= 12.0) return ((50 - 0) / (12.0 - .0) * (pm02 - .0) + 0);
  else if (pm02 <= 35.4) return ((100 - 50) / (35.4 - 12.0) * (pm02 - 12.0) + 50);
  else if (pm02 <= 55.4) return ((150 - 100) / (55.4 - 35.4) * (pm02 - 35.4) + 100);
  else if (pm02 <= 150.4) return ((200 - 150) / (150.4 - 55.4) * (pm02 - 55.4) + 150);
  else if (pm02 <= 250.4) return ((300 - 200) / (250.4 - 150.4) * (pm02 - 150.4) + 200);
  else if (pm02 <= 350.4) return ((400 - 300) / (350.4 - 250.4) * (pm02 - 250.4) + 300);
  else if (pm02 <= 500.4) return ((500 - 400) / (500.4 - 350.4) * (pm02 - 350.4) + 400);
  else return 500;
};
