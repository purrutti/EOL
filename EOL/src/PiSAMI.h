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

    // Blocking one-time init (setup only).
    bool begin(uint32_t timeoutMs = 5000UL);

    // Non-blocking measurement: call startMeasurement() once, then poll() each loop().
    // Internally runs: F5A (silence auto) → Q5A 0 (1 s) → R5A 0 → ACK → data.
    void startMeasurement(uint32_t timeoutMs = 120000UL);
    bool poll(PiSAMIRecord& record);

private:
    enum Phase {
        PH_F5A,       // sent F5A, waiting for ":5" ack to confirm silence
        PH_WAIT_Q,    // sent Q5A 0, waiting 1 s before R5A
        PH_WAIT_ACK,  // sent R5A 0, waiting for ":5:70A"
        PH_WAIT_DATA  // waiting for measurement data line
    };

    HardwareSerial& _ser;
    int      _rtsPin;
    String   _line;
    uint32_t _deadline;
    uint32_t _phaseT0;   // timestamp for millis-based phase waits
    Phase    _phase;
    bool     _active;

    void setRTS(bool high);
    void flushRx(uint32_t ms);
    bool sendCmd(const char* cmd);
    bool readLine(char* buf, size_t bufSize, uint32_t timeoutMs);
};
