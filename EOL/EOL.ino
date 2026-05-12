/*
  EOL.ino - PiSAMI pH + CTD on shared Serial2, relay-multiplexed
  Hardware: Industrial Shields ESP32 PLC 21
*/

#include "src/PiSAMI.h"
#include "src/CTD.h"
#include "src/WebUI.h"

// ── Pin configuration ────────────────────────────────────────────────────────
#define PISAMI_RELAY_PIN  Q0_2
#define CTD_RELAY_PIN     Q0_0

#define PISAMI_BAUD  57600
#define CTD_BAUD      9600

#define MEAS_INTERVAL_MS  (5UL * 60UL * 1000UL)

#define WIFI_SSID  "EOL"
#define WIFI_PASS  "Eol696969"

// ── Globals ──────────────────────────────────────────────────────────────────
PiSAMI pisami(Serial2, -1);
CTD    ctd(Serial2);

// ── State machine ─────────────────────────────────────────────────────────────
enum State {
    S_IDLE,
    S_CTD_SETTLE,    // relay CTD ON, attente 500 ms (millis)
    S_CTD1,          // wake-up tps, 1 s
    S_CTD2,          // mesure sans pompe, 30 s
    S_CTD3,          // mesure avec pompe, 30 s
    S_PISAMI_SETTLE, // relay PiSAMI ON, attente 1 s (millis)
    S_PISAMI         // R5A 0 → ACK → donnée, 120 s
};

State         state     = S_IDLE;
unsigned long nextCycle = 0;
unsigned long settleT0  = 0;   // timestamp début settle courant

// ── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    pinMode(PISAMI_RELAY_PIN, OUTPUT);
    pinMode(CTD_RELAY_PIN,    OUTPUT);
    digitalWrite(PISAMI_RELAY_PIN, LOW);
    digitalWrite(CTD_RELAY_PIN,    LOW);

    Serial.begin(115200);
    delay(2000);
    webLogln("EOL - PiSAMI + CTD");

    webUIBegin(WIFI_SSID, WIFI_PASS);

    // ── PiSAMI init ──
    webLog("[INIT] PiSAMI... ");
    digitalWrite(CTD_RELAY_PIN,    LOW);
    digitalWrite(PISAMI_RELAY_PIN, HIGH);
    delay(500);
    Serial2.begin(PISAMI_BAUD);
    webLogln(pisami.begin() ? "OK" : "FAILED");
    Serial2.end();
    digitalWrite(PISAMI_RELAY_PIN, LOW);
    delay(200);

    // ── CTD init ──
    webLog("[INIT] CTD... ");
    digitalWrite(PISAMI_RELAY_PIN, LOW);
    digitalWrite(CTD_RELAY_PIN,    HIGH);
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
}

// ── Drain inline (non-bloquant) ───────────────────────────────────────────────
static void drainSerial() {
    while (Serial2.available()) Serial2.read();
}

// ── Loop (entièrement non-bloquant) ──────────────────────────────────────────
void loop() {
    webUIHandle();

    CTDRecord    crec;
    PiSAMIRecord prec;

    switch (state) {

    // ── Attente du prochain cycle ─────────────────────────────────────────────
    case S_IDLE:
        if (millis() >= nextCycle) {
            webLogln("\n[CTD] Cycle start");
            digitalWrite(PISAMI_RELAY_PIN, LOW);
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
            ctd.startReading(30000);
            state = S_CTD3;
        }
        break;

    case S_CTD3:
        if (ctd.poll(crec)) {
            webLog("[CTD3] ");
            webLogln(crec.ok ? crec.raw : crec.error);
            Serial2.end();
            digitalWrite(CTD_RELAY_PIN,    LOW);
            digitalWrite(PISAMI_RELAY_PIN, HIGH);
            settleT0 = millis();
            webLogln("[PiSAMI] Starting (~60 s)...");
            state = S_PISAMI_SETTLE;
        }
        break;

    // ── Relay PiSAMI stabilisé + PiSAMI booté (2500 ms) ? ───────────────────
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
            webLog("[PiSAMI] ");
            webLogln(prec.ok ? prec.raw : prec.error);
            Serial2.end();
            digitalWrite(PISAMI_RELAY_PIN, LOW);
            nextCycle = millis() + MEAS_INTERVAL_MS;
            webLogf("[MEAS] Next cycle in %lu min\n", MEAS_INTERVAL_MS / 60000UL);
            state = S_IDLE;
        }
        break;
    }
}
