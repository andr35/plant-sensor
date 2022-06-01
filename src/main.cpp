
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <SPI.h>
#include <uFire_SHT20.h>
#include <Adafruit_ADS1X15.h>
#include <ArduinoECCX08.h>

#define PromLokiTransport_H
// #include <PromLokiTransport.h>
// ----
#include "clients/ESP8266Client.h"
typedef ESP8266Client PromLokiTransport;
// ----

#include <PrometheusArduino.h>

#include "certificates.h"
#include "config.h"

// Sensors
uFire_SHT20 sht20;
Adafruit_ADS1115 ads;

// Prometheus client and transport
PromLokiTransport transport;
PromClient client(transport);

// Create a write request for 6 series
WriteRequest req(6, 2048);

// Define a TimeSeries which can hold up to 5 samples
TimeSeries ts1(1, "temperature_celsius", "{subject=\"air\"}");
TimeSeries ts2(1, "humidity_percent", "{subject=\"air\"}");
TimeSeries ts3(1, "dew_point", "{subject=\"air\"}");
TimeSeries ts4(1, "soil_moisture", "{subject=\"soil\"}");
TimeSeries ts5(1, "battery_volts", "{subject=\"battery\"}");
TimeSeries ts6(1, "battery_percent", "{subject=\"battery\"}");

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

void setupPrometheusClient();

// Methods --------------------------------------------------------------------

void setup()
{

  // Serial -------
  Serial.begin(115200);
  delay(10);
  Serial.println('\n');

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

  // Client -------
  setupPrometheusClient();
}

void loop()
{
  // TODO need to reconnect wifi?

  // Get current timestamp
  int64_t time;
  time = transport.getTimeMillis();

  // Read sensors
  AirCondition air = measureAirCondition();
  ValPerc soil_moisture = measureSoilMoisture();
  ValPercFloat batt = measureBatteryVolt();

  // Add samples to time series

  if (!ts1.addSample(time, air.temp))
  {
    Serial.println(ts1.errmsg);
  }
  if (!ts2.addSample(time, air.humidity))
  {
    Serial.println(ts2.errmsg);
  }
  if (!ts3.addSample(time, air.dew_point))
  {
    Serial.println(ts3.errmsg);
  }
  if (!ts4.addSample(time, soil_moisture.percentage))
  {
    Serial.println(ts4.errmsg);
  }
  if (!ts5.addSample(time, batt.raw))
  {
    Serial.println(ts5.errmsg);
  }
  if (!ts6.addSample(time, batt.percentage))
  {
    Serial.println(ts6.errmsg);
  }

  // Send
  PromClient::SendResult res = client.send(req);
  if (!res == PromClient::SendResult::SUCCESS)
  {
    Serial.println(client.errmsg);
  }
  else
  {
    Serial.println("Samples correctly sent to Prometheus!");
  }

  // Reset batches after a succesful send.
  ts1.resetSamples();
  ts2.resetSamples();
  ts3.resetSamples();
  ts4.resetSamples();
  ts5.resetSamples();
  ts6.resetSamples();

  // TODO remove
  // Serial.println((String)air.temp + "°C");
  // Serial.println((String)air.dew_point + "°C dew point");
  // Serial.println((String)air.humidity + " %RH");
  // Serial.println();
  // Serial.println((String)soil_moisture.raw + " raw");
  // Serial.println((String)soil_moisture.percentage + " %");
  // Serial.println("Battery " + (String)batt.raw + "v");
  // Serial.println("Battery " + (String)batt.percentage + " %");

  // TODO put ESP in deep sleep

  delay(10000);
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

// Prometheus -----------------------------------------------------------------

void setupPrometheusClient()
{
  Serial.println("Setting up Prometheus client...");

  uint8_t serialTimeout;
  while (!Serial && serialTimeout < 50)
  {
    delay(100);
    serialTimeout++;
  }

  // Configure and start the transport layer
  transport.setUseTls(true);
  transport.setCerts(grafanaCert, strlen(grafanaCert));
  transport.setWifiSsid(WIFI_SSID);
  transport.setWifiPass(WIFI_PASSWORD);
#if GC_DEBUG
  transport.setDebug(Serial);
#endif
  if (!transport.begin())
  {
    Serial.println(transport.errmsg);
    while (true)
    {
    };
  }

  // Configure the client
  client.setUrl(GC_PROM_URL);
  client.setPath((char *)GC_PROM_PATH);
  client.setPort(GC_PORT);
  client.setUser(GC_PROM_USER);
  client.setPass(GC_PROM_PASS);
#if GC_DEBUG
  client.setDebug(Serial);
#endif
  if (!client.begin())
  {
    Serial.println(client.errmsg);
    while (true)
    {
    };
  }

  // Add our TimeSeries to the WriteRequest
  req.addTimeSeries(ts1);
  req.addTimeSeries(ts2);
  req.addTimeSeries(ts3);
  req.addTimeSeries(ts4);
  req.addTimeSeries(ts5);
  req.addTimeSeries(ts6);
#if GC_DEBUG
  req.setDebug(Serial);
#endif
}

// Utils ----------------------------------------------------------------------

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max)
{
  const float dividend = out_max - out_min;
  const float divisor = in_max - in_min;
  const float delta = x - in_min;

  return (delta * dividend + (divisor / 2.0)) / divisor + out_min;
}
