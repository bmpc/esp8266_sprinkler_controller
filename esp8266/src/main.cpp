/**
 * This is an esp8266 project to control a lawn irrigation system using a
 * solanoid for each sprinkler station. It connects to an MQTT broker on the
 * local WiFi network so that it can listen for events to turn on/off each
 * sprikler station on the irrigation system. A fail safe timeout is enforced by
 * the esp8266 in the case a STOP message is not reveived from the MQTT broker
 * to turn off the irrigation for a particular station.
 *
 * MQTT Topics:
 * Subscribe: lawn-irrigation/station{x}/set -> payload: on|18000 ; off
 * Send: lawn-irrigation/station{x}/state
 *
 * @file main.cpp
 * @author Bruno Conde
 * @brief
 * @version 0.1
 * @date 2022-07-23
 *
 * @copyright Copyright (c) 2022
 *
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

//#define DEBUG 1 // debug on USB Serial

#ifdef DEBUG
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINT_HEX(x) Serial.print(x, HEX)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTLN_HEX(x) Serial.println(x, HEX)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINT_HEX(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTLN_HEX(x)
#endif

const char* ssid = "<WIFI_SSID>";
const char* password = "<WIFI_PWD>";

const char* mqtt_broker = "<MQTT_IP>";
const char* mqtt_user = "<MQTT_user>";
const char* mqtt_pwd = "<MQTT_pwd>";

#define NUM_STATIONS 3
#define MAX_DURATION 1800000 // 30 minutes

#define STATION_1_PIN 14
#define STATION_2_PIN 12
#define STATION_3_PIN 13

WiFiClient espClient;
PubSubClient mqtt_client(espClient);

struct StationStatus {
  int id;
  int pin;
  bool is_active;
  uint64_t started;
  uint64_t duration;

  void start(long dur) {
    DEBUG_PRINT("Starting station [");
    DEBUG_PRINT(this->id);
    DEBUG_PRINTLN("]");
    this->started = millis();
    this->is_active = true;
    this->duration = dur > MAX_DURATION ? MAX_DURATION : dur;

    digitalWrite(LED_BUILTIN, LOW);
    digitalWrite(this->pin, HIGH);

    report_status();
  }

  void stop() {
    DEBUG_PRINT("Stopping station [");
    DEBUG_PRINT(this->id);
    DEBUG_PRINTLN("]");
    this->started = 0;
    this->is_active = false;
    this->duration = 0;

    digitalWrite(LED_BUILTIN, HIGH);
    digitalWrite(this->pin, LOW);

    report_status();
  }

  void report_status() {
    char buf[64];
    sprintf(buf, "lawn-irrigation/station%d/state", this->id);
    mqtt_client.publish(buf, this->is_active ? "on" : "off");
  }
};

StationStatus stations[NUM_STATIONS] = {{1, STATION_1_PIN, false, 0, 0},
                                        {2, STATION_2_PIN, false, 0, 0},
                                        {3, STATION_3_PIN, false, 0, 0}};

void setup_wifi() {
  delay(100);
  // We start by connecting to the WiFi network
  DEBUG_PRINTLN();
  DEBUG_PRINT("Connecting to ");
  DEBUG_PRINTLN(ssid);

  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    DEBUG_PRINT(".");
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
  }

  randomSeed(micros());

  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("WiFi connected");
  DEBUG_PRINT("IP address: ");
  DEBUG_PRINTLN(WiFi.localIP());
}

void mqtt_connect() {
  // Loop until we're connected. Max retries: 5
  uint8_t retries = 0;
  while (!mqtt_client.connected() && retries < 5) {
    retries++;
    DEBUG_PRINTLN("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (mqtt_client.connect(clientId.c_str(), mqtt_user, mqtt_pwd)) {
      mqtt_client.subscribe("lawn-irrigation/+/set");

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

// TOPIC format: lawn-irrigation/station#/set
StationStatus *get_station_from_topic(String &topic) {
  String station = topic.substring(topic.indexOf("/station") + 8, topic.lastIndexOf('/'));
  station.trim();
  int station_id = station.toInt();
  if (station_id > 0 && station_id <= NUM_STATIONS) {
    return &(stations[station_id - 1]);
  }

  return NULL;
}

bool can_start_station() {
  for (int i = 0; i < NUM_STATIONS; i++) {
    if (stations[i].is_active == true)
      return false;
  }

  return true;
}

void process_set_topic(StationStatus *station, byte *payload, unsigned int length) {
  // get op and duration
  char payloadBuf[length + 1];
  memcpy(payloadBuf, payload, length);
  payloadBuf[length] = '\0';

  String payloadStr(payloadBuf);

  if (payloadStr.startsWith("on") && can_start_station()) {
    String dur = payloadStr.substring(payloadStr.indexOf('|') + 1);
    station->start(dur.toInt());
  } else if (payloadStr.startsWith("off")) {
    station->stop();
  }
}

void process_state_topic(StationStatus *station) { station->report_status(); }

void mqtt_callback(char *topic, byte *payload, unsigned int length) {
  DEBUG_PRINT("MQTT Message arrived [");
  DEBUG_PRINT(topic);
  DEBUG_PRINT("] ");
  DEBUG_PRINTLN();

  String topicStr(topic);
  StationStatus *station = get_station_from_topic(topicStr);
  if (station != NULL) {
    if (topicStr.indexOf("set") > 0) {
      process_set_topic(station, payload, length);
    } else if (topicStr.indexOf("state") > 0) {
      process_state_topic(station);
    }
  }
}

void stop_stations() {
  for (int i = 0; i < NUM_STATIONS; i++) {
    StationStatus &station = stations[i];
    if (station.is_active == true && ((millis() - station.started) > station.duration)) {
      DEBUG_PRINTLN("Station needs to stop!");
      station.stop();
    }
  }
}

void serial_flush() {
  while (Serial.available() > 0) {
    Serial.read();
  }
}

void setup() {
  // Open serial communications and wait for port to open
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  // initialize pins
  pinMode(LED_BUILTIN, OUTPUT);
  for (int i = 0; i < NUM_STATIONS; i++) {
    pinMode(stations[i].pin, OUTPUT);
  }

  setup_wifi();

  mqtt_client.setServer(mqtt_broker, 1883);
  mqtt_client.setCallback(mqtt_callback);

  mqtt_connect();

  DEBUG_PRINTLN("Ready.");

  serial_flush();
}

void loop() {
  if (!mqtt_client.connected()) {
    mqtt_connect();
  }
  stop_stations();
  mqtt_client.loop();

  // TODO: don't know if this will drain the battery... probably we should add a
  // delay here before each loop
}