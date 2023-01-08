#pragma once
#ifndef _LOG_H_
#define _LOG_H_

//#define DEBUGING 1 // debug on USB Serial

#ifdef DEBUGING
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINT_HEX(x) Serial.print(x, HEX)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTLN_HEX(x) Serial.println(x, HEX)
#define DEBUG_PRINTF(x, x1) Serial.printf(x, x1)
#define DEBUG_PRINTF2(x, x1, x2) Serial.printf(x, x1, x2)
#define DEBUG_PRINTF3(x, x1, x2, x3) Serial.printf(x, x1, x2, x3)
#define DEBUG_PRINTF4(x, x1, x2, x3, x4) Serial.printf(x, x1, x2, x3, x4)
#define DEBUG_PRINTF5(x, x1, x2, x3, x4, x5) Serial.printf(x, x1, x2, x3, x4, x5)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINT_HEX(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTLN_HEX(x)
#define DEBUG_PRINTF(x, x1)
#define DEBUG_PRINTF2(x, x1, x2)
#define DEBUG_PRINTF3(x, x1, x2, x3)
#define DEBUG_PRINTF4(x, x1, x2, x3, x4)
#define DEBUG_PRINTF5(x, x1, x2, x3, x4, x5)
#endif

#include <Arduino.h>

inline void setupSerial() {
    #ifdef DEBUGING
    Serial.begin(9600);
    while (!Serial) {
        ; // wait for serial port to connect. Needed for native USB port only
    }

    DEBUG_PRINTLN("Ready.");
    #endif
}

#endif // _LOG_H_