#define WIFI_SSID "wifi_name"         // Add wifi name
#define WIFI_PASSWORD "wifi_password" // Add wifi password

#define DEBUG 1               // Enable/disable debug log lines
#define ENABLE_DISPLAY_OLED 0 // Enable/disable the external OLED display
#define ENABLE_DISPLAY_EINK 0 // Enable/disable the external E-Ink display
#define SENSOR_ID "plant"     // Add unique name for this sensor
#define SAMPLE_INTERVAL_SEC 5 // Sample interval (i.e. the duration between ESP wake-ups)

// Sensors
#define SOIL_MOISTURE_PIN 3    // Analog pin where soil moisture sensor is connected
#define BATTERY_VOLT_PIN 0     // Analog pin to read battery voltage
#define SOLAR_PANEL_VOLT_PIN 1 // Analog pin to read solar panel voltage
#define STATUS_LED_PIN A0      // Digital pin used by the status led

// E-Ink Display
#define EINK_BUSY_PIN D6  // E-Ink display Busy pin
#define EINK_RESET_PIN D4 // E-Ink display Reset pin
#define EINK_DC_PIN D3    // E-Ink display D/C pin

// Sensors const
#define BATTERY_MIN_VOLTS 2.8 // Minimum battery voltage level
#define BATTERY_MAX_VOLTS 4.2 // Maximum battery voltage level

#define AIR_MOISTURE_VAL 16000  // Value given by the soil moisture in the air (empirically calculated)
#define WATER_MOISTURE_VAL 6780 // Value given by the soil moisture in the water (empirically calculated)

// Loki client
// Follow https://grafana.com/blog/2021/03/08/how-i-built-a-monitoring-system-for-my-avocado-plant-with-arduino-and-grafana-cloud/?src=email&cnt=trial-started&camp=grafana-cloud-trial
#define GC_LOKI_URL "something.grafana.net"
#define GC_LOKI_USER ""
#define GC_LOKI_PASS ""
// Graphite client
#define GC_GRAPHITE_URL "something.grafana.net"
#define GC_GRAPHITE_USER ""
#define GC_GRAPHITE_PASS ""
