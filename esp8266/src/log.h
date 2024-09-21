#pragma once
#ifndef _LOG_H_
#define _LOG_H_

// #define DEBUGGING 1 // debug on USB Serial

#include <Arduino.h>
#include "mqttcli.h"

inline void debug_printf(const char * fmt, ...) {
#ifdef DEBUGGING
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
#endif
}

inline void report_log(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char msg[200];
  
  vsprintf(msg, fmt, args);
  #ifdef DEBUGGING
  debug_printf(strcat(msg, "\n"));
  #endif
  sprinkler_controller::mqttcli::publish("lawn-irrigation/log", msg, true);
  
  va_end(args);
}

inline void setupSerial() {
    #ifdef DEBUGGING
    Serial.begin(9600);
    while (!Serial) {
        ; // wait for serial port to connect. Needed for native USB port only
    }

    Serial.printf("Ready.");
    #endif
}

#endif // _LOG_H_
