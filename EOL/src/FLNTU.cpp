#include "FLNTU.h"
#include <string.h>
#include <stdio.h>

FLNTU::FLNTU(HardwareSerial& serial)
    : _ser(serial), _active(false), _deadline(0) {}

bool FLNTU::begin(uint32_t timeoutMs) {
    (void)timeoutMs;
    flushRx(200);
    return true;
}

bool FLNTU::sendCmd(const char* cmd) {
    _ser.print(cmd);
    _ser.write('\r');
    _ser.flush();
    return true;
}

void FLNTU::startReading(uint32_t timeoutMs) {
    sendCmd("$run");
    _line     = "";
    _deadline = millis() + timeoutMs;
    _active   = true;
}

bool FLNTU::poll(FLNTURecord& record) {
    if (!_active) return true;

    while (_ser.available()) {
        char c = (char)_ser.read();
        if (c == '\n') {
            if (isDataLine(_line)) {
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

bool FLNTU::isDataLine(const String& s) {
    // Les lignes de données FLNTU contiennent la date (/) et l'heure (:)
    // Les lignes d'info (Ser, Ver, Ave, Pkt) n'ont ni l'un ni l'autre
    return s.length() > 20
        && s.indexOf('/') >= 0
        && s.indexOf(':') >= 0;
}

FLNTUData FLNTU::decode(const FLNTURecord& record) {
    FLNTUData d;
    d.chl = d.chlRef = d.ntu = d.ntuRef = d.thermistor = 0;
    d.date[0] = d.hms[0] = '\0';
    d.valid = false;

    // Format : "MM/DD/YY    HH:MM:SS    chl chlRef ntu ntuRef therm"
    // (séparateurs = espaces multiples ou tabulations)
    int mm = 0, dd = 0, yy = 0;
    int hh = 0, mn = 0, ss = 0;
    unsigned f1 = 0, f2 = 0, f3 = 0, f4 = 0, f5 = 0;

    int n = sscanf(record.raw,
                   "%d/%d/%d %d:%d:%d %u %u %u %u %u",
                   &mm, &dd, &yy, &hh, &mn, &ss,
                   &f1, &f2, &f3, &f4, &f5);

    if (n < 11) return d;

    d.chl        = (uint16_t)f1;
    d.chlRef     = (uint16_t)f2;
    d.ntu        = (uint16_t)f3;
    d.ntuRef     = (uint16_t)f4;
    d.thermistor = (uint16_t)f5;

    snprintf(d.date, sizeof(d.date), "%04d%02d%02d", 2000 + yy, mm, dd);
    snprintf(d.hms,  sizeof(d.hms),  "%02d:%02d:%02d", hh, mn, ss);

    d.valid = true;
    return d;
}

void FLNTU::flushRx(uint32_t ms) {
    uint32_t deadline = millis() + ms;
    while (millis() < deadline) {
        while (_ser.available()) _ser.read();
        delay(1);
    }
}
