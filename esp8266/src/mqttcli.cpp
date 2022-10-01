#include "mqttcli.h"
#include "log.h"
#include "constants.h"

#include <PubSubClient.h>

namespace sprinkler_controller::mqttcli {

static WiFiClient espClient;
static PubSubClient mqtt_client(espClient);

static void mqtt_connect();

void init(MQTT_CALLBACK_SIGNATURE)  {
  mqtt_client.setServer(MQTT_BROKER, 1883);
  mqtt_client.setCallback(callback);
  mqtt_connect();
}

void loop() {
  if (!mqtt_client.connected()) {
      mqtt_connect();
    }
    mqtt_client.loop();
}

void publish(const char* topic, const char* payload, bool retained) {
  mqtt_client.publish(topic, payload, retained);
}

void subscribe(const char* topic) {
  mqtt_client.subscribe(topic);
}

static void mqtt_connect() {
  // Loop until we're connected. Max retries: 5
  uint8_t retries = 0;
  while (!mqtt_client.connected() && retries < 5) {
    retries++;
    DEBUG_PRINTLN("Attempting MQTT connection...");
    // Create a random client ID
    char client_id[20];
    sprintf(client_id, "ESP8266Client-%04X",random(0xffff));
    // Attempt to connect
    if (mqtt_client.connect(client_id, MQTT_USER, MQTT_PWD)) {
      DEBUG_PRINTLN("connected");
    } else {
      DEBUG_PRINT("failed, rc=");
      DEBUG_PRINT(mqtt_client.state());
      DEBUG_PRINTLN(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

} // namespace sprinkler_controller