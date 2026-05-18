/*
  EOL.ino - PiSAMI pH + CTD + FLNTU on shared Serial2, relay-multiplexed
  Hardware: Industrial Shields ESP32 PLC 21
*/

#include "src/PiSAMI.h"
#include "src/PiSAMI_decode.h"
#include "src/CTD.h"
#include "src/FLNTU.h"
#include "src/WebUI.h"
#include "src/SDLogger.h"
#include "src/NTPSync.h"

// ── Pin configuration ────────────────────────────────────────────────────────
#define PISAMI_RELAY_PIN  Q0_2
#define CTD_RELAY_PIN     Q0_0
#define FLNTU_RELAY_PIN   Q0_1

#define PISAMI_BAUD  57600
#define CTD_BAUD      9600
#define FLNTU_BAUD   19200

#define MEAS_INTERVAL_MS  (5UL * 60UL * 1000UL)

#define WIFI_SSID  "EOL"
#define WIFI_PASS  "Eol696969"

// ── Globals ──────────────────────────────────────────────────────────────────
PiSAMI pisami(Serial2, -1);
CTD    ctd(Serial2);
FLNTU  flntu(Serial2);

// ── State machine ─────────────────────────────────────────────────────────────
enum State {
    S_IDLE,
    S_CTD_SETTLE,    // relay CTD ON, attente 500 ms
    S_CTD1,          // wake-up tps, 1 s
    S_CTD2,          // mesure sans pompe, 30 s
    S_CTD3,          // mesure avec pompe, 30 s
    S_FLNTU_SETTLE,  // relay FLNTU ON, attente 500 ms
    S_FLNTU,         // $run → ligne de données, 30 s
    S_PISAMI_SETTLE, // relay PiSAMI ON, attente 3500 ms
    S_PISAMI         // F5A → Q5A 0 → R5A 0 → ACK → donnée, 120 s
};

State         state     = S_IDLE;
unsigned long nextCycle = 0;
unsigned long settleT0  = 0;
float         gSalinity = 35.0f;

// ── Affichage des records décodés ─────────────────────────────────────────────
static void printFLNTU(const FLNTUData& d) {
    webLogln("--- FLNTU -----------------------------------------");
    webLogf("  Chl signal : %5u cts  (ref %5u)\n", d.chl, d.chlRef);
    webLogf("  NTU signal : %5u cts  (ref %5u)\n", d.ntu, d.ntuRef);
    webLogf("  Thermistor : %5u cts\n", d.thermistor);
    webLogln("---------------------------------------------------");
}

static void printPiSAMIDecoded(const PiSAMI_Record& r) {
    if (!r.valid) {
        webLogf("[PiSAMI decode] Erreur %d\n", r.errorCode);
        return;
    }
    webLogln("--- PiSAMI-pH ----------------------------------");
    webLogf("  pH         : %.4f\n",  r.pH);
    webLogf("  Salinite   : %.2f PSU\n", r.salinity);
    webLogf("  T interne  : %.3f C\n", r.tempInternal);
    webLogf("  T externe  : %.3f C\n", r.tempExternal);
    webLogf("  Batterie   : %.3f V\n", r.batteryV);
    webLogln("  idx | R434  | S434  | R578  | S578");
    webLogln("  ----+-------+-------+-------+------");
    for (uint8_t i = 0; i < PISAMI_N_MEASUREMENTS; i++) {
        char type = (i < PISAMI_N_BLANKS) ? 'B' : 'M';
        uint8_t idx = (i < PISAMI_N_BLANKS) ? i : i - PISAMI_N_BLANKS;
        webLogf("  %c%-2u | %5u | %5u | %5u | %5u\n",
                type, idx,
                r.points[i].ref434, r.points[i].sig434,
                r.points[i].ref578, r.points[i].sig578);
    }
    webLogln("------------------------------------------------");
}

// ── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    pinMode(PISAMI_RELAY_PIN, OUTPUT);
    pinMode(CTD_RELAY_PIN,    OUTPUT);
    pinMode(FLNTU_RELAY_PIN,  OUTPUT);
    digitalWrite(PISAMI_RELAY_PIN, LOW);
    digitalWrite(CTD_RELAY_PIN,    LOW);
    digitalWrite(FLNTU_RELAY_PIN,  LOW);

    Serial.begin(115200);
    delay(2000);
    webLogln("EOL - PiSAMI + CTD + FLNTU");

    webUIBegin(WIFI_SSID, WIFI_PASS);

    // ── PiSAMI init ──
    webLog("[INIT] PiSAMI... ");
    digitalWrite(PISAMI_RELAY_PIN, HIGH);
    delay(500);
    Serial2.begin(PISAMI_BAUD);
    webLogln(pisami.begin() ? "OK" : "FAILED");
    Serial2.end();
    digitalWrite(PISAMI_RELAY_PIN, LOW);
    delay(200);

    // ── CTD init ──
    webLog("[INIT] CTD... ");
    digitalWrite(CTD_RELAY_PIN, HIGH);
    delay(500);
    Serial2.begin(CTD_BAUD);
    if (ctd.begin()) {
        ctd.sendCmd("getsd");
        webLogln("OK");
    } else {
        webLogln("FAILED");
    }
    Serial2.end();
    digitalWrite(CTD_RELAY_PIN, LOW);
    delay(200);

    // ── FLNTU init ──
    webLog("[INIT] FLNTU... ");
    digitalWrite(FLNTU_RELAY_PIN, HIGH);
    delay(500);
    Serial2.begin(FLNTU_BAUD);
    flntu.begin();
    Serial2.end();
    digitalWrite(FLNTU_RELAY_PIN, LOW);
    webLogln("OK");

    // ── SD card init ──
    webLog("[INIT] SD card... ");
    webLogln(sdLoggerBegin() ? "OK" : "FAILED (pas de carte ?)");

    // ── NTP via Ethernet ──
    webLog("[INIT] NTP (Ethernet)... ");
    if (ntpSync(20000)) {
        struct tm ti;
        getLocalTime(&ti, 500);
        char buf[24];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ti);
        webLogf("OK [%s UTC]\n", buf);
    } else {
        webLogln("FAILED (Ethernet ou routeur indisponible)");
    }
}

// ── Drain inline (non-bloquant) ───────────────────────────────────────────────
static void drainSerial() {
    while (Serial2.available()) Serial2.read();
}

