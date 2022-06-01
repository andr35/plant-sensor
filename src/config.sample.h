#define WIFI_SSID "wifi_name"         // Add wifi name
#define WIFI_PASSWORD "wifi_password" // Add wifi passowrd

#define ID "plant" // Add unique name for this sensor
#define INTERVAL 5 // Add interval (e.g. 1 min)

// Sensors
#define SOIL_MOISTURE_PIN 3 // Analog pin where soil moisture sensor is connected
#define BATTERY_VOLT_PIN 0  // Analog pin to read battery voltage

// Sensors const
#define BATTERY_MIN_VOLTS 2.8 // Minimum battery voltage level
#define BATTERY_MAX_VOLTS 4.2 // Maximum battery voltage level

#define AIR_MOISTURE_VAL 16000  // Value given by the soil moisture in the air (empirically calculated)
#define WATER_MOISTURE_VAL 6780 // Value given by the soil moisture in the water (empirically calculated)

// Prometheus client

// Follow https://github.com/grafana/diy-iot#setting-up-and-using-grafana-cloud
#define GC_PROM_URL "prometheus-prod-01-eu-west-0.grafana.net" // Url to Prometheus instance
#define GC_PROM_PATH "/api/prom/push"                          // Path
#define GC_PORT 443
#define GC_PROM_USER "" // Username
#define GC_PROM_PASS "" // API key
#define GC_DEBUG 1      // Enable/disable log debug lines on serial
