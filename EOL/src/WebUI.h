#pragma once
#include <Arduino.h>

void webUIBegin();
void webUIHandle();

// Logging — remplace Serial.print/println/printf dans EOL.ino
void webLog(const char* msg);
void webLogln(const char* msg = "");
void webLogf(const char* fmt, ...);