// ── Loop (entièrement non-bloquant) ──────────────────────────────────────────
void loop() {
    webUIHandle();

    CTDRecord    crec;
    FLNTURecord  frec;
    PiSAMIRecord prec;

    switch (state) {

    // ── Attente du prochain cycle ─────────────────────────────────────────────
    case S_IDLE:
        if (millis() >= nextCycle) {
            webLogln("\n[CTD] Cycle start");
            digitalWrite(PISAMI_RELAY_PIN, LOW);
            digitalWrite(FLNTU_RELAY_PIN,  LOW);
            digitalWrite(CTD_RELAY_PIN,    HIGH);
            settleT0 = millis();
            state = S_CTD_SETTLE;
        }
        break;

    // ── Relay CTD stabilisé ? ─────────────────────────────────────────────────
    case S_CTD_SETTLE:
        if (millis() - settleT0 >= 500) {
            Serial2.begin(CTD_BAUD);
            drainSerial();
            ctd.startReading(1000);
            state = S_CTD1;
        }
        break;

    // ── CTD mesures ──────────────────────────────────────────────────────────
    case S_CTD1:
        if (ctd.poll(crec)) {
            webLog("[CTD1] ");
            webLogln(crec.ok ? crec.raw : crec.error);
            ctd.startReading(30000);
            state = S_CTD2;
        }
        break;

    case S_CTD2:
        if (ctd.poll(crec)) {
            webLog("[CTD2] ");
            webLogln(crec.ok ? crec.raw : crec.error);
            if (crec.ok) {
                CTDData cd = CTD::decode(crec);
                if (cd.valid) {
                    gSalinity = cd.salinity;
                    webLogf("[CTD2] T=%.2f C  Cond=%.3f mS/cm  Sal=%.2f PSU\n",
                            cd.temperature, cd.conductivity, cd.salinity);
                }
                sdLogCTD(cd);
            }
            ctd.startReading(30000);
            state = S_CTD3;
        }
        break;

    case S_CTD3:
        if (ctd.poll(crec)) {
            webLog("[CTD3] ");
            webLogln(crec.ok ? crec.raw : crec.error);
            if (crec.ok) {
                CTDData cd = CTD::decode(crec);
                if (cd.valid) {
                    gSalinity = cd.salinity;
                    webLogf("[CTD3] T=%.2f C  Cond=%.3f mS/cm  Sal=%.2f PSU\n",
                            cd.temperature, cd.conductivity, cd.salinity);
                }
                sdLogCTD(cd);
            }
            Serial2.end();
            digitalWrite(CTD_RELAY_PIN,   LOW);
            digitalWrite(FLNTU_RELAY_PIN, HIGH);
            settleT0 = millis();
            webLogln("[FLNTU] Starting...");
            state = S_FLNTU_SETTLE;
        }
        break;

    // ── Relay FLNTU stabilisé ? ───────────────────────────────────────────────
    case S_FLNTU_SETTLE:
        if (millis() - settleT0 >= 3500) {
            Serial2.begin(FLNTU_BAUD);
            drainSerial();
            flntu.startReading(30000);
            state = S_FLNTU;
        }
        break;

    // ── FLNTU mesure ─────────────────────────────────────────────────────────
    case S_FLNTU:
        if (flntu.poll(frec)) {
            webLog("[FLNTU] ");
            webLogln(frec.ok ? frec.raw : frec.error);
            if (frec.ok) {
                FLNTUData fd = FLNTU::decode(frec);
                if (fd.valid) printFLNTU(fd);
                sdLogFLNTU(fd);
            }
            Serial2.end();
            digitalWrite(FLNTU_RELAY_PIN,  LOW);
            digitalWrite(PISAMI_RELAY_PIN, HIGH);
            settleT0 = millis();
            webLogln("[PiSAMI] Starting (~60 s)...");
            state = S_PISAMI_SETTLE;
        }
        break;

    // ── Relay PiSAMI stabilisé + PiSAMI booté (3500 ms) ? ───────────────────
    case S_PISAMI_SETTLE:
        if (millis() - settleT0 >= 3500) {
            Serial2.begin(PISAMI_BAUD);
            drainSerial();
            pisami.startMeasurement(120000);
            state = S_PISAMI;
        }
        break;

    // ── PiSAMI mesure ────────────────────────────────────────────────────────
    case S_PISAMI:
        if (pisami.poll(prec)) {
            if (prec.ok) {
                PiSAMI_Record decoded;
                uint8_t err = PiSAMI_pH::parse(String(prec.raw), decoded, gSalinity);
                if (err == PISAMI_OK) {
                    printPiSAMIDecoded(decoded);
                    sdLogSAMI(decoded);
                } else {
                    webLog("[PiSAMI] raw: ");
                    webLogln(prec.raw);
                    webLogf("[PiSAMI decode] Erreur %d\n", err);
                }
            } else {
                webLog("[PiSAMI] FAIL: ");
                webLogln(prec.error);
            }
            Serial2.end();
            digitalWrite(PISAMI_RELAY_PIN, LOW);
            nextCycle = millis() + MEAS_INTERVAL_MS;
            webLogf("[MEAS] Next cycle in %lu min\n", MEAS_INTERVAL_MS / 60000UL);
            state = S_IDLE;
        }
        break;
    }
}
