#include "SDLogger.h"
#include <SD.h>
#include <time.h>

static bool _sdOk = false;

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool openAppend(File& f, const char* path, const char* header) {
    bool isNew = !SD.exists(path);
    f = SD.open(path, FILE_APPEND);
    if (!f) return false;
    if (isNew) {
        f.println(header);
    }
    return true;
}

// ── API publique ──────────────────────────────────────────────────────────────

bool sdLoggerBegin(uint8_t csPin) {
    if (!SD.begin(csPin)) {
        _sdOk = false;
        return false;
    }
    if (!SD.exists("/data")) {
        SD.mkdir("/data");
    }
    _sdOk = true;
    return true;
}

void sdLogCTD(const CTDData& data) {
    if (!_sdOk || data.date[0] == '\0') return;

    char path[36];
    snprintf(path, sizeof(path), "/data/%s_EOL_CTD.csv", data.date);

    File f;
    if (!openAppend(f, path, "Date;Time;Temperature;conductivity;calculatedPSU;Oxygen"))
        return;

    f.printf("%s;%s;%.4f;%.5f;%.2f;%.4f\n",
             data.date, data.hms,
             data.temperature, data.conductivity,
             data.salinity,    data.oxygen);
    f.close();
}

void sdLogFLNTU(const FLNTUData& data) {
    if (!_sdOk || !data.valid) return;

    char path[38];
    snprintf(path, sizeof(path), "/data/%s_EOL_FLNTU.csv", data.date);

    File f;
    if (!openAppend(f, path, "Date;Time;Chl;ChlRef;NTU;NTURef;Thermistor"))
        return;

    f.printf("%s;%s;%u;%u;%u;%u;%u\n",
             data.date, data.hms,
             data.chl, data.chlRef,
             data.ntu, data.ntuRef,
             data.thermistor);
    f.close();
}

void sdLogSAMI(const PiSAMI_Record& rec) {
    if (!_sdOk || !rec.valid) return;

    // Horodatage depuis le timestamp SAMI (UTC)
    time_t ts = (time_t)rec.unixTimestamp;
    struct tm* t = gmtime(&ts);
    char dateStr[9], hmsStr[9];
    strftime(dateStr, sizeof(dateStr), "%Y%m%d", t);
    strftime(hmsStr,  sizeof(hmsStr),  "%H:%M:%S", t);

    char path[37];
    snprintf(path, sizeof(path), "/data/%s_EOL_SAMI.csv", dateStr);

    File f;
    if (!openAppend(f, path,
                    "Date;Time;Temperature init;Temperature end;ref PSU;pH;Battery"))
        return;

    float tInit = PiSAMI_pH::adcToTemp(rec.tInitRaw);
    float tEnd  = PiSAMI_pH::adcToTemp(rec.tFinalRaw);

    f.printf("%s;%s;%.3f;%.3f;%.2f;%.4f;%.3f\n",
             dateStr, hmsStr,
             tInit, tEnd,
             rec.salinity, rec.pH, rec.batteryV);
    f.close();
}
