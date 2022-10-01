/**
 * This is an esp8266 project to control a lawn irrigation system using
 * solanoids for each sprinkler station. It connects to an MQTT broker on the
 * local WiFi network so that it can listen for events to turn on/off each
 * sprinkler station on the irrigation system. 
 * 
 * The system has two functioning modes:
 *   1. Background - The default mode which triggers each station based on it's cron schedule.
 *   2. Interface  - An interactive mode where users can trigger specific stations from 
 *                   MQTT events (Usually used for ad-hoc watering or to test the spinklers).
 * 
 * The mode can be selected using an MQTT event. While on Background mode, the ESP8266 is 
 * in deep spleeping and a manual restart is required for the ESP8266 to restart and activate
 * the interface mode.
 * 
 * WARNING: While on interafce mode, the ESP8266 is always on. This will degrade the battery life much faster.
 * 
 * Stations are mutual exclusive. There can only be one station active at a time, which usually is the behavior of sprinkler irrigation systems.  
 * 
 * MQTT topics:
 *  Subsribe:
 *   - lawn-irrigation/station{x}/set     not retained -> payload: on|18000 ; off
 *   - lawn-irrigation/station{x}/config      retained -> payload: cron|18000
 *   - lawn-irrigation/interface-mode/set     retained -> payload: on ; off
 *   - lawn-irrigation/enabled/set            retained -> payload: on ; off
 * 
 *  Publish:
 *   - lawn-irrigation/station{x}/state
 *   - lawn-irrigation/interface-mode/state
 *   - lawn-irrigation/log
 *
 * @file main.cpp
 * @author Bruno Conde
 * @brief
 * @version 0.1
 * @date 2022-09-18
 *
 * @copyright Copyright (c) 2022
 *
 */

#include <Arduino.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include "mqttcli.h"
#include "stations.h"
#include "log.h"
#include "constants.h"

using namespace sprinkler_controller;

StationController stctr;
WiFiUDP ntpUDP;
NTPClient time_client(ntpUDP);

void enter_deep_sleep() {
  // Find out the next event
  time_t now = time_client.getEpochTime();
  StationEvent ev = stctr.next_station_event();

  if (ev.time != EventType::NOOP && ev.time > now) {
    time_t sleep_duration = ev.time - now;
    char msg[100];
    sprintf(msg, "Entering deep sleep mode for '%lld' seconds... good night!", sleep_duration);
    DEBUG_PRINTLN(msg);

    ESP.deepSleep(sleep_duration * 1000 * 1000); // convert from seconds to microseconds
  } else {
    // if there is no next event, we default to interface mode
    DEBUG_PRINTLN("No event found to process. Defaulting to interface mode.");
    stctr.set_interface_mode(true);
  }
}

void init_wifi() {
  delay(100);
  // We start by connecting to the WiFi network
  DEBUG_PRINTLN();
  DEBUG_PRINT("Connecting to ");
  DEBUG_PRINTLN(SSID);

  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);

  WiFi.begin(SSID, PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    DEBUG_PRINT(".");
    delay(500);
  }

  randomSeed(micros());

  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("WiFi connected");
  DEBUG_PRINT("IP address: ");
  DEBUG_PRINTLN(WiFi.localIP());
}

void setup() {  
  setupSerial();

  init_wifi();
  time_client.begin();

  stctr.init(&time_client);
  stctr.process_station_event();

  if (!stctr.is_interface_mode()) {
    enter_deep_sleep();
  }

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    DEBUG_PRINTLN("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    DEBUG_PRINTLN("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    DEBUG_PRINTF("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      DEBUG_PRINTLN("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      DEBUG_PRINTLN("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      DEBUG_PRINTLN("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      DEBUG_PRINTLN("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      DEBUG_PRINTLN("End Failed");
    }
  });
  ArduinoOTA.begin();
}

void loop() {
  if (!stctr.is_interface_mode()) {
    DEBUG_PRINTLN("Interface mode switched off.");
    // if we get here, this means that the interface mode was switched off
    stctr.check_stop_stations(true);
    enter_deep_sleep();
  }

  stctr.loop();
  stctr.check_stop_stations();
  ArduinoOTA.handle();
}