#include "SDLogger.h"
#include <SD.h>
#include <time.h>
#include <stdarg.h>

static bool _sdOk = false;

#define ERRORS_LOG_PATH      "/data/errors.log"
#define ERRORS_MARKER_PATH   "/data/errors_created.txt"
#define ERRORS_LOG_MAX_BYTES (50UL * 1024UL * 1024UL)  // 50 Mo

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool openAppend(File& f, const char* path, const char* header) {
    bool isNew = !SD.exists(path);
    f = SD.open(path, FILE_APPEND);
    if (!f) return false;
    if (isNew) f.println(header);
    return true;
}

// Remplit dateStr ("YYYYMMDD") et hmsStr ("HH:MM:SS") depuis l'horloge système.
// Retourne false si l'heure système n'est pas encore valide.
static bool nowStrings(char dateStr[9], char hmsStr[9]) {
    time_t t = time(nullptr);
    if (t < 1000000000UL) return false;
    struct tm* gmt = gmtime(&t);
    strftime(dateStr, 9, "%Y%m%d", gmt);
    strftime(hmsStr,  9, "%H:%M:%S", gmt);
    return true;
}

// ── API publique ──────────────────────────────────────────────────────────────

bool sdLoggerBegin(uint8_t csPin) {
    if (!SD.begin(csPin)) { _sdOk = false; return false; }
    if (!SD.exists("/data")) SD.mkdir("/data");
    _sdOk = true;
    return true;
}

void sdLogCTD(const CTDData& data) {
    if (!_sdOk || !data.valid) return;

    time_t t = time(nullptr);
    if (t < 1000000000UL) return;
    struct tm* gmt = gmtime(&t);

    char dateStr[9];
    strftime(dateStr, sizeof(dateStr), "%Y%m%d", gmt);
    char dtStr[24];
    strftime(dtStr, sizeof(dtStr), "%d %b %Y, %H:%M:%S", gmt);

    char path[36];
    snprintf(path, sizeof(path), "/data/%s_EOL_CTD.csv", dateStr);

    File f;
    if (!openAppend(f, path, "Temperature,Conductivity,Oxygen,Salinity,Date,Time")) {
        sdLogError("SD", "impossible d'ouvrir %s", path);
        return;
    }

    f.printf("%.4f,%.5f,%.3f,%.4f,%s\n",
             data.temperature, data.conductivity,
             data.oxygen, data.salinity, dtStr);
    f.close();
}

void sdLogFLNTU(const FLNTUData& data) {
    if (!_sdOk || !data.valid) return;

    char dateStr[9], hmsStr[9];
    if (!nowStrings(dateStr, hmsStr)) return;

    char path[38];
    snprintf(path, sizeof(path), "/data/%s_EOL_FLNTU.csv", dateStr);

    File f;
    if (!openAppend(f, path, "Date;Time;Chl_WL;Chl_Val;NTU_WL;NTU_Val;Thermistor")) {
        sdLogError("SD", "impossible d'ouvrir %s", path);
        return;
    }

    f.printf("%s;%s;%u;%u;%u;%u;%u\n",
             dateStr, hmsStr,
             data.chl, data.chlVal,
             data.ntu, data.ntuVal,
             data.thermistor);
    f.close();
}

void sdLogSAMI(const PiSAMI_Record& rec) {
    if (!_sdOk || !rec.valid) return;

    char dateStr[9], hmsStr[9];
    if (!nowStrings(dateStr, hmsStr)) return;

    char path[37];
    snprintf(path, sizeof(path), "/data/%s_EOL_SAMI.csv", dateStr);

    File f;
    if (!openAppend(f, path,
                    "Date;Time;Temperature init;Temperature end;ref PSU;pH;Battery")) {
        sdLogError("SD", "impossible d'ouvrir %s", path);
        return;
    }

    float tInit = PiSAMI_pH::adcToTemp(rec.tInitRaw);
    float tEnd  = PiSAMI_pH::adcToTemp(rec.tFinalRaw);

    f.printf("%s;%s;%.3f;%.3f;%.2f;%.4f;%.3f\n",
             dateStr, hmsStr,
             tInit, tEnd,
             rec.salinity, rec.pH, rec.batteryV);
    f.close();
}

void sdLogSAMIRaw(const char* raw) {
    if (!_sdOk) return;

    char dateStr[9], hmsStr[9];
    if (!nowStrings(dateStr, hmsStr)) return;

    char path[40];
    snprintf(path, sizeof(path), "/data/%s_EOL_SAMI_RAW.csv", dateStr);

    File f;
    if (!openAppend(f, path, "Date;Time;Raw")) {
        sdLogError("SD", "impossible d'ouvrir %s", path);
        return;
    }

    f.printf("%s;%s;%s\n", dateStr, hmsStr, raw);
    f.close();
}

// Écrit /data/errors_created.txt s'il n'existe pas encore, pour dater
// la version courante de errors.log (utilisé par le nommage FTP).
static void errorsLogTouchCreated(const char* dateStr) {
    if (SD.exists(ERRORS_MARKER_PATH)) return;
    File f = SD.open(ERRORS_MARKER_PATH, FILE_WRITE);
    if (!f) return;
    f.print(dateStr);
    f.close();
}

bool sdErrorsLogCreationDate(char dateOut[9]) {
    File f = SD.open(ERRORS_MARKER_PATH, FILE_READ);
    if (!f) return false;
    int n = f.readBytesUntil('\n', dateOut, 8);
    f.close();
    dateOut[n] = '\0';
    return n >= 8;
}

void sdLogError(const char* source, const char* fmt, ...) {
    if (!_sdOk) return;

    time_t t = time(nullptr);
    char dtStr[24];
    char dateStr[9];
    if (t >= 1000000000UL) {
        struct tm* gmt = gmtime(&t);
        strftime(dtStr,   sizeof(dtStr),   "%d %b %Y, %H:%M:%S", gmt);
        strftime(dateStr, sizeof(dateStr), "%Y%m%d", gmt);
    } else {
        strlcpy(dtStr, "no-time", sizeof(dtStr));
    }

    char msg[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    File f = SD.open(ERRORS_LOG_PATH, FILE_APPEND);
    if (!f) return;
    if (t >= 1000000000UL) errorsLogTouchCreated(dateStr);
    f.printf("%s,%s,%s\n", dtStr, source, msg);
    uint32_t size = f.size();
    f.close();

    // Coupe-circuit : evite de saturer la carte SD si errors.log grossit
    // plus vite que la fenetre FTP quotidienne ne peut l'evacuer.
    if (size > ERRORS_LOG_MAX_BYTES) {
        SD.remove(ERRORS_LOG_PATH);
        SD.remove(ERRORS_MARKER_PATH);
    }
}
