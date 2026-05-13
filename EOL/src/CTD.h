#pragma once
#include <Arduino.h>

#define CTD_BUF_SIZE  256

struct CTDRecord {
    bool  ok;
    char  raw[CTD_BUF_SIZE];
    char  error[64];
};

// Valeurs physiques parsées depuis une ligne CTD brute
struct CTDData {
    float temperature;   // °C           (champ 0)
    float conductivity;  // mS/cm        (champ 1)
    float oxygen;        //              (champ 2)
    float pressure;      // dbar         (champ 3)
    float salinity;      // PSU calculée
    bool  valid;         // false si parsing échoué ou conductivité <= 0
    char  date[9];       // "YYYYMMDD\0" extrait de l'horloge CTD
    char  hms[9];        // "HH:MM:SS\0" extrait de l'horloge CTD
};

class CTD {
public:
    CTD(HardwareSerial& serial);

    bool begin(uint32_t timeoutMs = 5000UL);
    bool sendCmd(const char* cmd);

    // Non-blocking measurement
    void startReading(uint32_t timeoutMs = 30000UL);
    bool poll(CTDRecord& record);

    // Décoder une ligne brute → CTDData + salinité calculée
    // conductivity attendue en mS/cm (multipliée par 1000 pour μS/cm)
    static CTDData decode(const CTDRecord& record);

private:
    HardwareSerial& _ser;
    String   _line;
    uint32_t _deadline;
    bool     _active;

    void flushRx(uint32_t ms);
    static bool isValidLine(const String& s);

    // Parse les champs numériques séparés par des virgules
    static int  parseFields(const char* raw, float* out, int maxFields);

    // Équation de salinité pratique PSS-78
    static double calculateSalinity(double temperature, double conductivity_uScm,
                                    double correctionFactor = 1.0);
};
