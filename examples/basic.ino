// Include your WiFi library of choice.
// Currently WiFiNINA is the only tested library.
#include <WiFiNINA.h>
// Include this library.
#include <Arduino_MQTT_Looped.h>

// Set up your secrets in a different file.
#define SSID "your_wifi_network"
#define WIFI_PASS "wifi_password"
#define MQTT_CLIENT_ID "client_id"
#define MQTT_USER "mqtt_username"
#define MQTT_PASS "mqtt_password"

const char* discoveryJson = "{ discovery message goes here }";

// Create and configure.
Arduino_MQTT_Looped mqttLooped(
  new WiFiClient(),
  SSID,
  WIFI_PASS,
  new IPAddress(127,0,0,1),
  MQTT_CLIENT_ID,
  MQTT_USER,
  MQTT_PASS
);

void setup() {
  // Birth, LWT, and discovery messages will be sent whenever the client comes online.
  mqttLooped.setBirth("your/topic/status", "online");
  mqttLooped.setWill("your/topic/status", "offline");
  mqttLooped.addDiscovery("homeassistant/light/cryptid-bottles/cryptidBottles/config", discoveryJson);
  // Subscription hooks.
  // @see https://en.cppreference.com/w/cpp/language/lambda
  mqttLooped.onMqtt("some/topic", [](String &payload){
    Serial.print("Received payload: ");
    Serial.println(payload);
  });
  mqttLooped.onMqtt("another/topic", [](String &payload){
    // do things
  });
}

void loop() {
  // The connection magic happens here.
  mqttLooped.loop();

  if (!mqttLooped.wifiIsConnected()) {
    // update an LED or something
  } else if (!mqttLooped.mqttIsConnected()) {
    // update an LED or something
  } else if (mqttLooped.mqttIsActive()) {
    // update an LED or something
  }
}
