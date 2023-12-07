// Include your WiFi library of choice.
// ** Currently WiFiNINA is the only tested library. **
#include <WiFiNINA.h>
// Include this library.
#include <MQTT_Looped.h>

// Set up your secrets in a different file.
#define SSID "your_wifi_network"
#define WIFI_PASS "wifi_password"
#define MQTT_CLIENT_ID "client_id"
#define MQTT_USER "mqtt_username"
#define MQTT_PASS "mqtt_password"

const char* discoveryJson = "{ discovery message goes here }";

// Create and configure.
MQTT_Looped mqttLooped(
  // The WiFi client likely needs no configuration. The SSID and Password will be set
  // by the MQTT_Looped class.
  new WiFiClient(),
  // Your WiFi network name and password.
  SSID,
  WIFI_PASS,
  // The IP address of the MQTT broker you want to connect to.
  new IPAddress(127,0,0,1),
  // Default MQTT port is usually 1883.
  1883,
  MQTT_USER,
  MQTT_PASS,
  // The client ID is arbitrary and identifies your device.
  MQTT_CLIENT_ID
);

void setup() {
  // Birth, LWT, and discovery messages are optional and will be sent whenever the
  // client connects with the MQTT broker.

  // A birth message simply announces that your service is online.
  mqttLooped.setBirth("your/topic/status", "online");

  // Last Will and Testament messages are sent by the broker if your service disconnects
  // ungracefully.
  // @see https://www.hivemq.com/blog/mqtt-essentials-part-9-last-will-and-testament/
  mqttLooped.setWill("your/topic/status", "offline");

  // Discovery messages are used by some services, such as Home Assisstant, to auto-configure
  // an IoT connection with your device.
  // @see https://www.home-assistant.io/integrations/mqtt#mqtt-discovery
  mqttLooped.addDiscovery("homeassistant/light/your-component/your-object-id/config", discoveryJson);

  // Subscription hooks. These hooks will subscribe to the given topic and, when said topic
  // is received, call the callback lambda function with the received payload.
  // @see https://en.cppreference.com/w/cpp/language/lambda
  mqttLooped.onMqtt("some/topic", [](char* payload, uint8_t /*len*/){
    Serial.print("Received payload: ");
    Serial.println(payload);
    uint16_t value = strtol(payload, nullptr, 10);
    // do things
  });

  // Discoveries should be sent when the broker announces it is online.
  mqttLooped.onMqtt("homeassistant/status", [&](char* payload, uint8_t /*len*/){
    if (strcmp(payload, "online") == 0) {
      mqttLooped.sendDiscoveries();
    }
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
