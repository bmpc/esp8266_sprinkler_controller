#include "mqttcli.h"
#include "log.h"
#include "constants.h"

#include <PubSubClient.h>

namespace sprinkler_controller::mqttcli {

static WiFiClient espClient;
static PubSubClient mqtt_client(espClient);
static char topics[10][40];
static int topic_count = 0;

static void mqtt_connect();

void init(MQTT_CALLBACK_SIGNATURE, const char** _topics, int _topic_count)  {
  mqtt_client.setServer(MQTT_BROKER, 1883);
  mqtt_client.setCallback(callback);

  if (_topic_count > 10) {
    debug_printf("Too many subscriptions. Maximum is 10!\n");
  } else {
    for (int i = 0; i < _topic_count; i++) {
       strcpy(topics[i], _topics[i]);
    }
    topic_count = _topic_count;

    mqtt_connect();
  }
}

void loop() {
  if (!mqtt_client.connected()) {
      mqtt_connect();
    }
    mqtt_client.loop();
}

void publish(const char* topic, const char* payload, bool retained) {
  mqtt_client.publish(topic, payload, retained);
  mqtt_client.flush();
}

void disconnect() {
  mqtt_client.disconnect();
}

static void mqtt_connect() {
  // Loop until we're connected. Max retries: 5
  uint8_t retries = 0;
  while (!mqtt_client.connected() && retries < 5) {
    retries++;
    debug_printf("Attempting MQTT connection...\n");
    // Create a random client ID
    char client_id[20];
    sprintf(client_id, "ESP8266Client-%04ld",random(0xffff));
    // Attempt to connect
    if (mqtt_client.connect(client_id, MQTT_USER, MQTT_PWD)) {
      debug_printf("connected\n");
      // Here is where we need to subscribe again to all endpoints
      for (int i = 0; i < topic_count; i++) {
        mqtt_client.subscribe(topics[i]);
      }
    } else {
      debug_printf("failed, rc=%d try again in 5 seconds", mqtt_client.state());

      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

} // namespace sprinkler_controller
