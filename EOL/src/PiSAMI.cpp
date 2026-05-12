#include "PiSAMI.h"
#include "WebUI.h"
#include <string.h>
#include <stdio.h>

PiSAMI::PiSAMI(HardwareSerial& serial, int rtsPin)
    : _ser(serial), _rtsPin(rtsPin), _active(false), _phase(PH_F5A),
      _deadline(0), _phaseT0(0) {}

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
    while (_ser.available()) _ser.read();
    sendCmd("F5A");           // silence auto-messages (PiSAMI vient de rebooter)
    _line      = "";
    _phase     = PH_F5A;
    _phaseT0   = millis();
    _deadline  = millis() + timeoutMs;
    _active    = true;
}

bool PiSAMI::poll(PiSAMIRecord& record) {
    if (!_active) return true;

    // ── PH_F5A : attendre ":5" (silence confirmé), timeout 5 s ──────────────
    if (_phase == PH_F5A) {
        while (_ser.available()) {
            char c = (char)_ser.read();
            if (c == '\n' || c == '\r') {
                if (_line == ":5") {
                    // auto-messages silencieux, on envoie Q5A 0
                    sendCmd("Q5A 0");
                    _line    = "";
                    _phaseT0 = millis();
                    _phase   = PH_WAIT_Q;
                    return false;
                }
                _line = "";
            } else {
                _line += c;
            }
        }
        // timeout F5A : pas d'ACK, on tente quand même Q5A 0
        if (millis() - _phaseT0 >= 5000) {
            webLogln("[PiSAMI] F5A no ACK, proceeding");
            sendCmd("Q5A 0");
            _line    = "";
            _phaseT0 = millis();
            _phase   = PH_WAIT_Q;
        }
        if (millis() > _deadline) {
            strlcpy(record.error, "F5A timeout", sizeof(record.error));
            record.ok = false; _active = false; return true;
        }
        return false;
    }

    // ── PH_WAIT_Q : 1 s après Q5A, puis envoyer R5A 0 ───────────────────────
    if (_phase == PH_WAIT_Q) {
        while (_ser.available()) _ser.read();  // jeter écho Q5A
        if (millis() - _phaseT0 >= 1000) {
            while (_ser.available()) _ser.read();
            sendCmd("R5A 0");
            _line  = "";
            _phase = PH_WAIT_ACK;
        }
        if (millis() > _deadline) {
            strlcpy(record.error, "Q5A timeout", sizeof(record.error));
            record.ok = false; _active = false; return true;
        }
        return false;
    }

    // ── PH_WAIT_ACK & PH_WAIT_DATA : lecture ligne par ligne ─────────────────
    while (_ser.available()) {
        char c = (char)_ser.read();
        Serial.print(c);
        if (c == '\n' || c == '\r') {
            if (_line.length() > 0) {
                if (_phase == PH_WAIT_ACK) {
                    if (_line.indexOf(":5:70A") >= 0) {
                        Serial.println("Got ACK...Now measuring");
                        _phase = PH_WAIT_DATA;
                    }
                    _line = "";
                } else {
                    _line.toCharArray(record.raw, sizeof(record.raw));
                    record.ok = true; record.error[0] = '\0';
                    _active   = false; return true;
                }
            }
        } else {
            _line += c;
        }
    }

    if (millis() > _deadline) {
        strlcpy(record.error,
                _phase == PH_WAIT_ACK ? "No command ACK" : "Measurement timeout",
                sizeof(record.error));
        record.ok = false; _active = false; return true;
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
