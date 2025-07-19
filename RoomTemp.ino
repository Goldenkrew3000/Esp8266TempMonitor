#include "MAX6675.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Wifi
const char* ssid = "";
const char* pswd = "";
const char* mqtt_broker = "192.168.5.254";
const char* mqtt_clientid = "EspTempMonitor";

// MAX6675 pins
const int max6675_miso = 12;
const int max6675_sck = 14;
const int max6675_cs = 15;

// Float storage
const int sample_amount = 30; // 15 seconds, 2x a second
float samples[sample_amount] = { 0.00 };
int sample_idx = 0;

// Thermocouple
MAX6675 ktype(max6675_cs, max6675_miso, max6675_sck);
int ktype_status = 0;
float ktype_tempc = 0.00;
float ktype_avg = 0.00;

// MQTT
WiFiClient wifiClient;
PubSubClient mqtt_client(wifiClient);
void callback(char* topic, byte* payload, unsigned int length) {}
char* mqtt_msg = NULL;
int rc = 0;
int keepalive_idx = 0;

void setup() {
    Serial.begin(115200);
    printf("\n\n\n");
    randomSeed(micros());
    SPI.begin();

    // Start Wifi
    printf("Connecting to SSID %s\n", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pswd);
    while (WiFi.status() != WL_CONNECTED) {
        printf(".");
        delay(250);
    } printf("\n");
    printf("Connected to Wifi\n");

    // Start thermocouple
    ktype.begin();
    ktype.setSPIspeed(4000000);

    // Connect to MQTT
    mqtt_client.setServer(mqtt_broker, 1883);
    mqtt_client.setCallback(callback);
}

void mqtt_reconnect() {
    while (!mqtt_client.connected()) {
        if (mqtt_client.connect(mqtt_clientid)) {
            printf("Connected to MQTT\n");
        } else {
            delay(5000); // Wait 5 seconds before reconnecting
        }
    }
}

void loop() {
    // Check mqtt connection and send MQTT Keepalive (every 5 seconds)
    if (!mqtt_client.connected()) {
        printf("(MQTT) Not connected\n");
        mqtt_reconnect();
    }

    if (keepalive_idx < 10) {
        keepalive_idx++;
    } else {
        keepalive_idx = 0;
        printf("(MQTT) Send KA\n");
        mqtt_client.loop();
    }

    // Read thermocouple
    if (sample_idx < sample_amount) {
        ktype_status = ktype.read();
        ktype_tempc = ktype.getCelsius();
        printf("(TC) ST: %d TmpC: %.2f IDX: %d\n", ktype_status, ktype_tempc, sample_idx);
        if (ktype_status != 0) {
            // Failure to read thermocouple, just duplicate result from last read
            samples[sample_idx] = samples[sample_idx - 1];
            printf("Failed to read thermocouple!\n");
        } else {
            samples[sample_idx] = ktype_tempc;
        }
        sample_idx++;
    } else {
        // Calculate average
        ktype_avg = 0.00;
        for (int i = 0; i < sample_amount; i++) {
            ktype_avg += samples[i];
        }
        ktype_avg = ktype_avg / sample_amount;
        printf("(TC) AVG: %.2f\n", ktype_avg);

        // Reset vars
        sample_idx = 0;
        samples[sample_amount] = { 0.00 };

        // Publish MQTT message
        rc = asprintf(&mqtt_msg, "{\"temp_c\": %.2f}\n", ktype_avg);
        if (rc == -1) {
            // This is a fatal error, most likely from a memory leak. Force a reset
            printf("MALLOC FAIL - asprintf could not malloc from heap!\n");
            ESP.reset();
        }
        printf("(MQTT) MSG: %s", mqtt_msg); // Newline already included in string
        mqtt_client.publish("roomTemp/json", mqtt_msg);
        
        // Free heap memory
        free(mqtt_msg);
        mqtt_msg = NULL;
    }
    
    delay(500);
}
