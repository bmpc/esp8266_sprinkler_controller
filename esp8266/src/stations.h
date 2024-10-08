#pragma once
#ifndef _STATION_H_
#define _STATION_H_

#include <arduino.h>
#include <NTPClient.h>
#include "mqttcli.h"

#define NUM_STATIONS 4

#define MAX_DURATION 1800L // 30 minutes

#define STATION_1_EN_PIN 14
#define STATION_2_EN_PIN 12
#define STATION_3_EN_PIN 13
#define STATION_4_EN_PIN 15

#define SR_SERIAL_INPUT 3
#define SR_STORAGE_CLK 2
#define SR_CLK 4 
#define SR_OUTPUT_ENABLED 0 
#define ENABLE_ICS_PIN 5

namespace sprinkler_controller {

/**
 * The Station structure which contains the station configuration as well as the
 * station state
 **/
struct Station {
  // config
  int id;
  int enable_pin;
  char cron[40];
  long config_duration; // in seconds

  // state
  bool is_active;
  time_t started;
  long active_duration; // in seconds
  
  void start(time_t start, long dur);
  void stop();
  void to_string(char* buf);
};

enum EventType { NOOP, START, STOP };

struct StationEvent {
  int8_t id = -1;
  time_t time = 0;
  long duration = 0;
  EventType type = NOOP;

  inline void to_string(char* str) {
    String t_str;
    switch(type) {
      case NOOP: t_str = "NOOP"; break;
      case START: t_str = "START"; break;
      case STOP: t_str = "STOP"; break;
    }
    sprintf(str, "Station:%d; Event:%s; At:%lld; Duration:%ld;\n", id, t_str.c_str(), time, duration);
  }
};

class StationController {
public:
  StationController() = default;

  void init(NTPClient *time_client);
  StationEvent next_station_event();
  void process_station_event();
  void check_stop_stations(bool force = false);
  void loop();
  constexpr bool is_interface_mode() {
    return m_interface_mode;
  }
  void set_interface_mode(bool mode);
private:
  NTPClient *m_time_client;
  bool m_enabled = true;
  bool m_interface_mode;
  Station m_stations[NUM_STATIONS] = {{1, STATION_1_EN_PIN, "", 0, false, 0, 0},
                                      {2, STATION_2_EN_PIN, "", 0, false, 0, 0},
                                      {3, STATION_3_EN_PIN, "", 0, false, 0, 0},
                                      {4, STATION_4_EN_PIN, "", 0, false, 0, 0}};
  StationEvent m_station_event;

  void mqtt_callback(char *topic, byte *payload, uint32_t length);
  Station *get_station_from_topic(const char* topic);
  bool can_start_station();
  void process_topic_mode_set(const char* payload_str, uint16_t length);
  void process_topic_mode_state(const char* payload_str, uint16_t length);
  void process_topic_station_set(Station &station, const char* payload_str, uint32_t length);
  void process_topic_station_state(Station &station);
  void process_topic_station_config(Station &station, const char* payload_str, uint32_t length);
  void process_topic_enabled_set(const char* payload_str, uint32_t length);
  void report_interface_mode_state();
  void load();
  void save();
  void print_state();
};

} // namespace sprinkler_controller

#endif