
//
// Based on the following source code:
// https://grafana.com/blog/2021/03/08/how-i-built-a-monitoring-system-for-my-avocado-plant-with-arduino-and-grafana-cloud/?src=email&cnt=trial-started&camp=grafana-cloud-trial
// https://github.com/ivanahuckova/avocado_monitoring
//

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <SPI.h>
#include <uFire_SHT20.h>
#include <Adafruit_ADS1X15.h>

#include "config.h"

#if ENABLE_DISPLAY_OLED
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#endif

#if ENABLE_DISPLAY_EINK
#include <GxEPD2_BW.h>
#include <Fonts/Org_01.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include "eink_background.h"
#endif

// NTP Client
WiFiUDP ntpUDP;
NTPClient ntpClient(ntpUDP);

// Sensors
uFire_SHT20 sht20;
Adafruit_ADS1115 ads;

// Grafana client and transport
HTTPClient httpLoki;
HTTPClient httpGraphite;

#if ENABLE_DISPLAY_OLED
#define OLED_RESET 0 // GPIO0
Adafruit_SSD1306 display(OLED_RESET);
#endif

#if ENABLE_DISPLAY_EINK
// TODO set the correct pins
GxEPD2_BW<GxEPD2_154, GxEPD2_154::HEIGHT> display(GxEPD2_154(/*CS=D8*/ SS, /*DC=D3*/ 0, /*RST=D4*/ 2, /*BUSY=D2*/ 4));
#endif

// Defs -----------------------------------------------------------------------

struct AirCondition
{
  float temp;
  float humidity;
  float dew_point;
};

struct ValPerc
{
  int raw;
  int percentage;
};
struct ValPercFloat
{
  float raw;
  float percentage;
};

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max);

AirCondition measureAirCondition();
ValPerc measureSoilMoisture();
ValPercFloat measureBatteryVolt();
float measureSolarPanelVolt();
bool evaluateSamples(AirCondition air, ValPerc soil, ValPercFloat battery, float solarPanelVolt);

void setupWiFi();

void sendToGraphite(unsigned long ts, AirCondition air, ValPerc soil, ValPercFloat battery, float solarPanelVolt);
void sendToLoki(unsigned long ts, AirCondition air, ValPerc soil, ValPercFloat battery, float solarPanelVolt, String message);

String getTimeString(unsigned long ts);
void printDisplayInfo(unsigned long ts, AirCondition air, ValPerc soil, ValPercFloat battery, float solarPanelVolt);
void printDisplay(String text);

// Methods --------------------------------------------------------------------

void setup()
{

#if DEBUG
  // Serial -------
  Serial.begin(115200);
  delay(10);
  Serial.println('\n');
#endif

  // Led ----------
  pinMode(STATUS_LED_PIN, OUTPUT);

  // DHT20 --------
  Wire.begin();
  sht20.begin();

  // ADC ----------
  if (!ads.begin())
  {
    while (1)
    {
      Serial.println("Failed to initialize ADS!");
      delay(1000);
    }
  }

// Display ------
#if ENABLE_DISPLAY_OLED
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // initialize with the I2C addr 0x3C (for the 64x48)
  display.display();
  printDisplay("Ciao!\n\nWiFi...");
#endif

#if ENABLE_DISPLAY_EINK
  display.init(DEBUG ? 115200 : 0);
#endif

  // WiFi ---------
  setupWiFi();
  ntpClient.begin();
}

void loop()
{
  digitalWrite(STATUS_LED_PIN, HIGH);

  // Reconnect to WiFi if required
  if (WiFi.status() != WL_CONNECTED)
  {
    WiFi.disconnect();
    yield();
    setupWiFi();
  }

#if ENABLE_DISPLAY_OLED || ENABLE_DISPLAY_EINK
  printDisplay("WiFi connected!");
#endif

  // Update time via NTP if required
  while (!ntpClient.update())
  {
    yield();
    ntpClient.forceUpdate();
  }

  // Get current timestamp
  unsigned long ts = ntpClient.getEpochTime();

  // Read sensors
  Serial.println("Collect data...");
  AirCondition air = measureAirCondition();
  ValPerc soil_moisture = measureSoilMoisture();
  ValPercFloat battery = measureBatteryVolt();
  float solarPanelVolt = measureSolarPanelVolt();

  // Check if values are valid
  if (evaluateSamples(air, soil_moisture, battery, solarPanelVolt))
  {
    // Send
    sendToGraphite(ts, air, soil_moisture, battery, solarPanelVolt);
    sendToLoki(ts, air, soil_moisture, battery, solarPanelVolt, "New_samples!");
  }

  digitalWrite(STATUS_LED_PIN, LOW);

// Print on display
#if ENABLE_DISPLAY_OLED || ENABLE_DISPLAY_EINK
  printDisplayInfo(ts, air, soil_moisture, battery, solarPanelVolt);
#endif

  // Put ESP in deep sleep
  Serial.println("Go in deep sleep for " + String(SAMPLE_INTERVAL_SEC) + " sec");
  ESP.deepSleep(SAMPLE_INTERVAL_SEC * 1000000);
  // delay(SAMPLE_INTERVAL_SEC * 1000);
}

