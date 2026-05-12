#pragma once
#include <Arduino.h>

#define CTD_BUF_SIZE  256

struct CTDRecord {
    bool  ok;
    char  raw[CTD_BUF_SIZE];
    char  error[64];
};

class CTD {
public:
    CTD(HardwareSerial& serial);

    bool begin(uint32_t timeoutMs = 5000UL);
    bool sendCmd(const char* cmd);

    // Non-blocking measurement: call startReading() once, then poll() each loop().
    // poll() returns true when a valid line is received or the timeout expires.
    void startReading(uint32_t timeoutMs = 30000UL);
    bool poll(CTDRecord& record);

private:
    HardwareSerial& _ser;
    String   _line;
    uint32_t _deadline;
    bool     _active;

    void flushRx(uint32_t ms);
    static bool isValidLine(const String& s);
};
