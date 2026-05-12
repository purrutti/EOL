#pragma once
#include <Arduino.h>

#define PISAMI_BUF_SIZE  512

struct PiSAMIRecord {
    bool  ok;
    char  raw[PISAMI_BUF_SIZE];
    char  error[64];
};

class PiSAMI {
public:
    PiSAMI(HardwareSerial& serial, int rtsPin = -1);

    // Blocking one-time init: silences auto-messages, verifies comms.
    bool begin(uint32_t timeoutMs = 5000UL);

    // Non-blocking measurement: call startMeasurement() once, then poll() each loop().
    // poll() returns true when data is received or the timeout expires.
    void startMeasurement(uint32_t timeoutMs = 120000UL);
    bool poll(PiSAMIRecord& record);

private:
    HardwareSerial& _ser;
    int      _rtsPin;
    String   _line;
    uint32_t _deadline;
    bool     _active;
    bool     _gotAck;

    void setRTS(bool high);
    void flushRx(uint32_t ms);
    bool sendCmd(const char* cmd);
    bool readLine(char* buf, size_t bufSize, uint32_t timeoutMs);
};
