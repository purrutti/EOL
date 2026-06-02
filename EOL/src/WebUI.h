#pragma once
#include <Arduino.h>

// Start WiFi AP + web server (call once in setup)
void webUIBegin(const char* ssid, const char* pass);

// Start Ethernet web server (call after ntpSync, once DHCP IP is known)
void webUIEthernetBegin();

// Service pending HTTP requests — WiFi + Ethernet (call every loop iteration)
void webUIHandle();

// Log helpers — mirror to Serial and to the web debug page
void webLog(const char* msg);
void webLogln(const char* msg = "");
void webLogf(const char* fmt, ...);
