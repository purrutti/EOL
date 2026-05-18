#pragma once
#include <Arduino.h>

#define FLNTU_BUF_SIZE 128

struct FLNTURecord {
    bool ok;
    char raw[FLNTU_BUF_SIZE];
    char error[64];
};

// Valeurs extraites d'une ligne de mesure FLNTU
struct FLNTUData {
    uint16_t chl;        // signal fluorescence (ex. 695 nm)
    uint16_t chlRef;     // référence fluorescence
    uint16_t ntu;        // signal turbidité (ex. 700 nm)
    uint16_t ntuRef;     // référence turbidité
    uint16_t thermistor; // comptage thermistance
    char date[9];        // "YYYYMMDD\0"
    char hms[9];         // "HH:MM:SS\0"
    bool valid;
};

class FLNTU {
public:
    FLNTU(HardwareSerial& serial);

    bool begin(uint32_t timeoutMs = 5000UL);
    bool sendCmd(const char* cmd);

    // Non-blocking measurement: envoie "$run" puis attend la ligne de données
    void startReading(uint32_t timeoutMs = 30000UL);
    bool poll(FLNTURecord& record);

    // Décoder la ligne brute → FLNTUData
    static FLNTUData decode(const FLNTURecord& record);

private:
    HardwareSerial& _ser;
    String   _line;
    uint32_t _deadline;
    bool     _active;

    void flushRx(uint32_t ms);

    // Ligne de données : contient '/' (date) ET ':' (heure)
    static bool isDataLine(const String& s);
};
