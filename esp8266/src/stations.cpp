#include "stations.h"
#include "ccronexpr/ccronexpr.h"
#include "log.h"
#include <EEPROM.h>

namespace sprinkler_controller {

static uint8_t EEPROM_MARKER = 114;

static int EEPROM_SIZE = sizeof(EEPROM_MARKER) + sizeof(StationEvent) + (sizeof(Station) * NUM_STATIONS);

static uint64_t lastMillis = 0;

// forward decl
static void enable_ics();
static void disable_ics();
static void set_stations_status(uint8_t status, uint8_t enable_pin);
static void report_status(const Station &station);
static time_t get_next_station_start(const char *cron, const time_t date);
static uint8_t get_station_id(const char *topic);
static int index_of(const char *str, const char *findstr);
static bool starts_with(const char* start_str, const char* str);
static void substr(const char s[], char sub[], int p, int l);
static void substr(const char s[], char sub[], int p);

void Station::start(time_t start, long dur = 0) {
  this->started = start;
  this->is_active = true;
  if (dur > 0) {
    this->active_duration = dur > MAX_DURATION ? MAX_DURATION : dur;
  } else {
    this->active_duration = this->config_duration;
  }

  uint8_t mask = 1 << (2 * (this->id - 1));
  set_stations_status(mask, enable_pin);
}

void Station::stop() {
  this->started = 0;
  this->is_active = false;

  uint8_t mask = 2 << (2 * (this->id - 1));
  set_stations_status(mask, enable_pin);
}

void Station::to_string(char* s) {
  sprintf(s, "Station[%d] { enable_pin: %d, is_active: %d, started: %lld, duration[active]: %lu, duration[config]: %lu, cron: '%s' }\n", id, enable_pin, is_active, started, active_duration, config_duration, cron);
}

void StationController::init(NTPClient *time_client) {
  debug_printf("Initializing...\n");

  m_time_client = time_client;

  EEPROM.begin(EEPROM_SIZE);

  digitalWrite(ENABLE_ICS_PIN, LOW);
  pinMode(ENABLE_ICS_PIN, OUTPUT);

  load();

  int retries = 5;
  while(!m_time_client->forceUpdate() && retries-- > 0);

  mqttcli::init([this](char *topic, byte *payload, uint32_t length) {
    debug_printf("MQTT Message arrived [%s]\n", topic);

    char payload_str[length + 1];
    memcpy(payload_str, payload, length);
    payload_str[length] = '\0';

    if (starts_with("lawn-irrigation/interface-mode/set", topic))  {
      process_topic_mode_set(payload_str, length); // retained message
    } else if (starts_with("lawn-irrigation/enabled/set", topic))  {
      process_topic_enabled_set(payload_str, length); // retained message
    } else if (starts_with("lawn-irrigation/station", topic))  {
      Station *station = get_station_from_topic(topic);
      if (station != NULL) {
        if (m_interface_mode && index_of(topic, "set") > 0) {
          process_topic_station_set(*station, payload_str, length);
        } else if (m_interface_mode && index_of(topic, "state") > 0) {
          process_topic_station_state(*station);
        } else if (index_of(topic, "config") > 0) {
          process_topic_station_config(*station, payload_str, length); // retained message
        }
      }
    }
  });

  // subscribe to messages
  mqttcli::subscribe("lawn-irrigation/+/config");
  mqttcli::subscribe("lawn-irrigation/+/set");

  // receive and process retained messages
  int loop_idx = 10;
  while (loop_idx-- > 0) {
    mqttcli::loop();
    delay(100);
  }

  time_t now = m_time_client->getEpochTime();
  report_log("### Started at: '%lld'. Epoch Time Retries: '%d' ###\n", now, 5 - retries);

  print_state();

  debug_printf("MQTT init complete.\n");
}

void StationController::loop() {
  m_time_client->update();
  mqttcli::loop();

  // process station events every 30 seconds
  if (millis() - lastMillis >= 30*1000UL) {
      lastMillis = millis();

      process_station_event();
      next_station_event();
  }
}

void StationController::set_interface_mode(bool mode) {
  m_interface_mode = mode;

  save();

  report_interface_mode_state();
}

// TOPIC format: lawn-irrigation/station#/set
Station *StationController::get_station_from_topic(const char* topic) {
  uint8_t station_id = get_station_id(topic);
  if (station_id > 0 && station_id <= NUM_STATIONS) {
    return &(m_stations[station_id - 1]);
  }

  return NULL;
}

void StationController::check_stop_stations(bool force) {
  for (int i = 0; i < NUM_STATIONS; i++) {
    Station &station = this->m_stations[i];
    time_t now = m_time_client->getEpochTime();
    if (station.is_active == true) {
      if (force || ((now - station.started) > station.active_duration)) {
        report_log("[%lld] Stopping station %d. Started = %lld, Duration[active] = %ld, Elapsed = %lld, Forced = %d", now, station.id, station.started, station.active_duration, now - station.started, force);
        
        station.stop();

        report_status(station);
      }
    }
  }
}

StationEvent StationController::next_station_event() {
  StationEvent next_event;

  for (int i = 0; i < NUM_STATIONS; i++) {
    Station &station = m_stations[i];

    if (station.is_active == true) {
      time_t t = station.started + station.config_duration;
      if (next_event.time == 0 || next_event.time > t) {
        next_event.id = station.id;
        next_event.time = t;
        next_event.type = EventType::STOP;
      }
    } else {
      if (strlen(station.cron) > 0) {
        time_t t = get_next_station_start(station.cron, m_time_client->getEpochTime());
        if (next_event.time == 0 || next_event.time > t) {
          next_event.id = station.id;
          next_event.time = t;
          next_event.duration = station.config_duration;
          next_event.type = EventType::START;
        }
      }
    }
  }

  m_station_event = next_event;

  save();

  char msg[100];
  next_event.to_string(msg);
  debug_printf("Next Station Event: %s\n", msg);

  return next_event;
}

void StationController::process_station_event() {
  check_stop_stations();
  
  if (m_station_event.id == -1)
    return;

  if (m_station_event.type == START) {
      time_t now = m_time_client->getEpochTime();
      if (now > m_station_event.time + 30) {
        report_log("[%lld] Scheduled START event out-of-sync with the system time...\nScheduled: '%lld' vs Now: '%lld' \nSkipping event!", now, m_station_event.time, now);
      } else if (now < (m_station_event.time - 30)) {
        if (!m_interface_mode) {
          report_log("[%lld] Nothing to do yet...!", now);
        }
      } else {
        if (m_enabled) {
            check_stop_stations(true); // if we are starting a station, all other stations must be stopped

            Station &st = m_stations[m_station_event.id - 1];
            
            report_log("[%lld] Starting station %d. Duration = %ld", now, st.id, m_station_event.duration);
            
            st.start(now, m_station_event.duration);
            report_status(st);
            
            save();
        } else {
          report_log("[%lld] Skipping station START event since irrigation is disabled.", now);
        }
      }
    }
}

void StationController::process_topic_enabled_set(const char* payload_str, uint32_t length) {
  debug_printf("Processing topic 'lawn-irrigation/enabled/set'...\n");

  m_enabled = strcmp(payload_str, "on") == 0;

  debug_printf("Topic 'lawn-irrigation/enabled/set' done.\n");
}

void StationController::process_topic_station_set(Station &station, const char* payload_str, uint32_t length) {
  debug_printf("Processing topic 'lawn-irrigation/station/set'...\n");

  // get op and duration
  if (starts_with("on", payload_str)) {
    check_stop_stations(true); // if we are starting a station, all other stations must be stopped
    char dur[20] = {0};
    substr(payload_str, dur, index_of(payload_str, "|") + 1);
    
    station.start(m_time_client->getEpochTime(), atoi(dur));
    report_status(station);

  } else if (starts_with("off", payload_str)) {
    station.stop();
    report_status(station);
  }

  save();

  debug_printf("Topic 'lawn-irrigation/station/set' done.\n");
}

void StationController::process_topic_station_state(Station &station) {
  debug_printf("Processing topic 'lawn-irrigation/station/state'...\n");

  report_status(station);

  debug_printf("Topic 'lawn-irrigation/station/state' done.\n");
}

void StationController::process_topic_mode_set(const char* payload_str, uint16_t length) {
  debug_printf("Processing topic 'lawn-irrigation/interface-mode/set'...\n");
  
  // get mode
  m_interface_mode = strncmp("on", payload_str, 2) == 0;

  save();

  report_interface_mode_state();

  debug_printf("Topic 'lawn-irrigation/interface-mode/set' done.\n");
}

void StationController::process_topic_station_config(Station &station, const char* payload_str, uint32_t length) {
  debug_printf("Processing topic 'lawn-irrigation/station/config'...\n");

  // get mode
  int sep_index = index_of(payload_str, "|");

  substr(payload_str, station.cron, 0, sep_index);
  char dur[10] = {0};
  substr(payload_str, dur, sep_index + 1);

  station.config_duration = atoi(dur);

  save();

  debug_printf("Topic 'lawn-irrigation/station/config' done.\n");
}

void StationController::report_interface_mode_state() {
  mqttcli::publish("lawn-irrigation/interface-mode/state", m_interface_mode ? "on" : "off", false);
}

void StationController::load() {
  int addr = 0;

  if (EEPROM.read(addr) == EEPROM_MARKER) {
    debug_printf("Loading state from EEPROM...");

    addr += 1;

    EEPROM.get(addr, m_station_event);
    addr += sizeof(m_station_event);

    for (int i = 0; i < NUM_STATIONS; i++) {
      EEPROM.get(addr, m_stations[i]);
      addr += sizeof(Station);
    }

    debug_printf("done.\n");
  }
}

void StationController::save() {
  debug_printf("Saving state to EEPROM...");

  int addr = 0;
  EEPROM.write(addr, EEPROM_MARKER);
  addr += 1;

  EEPROM.put(addr, m_station_event);
  addr += sizeof(m_station_event);

  for (int i = 0; i < NUM_STATIONS; i++) {
    EEPROM.put(addr, m_stations[i]);
    addr += sizeof(Station);
  }

  EEPROM.commit();

  debug_printf("done.\n");
}

void StationController::print_state() {
  debug_printf("\n################################\nSystem is %s\nInterface mode = %s\n\n", m_enabled ? "ENABLED": "DISABLED", m_interface_mode ? "ON": "OFF");
  char msg[200];
  m_station_event.to_string(msg);
  debug_printf(msg);
  for (int i=0; i < NUM_STATIONS; i++) {
    char s[200];
    m_stations[i].to_string(s);
    debug_printf(s);
  }
  debug_printf("################################\n");
}

static void set_shift_register(uint8_t value) {
  shiftOut(SR_SERIAL_INPUT, SR_CLK, LSBFIRST, value);

  digitalWrite(SR_STORAGE_CLK, LOW);
  digitalWrite(SR_STORAGE_CLK, HIGH);
  digitalWrite(SR_STORAGE_CLK, LOW);
}

static void enable_ics() {
  // TODO: check which pin we can use to control the ICS
  //       https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/

  // setup
  // use RX pin as GPIO
  pinMode(SR_SERIAL_INPUT, FUNCTION_3);
  pinMode(SR_SERIAL_INPUT, OUTPUT);

  digitalWrite(STATION_1_EN_PIN, LOW);
  pinMode(STATION_1_EN_PIN, OUTPUT);
  digitalWrite(STATION_2_EN_PIN, LOW);
  pinMode(STATION_2_EN_PIN, OUTPUT);
  digitalWrite(STATION_3_EN_PIN, LOW);
  pinMode(STATION_3_EN_PIN, OUTPUT);

  digitalWrite(SR_OUTPUT_ENABLED, HIGH);
  pinMode(SR_OUTPUT_ENABLED, OUTPUT);

  digitalWrite(SR_CLK, LOW);
  pinMode(SR_CLK, OUTPUT);

  digitalWrite(SR_STORAGE_CLK, LOW);
  pinMode(SR_STORAGE_CLK, OUTPUT);

  // enable
  digitalWrite(ENABLE_ICS_PIN, HIGH);
}

static void disable_ics() {
  //disable
  digitalWrite(ENABLE_ICS_PIN, LOW);

  // revert from GPIO to RX 
  pinMode(SR_SERIAL_INPUT, FUNCTION_0);
  pinMode(SR_SERIAL_INPUT, INPUT);
  
  // float pins
  pinMode(STATION_1_EN_PIN, INPUT);
  pinMode(STATION_2_EN_PIN, INPUT);
  pinMode(STATION_3_EN_PIN, INPUT);
  pinMode(SR_OUTPUT_ENABLED, INPUT);
  pinMode(SR_CLK, INPUT);
  pinMode(SR_STORAGE_CLK, INPUT);
}

static void set_stations_status(uint8_t status, uint8_t enable_pin) {
  enable_ics();

  set_shift_register(status);

  digitalWrite(SR_OUTPUT_ENABLED, LOW);
  delay(50);
  digitalWrite(enable_pin, HIGH);
  delay(1000);
  digitalWrite(enable_pin, LOW);

  digitalWrite(SR_OUTPUT_ENABLED, HIGH);

  disable_ics();
}

static time_t get_next_station_start(const char *cron, time_t date) {
  cron_expr ce;
  const char *err = NULL;
  cron_parse_expr(cron, &ce, &err);

  if (err != NULL) {
    debug_printf("Error while parsing the CRON expr - %s\n", err);
  }

  return cron_next(&ce, date);
}

static void report_status(const Station &station) {
  char buf[64];
  sprintf(buf, "lawn-irrigation/station%d/state", station.id);
  mqttcli::publish(buf, station.is_active ? "on" : "off", false);
}

// assuming that the station ID is a single digit
static uint8_t get_station_id(const char *topic) {
  char *found = strstr(topic, "/station");
  char *pos = found ? found + 8 : NULL;
  if (pos != NULL) {
    return (*pos) - '0';
  }

  return 0;
}

static int index_of(const char *str, const char *findstr) {
  char *pos = strstr(str, findstr);
  return pos ? pos - str : -1;
}

static bool starts_with(const char* start_str, const char* str) {
  return strncmp(start_str, str, strlen(start_str)) == 0;
}

static void substr(const char s[], char sub[], int p, int l) {
   int c = 0;
   
   while (c < l) {
      sub[c] = s[p + c];
      c++;
   }
   sub[c] = '\0';
}

static void substr(const char s[], char sub[], int p) {
   int c = 0;
   
   char ch;

   do {
    ch = s[p + c];
    sub[c] = ch;
    c++;
   } while (ch != '\0');

   sub[c] = '\0';
}

} // namespace sprinkler_controller
