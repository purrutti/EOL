#include "PiSAMI.h"
#include <string.h>
#include <stdio.h>

PiSAMI::PiSAMI(HardwareSerial& serial, int rtsPin)
    : _ser(serial), _rtsPin(rtsPin), _active(false), _gotAck(false), _deadline(0) {}

bool PiSAMI::begin(uint32_t timeoutMs) {
    setRTS(true);
    delay(2500);
    flushRx(500);

    if (!sendCmd("F5A")) return false;

    char buf[64];
    uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline) {
        if (readLine(buf, sizeof(buf), 2000) && strcmp(buf, ":5") == 0)
            return true;
    }
    return false;
}

// ── Non-blocking API ─────────────────────────────────────────────────────────

void PiSAMI::startMeasurement(uint32_t timeoutMs) {
    flushRx(100);

    sendCmd("Q5A 0");
    delay(1000);
    flushRx(100);
    sendCmd("R5A 0");
    _line     = "";
    _gotAck   = false;
    _deadline = millis() + timeoutMs;
    _active   = true;
}

bool PiSAMI::poll(PiSAMIRecord& record) {
    if (!_active) return true;

    while (_ser.available()) {
        char c = (char)_ser.read();
        Serial.print(c);
        if (c == '\n' || c== '\r') {
            if (_line.length() > 0) {
                if (!_gotAck) {
                    if (_line.indexOf(":5:70A")>=0) {
                        _gotAck = true;
                        Serial.println("Got ACK...Now measuring");
                    }
                    _line = "";
                } else {
                    _line.toCharArray(record.raw, sizeof(record.raw));
                    record.ok       = true;
                    record.error[0] = '\0';
                    _active         = false;
                    return true;
                }
            }
        } else if (c != '\r' && c != '\n') {
            _line += c;
        }
    }

    if (millis() > _deadline) {
        strlcpy(record.error, _gotAck ? "Measurement timeout" : "No command ACK",
                sizeof(record.error));
        record.ok = false;
        _active   = false;
        return true;
    }

    return false;
}

// ── Hardware helpers ─────────────────────────────────────────────────────────

void PiSAMI::setRTS(bool high) {
    if (_rtsPin >= 0) {
        pinMode(_rtsPin, OUTPUT);
        digitalWrite(_rtsPin, high ? HIGH : LOW);
    }
}

void PiSAMI::flushRx(uint32_t ms) {
    uint32_t deadline = millis() + ms;
    while (millis() < deadline) {
        while (_ser.available()) _ser.read();
        delay(1);
    }
}

bool PiSAMI::sendCmd(const char* cmd) {
    _ser.print(cmd);
    _ser.write('\r');
    _ser.flush();
    return true;
}

bool PiSAMI::readLine(char* buf, size_t bufSize, uint32_t timeoutMs) {
    size_t   i        = 0;
    uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline) {
        if (_ser.available()) {
            char c = (char)_ser.read();
            if (c == '\r' || c == '\n') {
                if (i > 0) { buf[i] = '\0'; return true; }
            } else if (i < bufSize - 1) {
                buf[i++] = c;
            }
        } else {
            delay(1);
        }
    }
    buf[i] = '\0';
    return false;
}