// Measures -------------------------------------------------------------------

AirCondition measureAirCondition()
{
  sht20.measure_all();

  AirCondition res = {
      sht20.tempC,
      sht20.RH,
      sht20.dew_pointC};

  return res;
}

ValPerc measureSoilMoisture()
{

  int16_t raw = ads.readADC_SingleEnded(SOIL_MOISTURE_PIN);
  int16_t perc = map(raw, AIR_MOISTURE_VAL, WATER_MOISTURE_VAL, 0, 100);

  if (perc >= 100)
  {
    perc = 100;
  }
  else if (perc <= 0)
  {
    perc = 0;
  }

  ValPerc res = {
      raw,
      perc};

  return res;
}

ValPercFloat measureBatteryVolt()
{

  int16_t raw = ads.readADC_SingleEnded(BATTERY_VOLT_PIN);
  float volt = ads.computeVolts(raw);
  float perc = mapFloat(volt, BATTERY_MIN_VOLTS, BATTERY_MAX_VOLTS, 0.0, 100.0);

  if (perc >= 100.0)
  {
    perc = 100.0;
  }
  else if (perc <= 0.0)
  {
    perc = 0.0;
  }

  ValPercFloat res = {
      volt,
      perc};

  return res;
}

float measureSolarPanelVolt()
{
  int16_t raw = ads.readADC_SingleEnded(SOLAR_PANEL_VOLT_PIN);
  float volt = ads.computeVolts(raw);
  return volt;
}

bool evaluateSamples(AirCondition air, ValPerc soil, ValPercFloat battery, float solarPanelVolt)
{
  if (air.temp > 100 || air.humidity > 100 || air.dew_point > 100)
  {
    return false;
  }

  return true;
}

// Setup ----------------------------------------------------------------------

void setupWiFi()
{
  Serial.print("Connecting to '");
  Serial.print(WIFI_SSID);
  Serial.print("' ...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void sendToLoki(unsigned long ts, AirCondition air, ValPerc soil, ValPercFloat battery, float solarPanelVolt, String message)
{
  String lokiUrl = String("https://") + GC_LOKI_USER + ":" + GC_LOKI_PASS + "@" + GC_LOKI_URL + "/loki/api/v1/push";
  String body = "{\"streams\": [{ \"stream\": { \"plant_id\": \"" + String(SENSOR_ID) + "\", \"monitoring_type\": \"plant\"}, \"values\": [ [ \"" + ts + "000000000\", \"" + "temperature=" + air.temp + " humidity=" + air.humidity + " dew_point=" + air.dew_point + " soil_moisture=" + soil.percentage + +" soil_moisture_raw=" + soil.raw + " battery_volts=" + battery.raw + " battery_perc=" + battery.percentage + " solar_panel_volts=" + solarPanelVolt + " msg=\'" + message + "\'\" ] ] }]}";

  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();

  // Submit POST request via HTTP
  httpLoki.begin(*client, lokiUrl);
  httpLoki.addHeader("Content-Type", "application/json");
  int httpCode = httpLoki.POST(body);
  Serial.printf("Loki [HTTPS] POST...  Code: %d\n", httpCode);
  httpLoki.end();
}

void sendToGraphite(unsigned long ts, AirCondition air, ValPerc soil, ValPercFloat battery, float solarPanelVolt)
{

  // Build hosted metrics json payload
  String body = String("[") +
                "{\"name\":\"temperature\",\"interval\":" + SAMPLE_INTERVAL_SEC + ",\"value\":" + air.temp + ",\"mtype\":\"gauge\",\"time\":" + ts + "}," +
                "{\"name\":\"humidity\",\"interval\":" + SAMPLE_INTERVAL_SEC + ",\"value\":" + air.humidity + ",\"mtype\":\"gauge\",\"time\":" + ts + "}," +
                "{\"name\":\"dew_point\",\"interval\":" + SAMPLE_INTERVAL_SEC + ",\"value\":" + air.dew_point + ",\"mtype\":\"gauge\",\"time\":" + ts + "}," +
                "{\"name\":\"soil_moisture\",\"interval\":" + SAMPLE_INTERVAL_SEC + ",\"value\":" + soil.percentage + ",\"mtype\":\"gauge\",\"time\":" + ts + "}," +
                "{\"name\":\"battery_volts\",\"interval\":" + SAMPLE_INTERVAL_SEC + ",\"value\":" + battery.raw + ",\"mtype\":\"gauge\",\"time\":" + ts + "}," +
                "{\"name\":\"battery_perc\",\"interval\":" + SAMPLE_INTERVAL_SEC + ",\"value\":" + battery.percentage + ",\"mtype\":\"gauge\",\"time\":" + ts + "}," +
                "{\"name\":\"solar_panel_volts\",\"interval\":" + SAMPLE_INTERVAL_SEC + ",\"value\":" + solarPanelVolt + ",\"mtype\":\"gauge\",\"time\":" + ts + "}]";

  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();

  // Submit POST request via HTTP
  httpGraphite.begin(*client, String("https://") + GC_GRAPHITE_URL + "/graphite/metrics");
  httpGraphite.setAuthorization(GC_GRAPHITE_USER, GC_GRAPHITE_PASS);
  httpGraphite.addHeader("Content-Type", "application/json");

  int httpCode = httpGraphite.POST(body);
  Serial.printf("Graphite [HTTPS] POST...  Code: %d\n", httpCode);
  httpGraphite.end();
}

// Display --------------------------------------------------------------------

String getTimeString(unsigned long ts)
{
  int hours = (ts / 60 / 60) % 24;
  int min = (ts / 60) % 60;
  String hoursStr = hours < 10 ? "0" + String(hours) : String(hours);
  String minStr = min < 10 ? "0" + String(min) : String(min);

  return hoursStr + ":" + minStr;
}

#if ENABLE_DISPLAY_OLED

void printDisplayInfo(unsigned long ts, AirCondition air, ValPerc soil, ValPercFloat battery, float solarPanelVolt)
{
  Serial.println("Print on display full info");
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);

  display.printf("TEM  %.1f", air.temp);
  display.println("C");
  display.printf("HUM    %.0f", air.humidity);
  display.println("%");
  display.printf("SOIL   %i", soil.percentage);
  display.println("%");
  display.println("");
  display.println("----------");
  display.println("O    " + getTimeString(ts));

  display.display();

  // Sleep after 3sec
  delay(3 * 1000);
  display.clearDisplay();
  display.display();
}

void printDisplay(String text)
{
  Serial.println("Print on display");
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);

  display.print(text);

  display.display();
}
#endif

