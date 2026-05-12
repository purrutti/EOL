/*
  EOL.ino - PiSAMI pH + CTD on shared Serial2, relay-multiplexed
  Hardware: Industrial Shields ESP32 PLC 21
*/

#include "src/PiSAMI.h"
#include "src/CTD.h"

// ── Pin configuration ────────────────────────────────────────────────────────
#define PISAMI_RELAY_PIN  Q0_2   // powers PiSAMI
#define CTD_RELAY_PIN     Q0_0   // powers CTD

#define PISAMI_BAUD  57600
#define CTD_BAUD      9600

// Interval between full measurement cycles (ms)
#define MEAS_INTERVAL_MS  (5UL * 60UL * 1000UL)

// ── Globals ──────────────────────────────────────────────────────────────────
PiSAMI pisami(Serial2, -1);
CTD    ctd(Serial2);

// ── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    pinMode(PISAMI_RELAY_PIN, OUTPUT);
    pinMode(CTD_RELAY_PIN,    OUTPUT);
    digitalWrite(PISAMI_RELAY_PIN, LOW);
    digitalWrite(CTD_RELAY_PIN,    LOW);

    Serial.begin(115200);
    delay(2000);
    Serial.println(F("EOL - PiSAMI + CTD"));

    // ── PiSAMI init ──
    Serial.print(F("[INIT] PiSAMI... "));
    digitalWrite(CTD_RELAY_PIN,    LOW);
    digitalWrite(PISAMI_RELAY_PIN, HIGH);
    delay(500);
    Serial2.begin(PISAMI_BAUD);
    if (pisami.begin()) {
        Serial.println(F("OK"));
    } else {
        Serial.println(F("FAILED"));
    }
    Serial2.end();
    digitalWrite(PISAMI_RELAY_PIN, LOW);
    delay(200);

    // ── CTD init ──
    Serial.print(F("[INIT] CTD... "));
    digitalWrite(PISAMI_RELAY_PIN, LOW);
    digitalWrite(CTD_RELAY_PIN,    HIGH);
    delay(500);
    Serial2.begin(CTD_BAUD);
    if (ctd.begin()) {
        ctd.sendCmd("getsd");
        Serial.println(F("OK"));
    } else {
        Serial.println(F("FAILED"));
    }
    Serial2.end();
    digitalWrite(CTD_RELAY_PIN, LOW);
}

// ── State machine ────────────────────────────────────────────────────────────
enum State { S_IDLE, S_CTD1, S_CTD2, S_CTD3, S_PISAMI };
State         state     = S_IDLE;
unsigned long nextCycle = 0;

// ── Loop (non-blocking) ───────────────────────────────────────────────────────
void loop() {
    CTDRecord    crec;
    PiSAMIRecord prec;

    switch (state) {

    case S_IDLE:
        if (millis() >= nextCycle) {
            Serial.println(F("\n[CTD] Cycle start"));
            digitalWrite(PISAMI_RELAY_PIN, LOW);
            digitalWrite(CTD_RELAY_PIN,    HIGH);
            delay(500);
            Serial2.begin(CTD_BAUD);
            ctd.startReading(1000);
            state = S_CTD1;
        }
        break;

    case S_CTD1:
        if (ctd.poll(crec)) {
            Serial.print(F("[CTD1] "));
            Serial.println(crec.ok ? crec.raw : crec.error);
            ctd.startReading(30000);
            state = S_CTD2;
        }
        break;

    case S_CTD2:
        if (ctd.poll(crec)) {
            Serial.print(F("[CTD2] "));
            Serial.println(crec.ok ? crec.raw : crec.error);
            ctd.startReading(30000);
            state = S_CTD3;
        }
        break;

    case S_CTD3:
        if (ctd.poll(crec)) {
            Serial.print(F("[CTD3] "));
            Serial.println(crec.ok ? crec.raw : crec.error);
            Serial2.end();
            digitalWrite(CTD_RELAY_PIN, LOW);
            delay(200);
            // ── Switch to PiSAMI ──
            Serial.println(F("[PiSAMI] Starting (~60 s)..."));
            digitalWrite(CTD_RELAY_PIN,    LOW);
            digitalWrite(PISAMI_RELAY_PIN, HIGH);
            delay(1000);
            Serial2.begin(PISAMI_BAUD);
            pisami.startMeasurement(120000);
            state = S_PISAMI;
        }
        break;

    case S_PISAMI:
        if (pisami.poll(prec)) {
            Serial.print(F("[PiSAMI] "));
            Serial.println(prec.ok ? prec.raw : prec.error);
            Serial2.end();
            digitalWrite(PISAMI_RELAY_PIN, LOW);
            nextCycle = millis() + MEAS_INTERVAL_MS;
            Serial.print(F("[MEAS] Next cycle in "));
            Serial.print(MEAS_INTERVAL_MS / 60000UL);
            Serial.println(F(" min"));
            state = S_IDLE;
        }
        break;
    }
}