
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

// NTP Client
WiFiUDP ntpUDP;
NTPClient ntpClient(ntpUDP);

// Sensors
uFire_SHT20 sht20;
Adafruit_ADS1115 ads;

// Grafana client and transport
HTTPClient httpLoki;
HTTPClient httpGraphite;

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

void setupWiFi();

void sendToGraphite(unsigned long ts, AirCondition air, ValPerc soil, ValPercFloat battery);
void sendToLoki(unsigned long ts, AirCondition air, ValPerc soil, ValPercFloat battery, String message);

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
  pinMode(LED_BUILTIN, OUTPUT);

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

  // WiFi ---------
  setupWiFi();
  ntpClient.begin();
}

void loop()
{

  // Reconnect to WiFi if required
  if (WiFi.status() != WL_CONNECTED)
  {
    WiFi.disconnect();
    yield();
    setupWiFi();
  }

  digitalWrite(LED_BUILTIN, HIGH);

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

  // Send
  sendToGraphite(ts, air, soil_moisture, battery);
  sendToLoki(ts, air, soil_moisture, battery, "New samples!");

  digitalWrite(LED_BUILTIN, LOW);

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

void sendToLoki(unsigned long ts, AirCondition air, ValPerc soil, ValPercFloat battery, String message)
{
  String lokiUrl = String("https://") + GC_LOKI_USER + ":" + GC_LOKI_PASS + "@" + GC_LOKI_URL + "/loki/api/v1/push";
  String body = "{\"streams\": [{ \"stream\": { \"plant_id\": \"" + String(SENSOR_ID) + "\", \"monitoring_type\": \"plant\"}, \"values\": [ [ \"" + ts + "000000000\", \"" + "temperature=" + air.temp + " humidity=" + air.humidity + " dew_point=" + air.dew_point + " soil_moisture=" + soil.percentage + " battery_volts=" + battery.raw + " battery_perc=" + battery.percentage + " msg=\'" + message + "\'\" ] ] }]}";

  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();

  // Submit POST request via HTTP
  httpLoki.begin(*client, lokiUrl);
  httpLoki.addHeader("Content-Type", "application/json");
  int httpCode = httpLoki.POST(body);
  Serial.printf("Loki [HTTPS] POST...  Code: %d\n", httpCode);
  httpLoki.end();
}

void sendToGraphite(unsigned long ts, AirCondition air, ValPerc soil, ValPercFloat battery)
{

  // Build hosted metrics json payload
  String body = String("[") +
                "{\"name\":\"temperature\",\"interval\":" + SAMPLE_INTERVAL_SEC + ",\"value\":" + air.temp + ",\"mtype\":\"gauge\",\"time\":" + ts + "}," +
                "{\"name\":\"humidity\",\"interval\":" + SAMPLE_INTERVAL_SEC + ",\"value\":" + air.humidity + ",\"mtype\":\"gauge\",\"time\":" + ts + "}," +
                "{\"name\":\"dew_point\",\"interval\":" + SAMPLE_INTERVAL_SEC + ",\"value\":" + air.dew_point + ",\"mtype\":\"gauge\",\"time\":" + ts + "}," +
                "{\"name\":\"soil_moisture\",\"interval\":" + SAMPLE_INTERVAL_SEC + ",\"value\":" + soil.percentage + ",\"mtype\":\"gauge\",\"time\":" + ts + "}," +
                "{\"name\":\"battery_volts\",\"interval\":" + SAMPLE_INTERVAL_SEC + ",\"value\":" + battery.raw + ",\"mtype\":\"gauge\",\"time\":" + ts + "}," +
                "{\"name\":\"battery_perc\",\"interval\":" + SAMPLE_INTERVAL_SEC + ",\"value\":" + battery.percentage + ",\"mtype\":\"gauge\",\"time\":" + ts + "}]";

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

// Utils ----------------------------------------------------------------------

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max)
{
  const float dividend = out_max - out_min;
  const float divisor = in_max - in_min;
  const float delta = x - in_min;

  return (delta * dividend + (divisor / 2.0)) / divisor + out_min;
}
