#include "CTD.h"
#include <string.h>
#include <stdio.h>

CTD::CTD(HardwareSerial& serial)
    : _ser(serial), _active(false), _deadline(0) {}

bool CTD::begin(uint32_t timeoutMs) {
    (void)timeoutMs;
    delay(2000);
    flushRx(500);
    return true;
}

// ── Non-blocking API ─────────────────────────────────────────────────────────

void CTD::startReading(uint32_t timeoutMs) {
    sendCmd("tps");
    _line     = "";
    _deadline = millis() + timeoutMs;
    _active   = true;
}

bool CTD::poll(CTDRecord& record) {
    if (!_active) return true;

    while (_ser.available()) {
        char c = (char)_ser.read();
        if (c == '\n') {
            if (isValidLine(_line)) {
                _line.toCharArray(record.raw, sizeof(record.raw));
                record.ok       = true;
                record.error[0] = '\0';
                _active         = false;
                return true;
            }
            _line = "";
        } else if (c != '\r') {
            _line += c;
        }
    }

    if (millis() > _deadline) {
        strlcpy(record.error, "Measurement timeout", sizeof(record.error));
        record.ok = false;
        _active   = false;
        return true;
    }

    return false;
}

bool CTD::isValidLine(const String& s) {
    return s.length() > 5
        && s.indexOf("Exe") < 0
        && s.indexOf("tps") < 0;
}

// ── Hardware helpers ─────────────────────────────────────────────────────────

bool CTD::sendCmd(const char* cmd) {
    _ser.print(cmd);
    _ser.write('\r');
    _ser.flush();
    return true;
}

void CTD::flushRx(uint32_t ms) {
    uint32_t deadline = millis() + ms;
    while (millis() < deadline) {
        while (_ser.available()) _ser.read();
        delay(1);
    }
}