#if ENABLE_DISPLAY_EINK

void printTextOnDisplay(String text, uint16_t x, uint16_t y, const GFXfont *font, bool centerX, bool centerY)
{
  display.setFont(font);
  display.setTextColor(GxEPD_BLACK);

  int16_t tbx, tby;
  uint16_t tbw, tbh;
  display.getTextBounds(text, x, y, &tbx, &tby, &tbw, &tbh);
  // Center wrt the given coord
  uint16_t lx = centerX ? (x - (tbw / 2)) : x;
  uint16_t ly = centerY ? y + (tbh / 2) : y;

  display.setCursor(lx, ly);
  display.print(text);
}

void printDisplayInfo(unsigned long ts, AirCondition air, ValPerc soil, ValPercFloat battery, float solarPanelVolt)
{
  Serial.println("Print on display full info");

  display.setRotation(1);
  display.setFullWindow();
  display.firstPage();

  do
  {
    display.fillScreen(GxEPD_BLACK);
    // Draw background
    display.drawBitmap(0, 0, EINK_BACKGROUND, display.epd2.WIDTH, display.epd2.HEIGHT, GxEPD_WHITE);

    char buffer[10];
    // Print current time
    printTextOnDisplay(getTimeString(ts), display.height() / 2, 12, &FreeMonoBold9pt7b, true, false);
    // Print soil moisture
    sprintf(buffer, "%i%%", soil.percentage);
    printTextOnDisplay(buffer, 80, 60, &FreeMonoBold24pt7b, false, true);
    // Print air temperature
    sprintf(buffer, "%.1fC", air.temp);
    printTextOnDisplay(buffer, 30, 160, &FreeMonoBold12pt7b, false, true);
    // Print air humidity
    sprintf(buffer, "%.0f%%", air.humidity);
    printTextOnDisplay(buffer, 145, 160, &FreeMonoBold12pt7b, false, true);
    // Print next update time
    printTextOnDisplay("Next at " + getTimeString(ts + SAMPLE_INTERVAL_SEC), display.height() / 2, display.width() - 2, &Org_01, true, false);

  } while (display.nextPage());
  display.hibernate();
}

void printDisplay(String text)
{
  Serial.println("Print on display");
  display.setRotation(1);
  display.setFullWindow();
  display.firstPage();

  do
  {
    display.fillScreen(GxEPD_BLACK);
    printTextOnDisplay(text, display.width() / 2, display.height() / 2, &FreeMonoBold12pt7b, true, true);
  } while (display.nextPage());
  display.hibernate();
}
#endif

// Utils ----------------------------------------------------------------------

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max)
{
  const float dividend = out_max - out_min;
  const float divisor = in_max - in_min;
  const float delta = x - in_min;

  return (delta * dividend + (divisor / 2.0)) / divisor + out_min;
}
