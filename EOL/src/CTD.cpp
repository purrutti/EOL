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

// ── Parsing & salinity ───────────────────────────────────────────────────────

// Extrait "YYYYMMDD" et "HH:MM:SS" depuis le format CTD "DD Mon YYYY, HH:MM:SS"
static bool parseDatetime(const char* raw, char* dateOut, char* hmsOut) {
    static const char* MON[12] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    for (const char* p = raw; *p; p++) {
        for (int m = 0; m < 12; m++) {
            if (strncmp(p, MON[m], 3) != 0) continue;
            if (p == raw || *(p - 1) != ' ') continue;

            // Jour : chiffres avant l'espace précédant le mois
            const char* q = p - 2;
            while (q > raw && (*q == ' ' || *q == ',')) q--;
            if (!isdigit((uint8_t)*q)) continue;
            while (q > raw && isdigit((uint8_t)*(q - 1))) q--;
            int day = atoi(q);

            // Année : après "Mon "
            const char* yr = p + 4;
            while (*yr == ' ') yr++;
            if (!isdigit((uint8_t)*yr)) continue;
            int year = atoi(yr);

            // Heure : après les chiffres de l'année et ", "
            const char* t = yr;
            while (isdigit((uint8_t)*t)) t++;
            while (*t == ',' || *t == ' ') t++;
            if (strlen(t) < 8 || t[2] != ':' || t[5] != ':') continue;

            snprintf(dateOut, 9, "%04d%02d%02d", year, m + 1, day);
            memcpy(hmsOut, t, 8);
            hmsOut[8] = '\0';
            return true;
        }
    }
    return false;
}

int CTD::parseFields(const char* raw, float* out, int maxFields) {
    int count = 0;
    const char* p = raw;
    while (*p && count < maxFields) {
        char* end;
        double val = strtod(p, &end);
        if (end == p) break;
        out[count++] = (float)val;
        p = end;
        while (*p == ' ' || *p == ',') p++;
    }
    return count;
}

double CTD::calculateSalinity(double temperature, double conductivity_uScm,
                               double correctionFactor) {
    double a[] = { 0.0080, -0.1692, 25.3851, 14.0941, -7.0261, 2.7081 };
    double b[] = { 0.0005, -0.0056, -0.0066, -0.0375,  0.0636, -0.0144 };
    double c[] = { 0.6766097, 2.00564e-2, 1.104259e-4, -6.9698e-7, 1.0031e-9 };
    const double k     = 0.0162;
    const double C_ref = 42914.0;

    double R   = conductivity_uScm / C_ref;
    double r_t = c[0] + c[1]*temperature + c[2]*temperature*temperature
               + c[3]*temperature*temperature*temperature
               + c[4]*temperature*temperature*temperature*temperature;
    double R_t = R / r_t;

    double sqrtR = sqrt(R_t);
    double sal = a[0] + a[1]*sqrtR + a[2]*R_t + a[3]*R_t*sqrtR
               + a[4]*R_t*R_t + a[5]*R_t*R_t*sqrtR
               + ((temperature - 15.0) / (1.0 + k*(temperature - 15.0)))
               * (b[0] + b[1]*sqrtR + b[2]*R_t + b[3]*R_t*sqrtR
                  + b[4]*R_t*R_t + b[5]*R_t*R_t*sqrtR);

    return sal * correctionFactor;
}

CTDData CTD::decode(const CTDRecord& record) {
    CTDData d;
    d.temperature  = 0.0f;
    d.conductivity = 0.0f;
    d.pressure     = 0.0f;
    d.oxygen       = 0.0f;
    d.salinity     = 35.0f;
    d.valid        = false;
    d.date[0]      = '\0';
    d.hms[0]       = '\0';

    float fields[6];
    int n = parseFields(record.raw, fields, 6);
    if (n < 2) return d;

    d.temperature  = fields[0];
    d.conductivity = fields[1];   // mS/cm
    if (n >= 3) d.pressure = fields[2];
    if (n >= 4) d.oxygen   = fields[3];

    parseDatetime(record.raw, d.date, d.hms);

    if (d.conductivity > 0.0f) {
        d.salinity = (float)calculateSalinity(d.temperature,
                                               d.conductivity * 1000.0);
        d.valid = true;
    }
    return d;
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
