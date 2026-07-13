/**
 * PiSAMI_pH.cpp
 * Implémentation du parser/décodeur PiSAMI-pH pour Arduino
 */

#include "PiSAMI_decode.h"

// ─── Utilitaires bas niveau ────────────────────────────────────────────────────

uint8_t PiSAMI_pH::hexToNibble(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    return 0xFF; // invalide
}

bool PiSAMI_pH::hexToNibbleArray(const char* hex, uint16_t len,
                                   uint8_t* nibs, uint16_t nibLen) {
    if (len > nibLen) return false;
    for (uint16_t i = 0; i < len; i++) {
        uint8_t n = hexToNibble(hex[i]);
        if (n == 0xFF) return false;
        nibs[i] = n;
    }
    return true;
}

uint32_t PiSAMI_pH::readNibbles(const uint8_t* nibs, uint16_t start, uint8_t len) {
    uint32_t val = 0;
    for (uint8_t i = 0; i < len; i++) {
        val = (val << 4) | nibs[start + i];
    }
    return val;
}

// ─── Conversions physiques ─────────────────────────────────────────────────────

float PiSAMI_pH::adcToTemp(uint16_t adc) {
    if (adc == 0 || adc >= 16384) return NAN;
    double R = THERM_RREF * (double)adc / (THERM_ADC_MAX - (double)adc);
    if (R <= 0.0) return NAN;
    double lnR = log(R);
    double T_K = 1.0 / (THERM_A + THERM_B * lnR + THERM_C * lnR * lnR * lnR);
    return (float)(T_K - 273.15);
}

float PiSAMI_pH::adcToBattery(uint16_t adc) {
    return (float)adc * BAT_VCC * BAT_DIVIDER / BAT_ADC_MAX;
}

uint32_t PiSAMI_pH::samiToUnix(uint32_t samiTs) {
    // SAMI epoch 1904-01-01 est antérieur à Unix 1970-01-01
    // Différence = 2082844800 secondes
    return samiTs - PISAMI_EPOCH_OFFSET;
}

// ─── Calcul du pH ──────────────────────────────────────────────────────────────

/**
 * pKa' de mCP selon T (Kelvin) et S (PSU)
 * Équation 3 du manuel PiSAMI (Liu 2011)
 */
double PiSAMI_pH::calcPKa(double T_K, double S) {
    double sqrtS = sqrt(S);
    return -241.462
           + 7085.72  / T_K
           + 43.8332  * log(T_K)
           - 0.0806406 * T_K
           - 0.3238   * sqrtS
           + 0.0807   * S
           - 0.01157  * S * sqrtS
           + 0.000694 * S * S
           + 0.6367;
}

/**
 * Ratios d'absorptivité molaire pour mCP purifié (équations 4-10 du manuel)
 * t_C : température en °C
 */
void PiSAMI_pH::calcEpsilons(double t_C, double& e1, double& e2, double& e3) {
    // Absorptivités à 434 nm (forme HI-) et 578 nm (forme I2-)
    double a434 = 18432.0 + 23.8680  * (25.0 - t_C);
    double a578 = 120.0;
    double b434 = 2419.0 - 7.967   * (25.0 - t_C);    // calibration capteur (Eb434=2419 @ 25 C)
    double b578 = 40910.0 + 104.5411 * (25.0 - t_C);

    // e1 = a578/a434,  e2 = b578/a434,  e3 = b434/a434
    e1 = a578 / a434;
    e2 = b578 / a434;
    e3 = b434 / a434;
}

/**
 * Régression linéaire simple (moindres carrés) pour l'extrapolation pH(c→0)
 * Retourne la valeur de y à x=0 : y_intercept = mean(y) - slope * mean(x)
 */
float PiSAMI_pH::extrapolatePH(const float* pH_pts, const float* conc_pts,
                                 uint8_t n) {
    if (n < 2) return pH_pts[0];

    double sum_x = 0, sum_y = 0, sum_xx = 0, sum_xy = 0;
    for (uint8_t i = 0; i < n; i++) {
        sum_x  += conc_pts[i];
        sum_y  += pH_pts[i];
        sum_xx += (double)conc_pts[i] * conc_pts[i];
        sum_xy += (double)conc_pts[i] * pH_pts[i];
    }
    double denom = (double)n * sum_xx - sum_x * sum_x;
    if (fabs(denom) < 1e-12) {
        // droite horizontale – retourner la moyenne des pH
        return (float)(sum_y / n);
    }
    double intercept = (sum_y * sum_xx - sum_x * sum_xy) / denom;
    return (float)intercept;
}

float PiSAMI_pH::calculatePH(const PiSAMI_Point* points, uint8_t nPoints,
                               float blank434,    float blankRef434,
                               float blank578,    float blankRef578,
                               float tempC,       float salinity) {
    if (nPoints < 2) return NAN;
    if (blank434 <= 0 || blankRef434 <= 0 ||
        blank578 <= 0 || blankRef578 <= 0) return NAN;

    double T_K = (double)tempC + 273.15;
    double pKa = calcPKa(T_K, (double)salinity);
    double e1, e2, e3;
    calcEpsilons((double)tempC, e1, e2, e3);

    // Tableaux temporaires pour l'extrapolation
    float pH_arr[PISAMI_N_MEASUREMENTS];
    float conc_arr[PISAMI_N_MEASUREMENTS];  // absorbance totale ∝ [indicateur]
    uint8_t valid = 0;

    for (uint8_t i = 0; i < nPoints; i++) {
        // Absorbances normalisées par la référence
        double A434 = -log10(((double)points[i].sig434 / (double)points[i].ref434) /
                              ((double)blank434        / (double)blankRef434));
        double A578 = -log10(((double)points[i].sig578 / (double)points[i].ref578) /
                              ((double)blank578        / (double)blankRef578));

        // Ratio R = A578 / A434
        if (fabs(A434) < 1e-6) continue;
        double R = A578 / A434;

        // pH ponctuel via Henderson-Hasselbalch
        double denom_pH = e2 - R * e3;
        if (fabs(denom_pH) < 1e-12) continue;
        double ratio = (R - e1) / denom_pH;
        if (ratio <= 0.0) continue; // hors domaine de log10 -> point invalide
        double pH_point = pKa + log10(ratio);

        // Concentration indicateur proportionnelle à A434 + A578
        double conc = A434 + A578;
        if (conc < 0) continue;

        pH_arr[valid]   = (float)pH_point;
        conc_arr[valid] = (float)conc;
        valid++;
    }

    if (valid < 2) return NAN;

    // Extrapoler à [indicateur]=0 (Seidel 2008)
    return extrapolatePH(pH_arr, conc_arr, valid);
}

// ─── Parser principal ──────────────────────────────────────────────────────────

uint8_t PiSAMI_pH::parse(const String& hexStr, PiSAMI_Record& record,
                          float salinity) {
    return parseBuffer(hexStr.c_str(), hexStr.length(), record, salinity);
}

uint8_t PiSAMI_pH::parseBuffer(const char* buf, size_t bufLen,
                                 PiSAMI_Record& record, float salinity) {
    // ── Initialisation ──
    memset(&record, 0, sizeof(record));
    record.valid     = false;
    record.salinity  = salinity;

    // ── Chercher une séquence de 465 chars hex valides dans buf ──
    const char* hexStart = nullptr;
    if (bufLen >= PISAMI_HEX_LEN) {
        for (size_t i = 0; i <= bufLen - PISAMI_HEX_LEN; i++) {
            bool ok = true;
            for (uint16_t j = 0; j < PISAMI_HEX_LEN; j++) {
                if (hexToNibble(buf[i + j]) == 0xFF) { ok = false; break; }
            }
            if (ok) { hexStart = buf + i; break; }
        }
    }
    if (!hexStart) {
        record.errorCode = PISAMI_ERR_BAD_LENGTH;
        return PISAMI_ERR_BAD_LENGTH;
    }

    // ── Convertir en tableau de nibbles ──
    // Utiliser un buffer statique pour éviter malloc sur Arduino
    static uint8_t nibs[PISAMI_NIB_LEN];
    if (!hexToNibbleArray(hexStart, PISAMI_NIB_LEN, nibs, PISAMI_NIB_LEN)) {
        record.errorCode = PISAMI_ERR_INVALID_HEX;
        return PISAMI_ERR_INVALID_HEX;
    }

    // ── Lire l'en-tête ──
    record.recordType = (uint8_t) readNibbles(nibs, 0,  2);
    record.field1     = (uint8_t) readNibbles(nibs, 2,  2);
    record.field2     = (uint16_t)readNibbles(nibs, 4,  3);
    record.timestamp  = (uint32_t)readNibbles(nibs, 7,  8);
    record.tInitRaw   = (uint16_t)readNibbles(nibs, 15, 4);

    // Vérification du type de record
    if (record.recordType != PISAMI_RECORD_TYPE_PH) {
        record.errorCode = PISAMI_ERR_BAD_TYPE;
        return PISAMI_ERR_BAD_TYPE;
    }

    // ── Timestamp Unix ──
    record.unixTimestamp = samiToUnix(record.timestamp);

    // ── 27 points de mesure (nibbles 19 à 450) ──
    uint16_t pos = 19;
    for (uint8_t i = 0; i < PISAMI_N_MEASUREMENTS; i++) {
        record.points[i].ref434 = (uint16_t)readNibbles(nibs, pos,     4); pos += 4;
        record.points[i].sig434 = (uint16_t)readNibbles(nibs, pos,     4); pos += 4;
        record.points[i].ref578 = (uint16_t)readNibbles(nibs, pos,     4); pos += 4;
        record.points[i].sig578 = (uint16_t)readNibbles(nibs, pos,     4); pos += 4;
    }
    // pos devrait être 451 ici

    // ── Pied de record ──
    record.tFinalRaw  = (uint16_t)readNibbles(nibs, 451, 4);
    record.batteryRaw = (uint16_t)readNibbles(nibs, 455, 4);
    record.tExtRaw    = (uint16_t)readNibbles(nibs, 459, 4);
    // nibble 463 = trailing, ignoré

    // ── Conversions physiques ──
    float tInit  = adcToTemp(record.tInitRaw);
    float tFinal = adcToTemp(record.tFinalRaw);
    record.tempInternal = (tInit + tFinal) / 2.0f;
    record.tempExternal = adcToTemp(record.tExtRaw);
    record.batteryV     = adcToBattery(record.batteryRaw);

    // ── Calcul des moyennes des blancs (4 premiers points) ──
    uint32_t sumR4=0, sumS4=0, sumR5=0, sumS5=0;
    for (uint8_t i = 0; i < PISAMI_N_BLANKS; i++) {
        sumR4 += record.points[i].ref434;
        sumS4 += record.points[i].sig434;
        sumR5 += record.points[i].ref578;
        sumS5 += record.points[i].sig578;
    }
    record.blank_ref434 = (uint16_t)(sumR4 / PISAMI_N_BLANKS);
    record.blank_sig434 = (uint16_t)(sumS4 / PISAMI_N_BLANKS);
    record.blank_ref578 = (uint16_t)(sumR5 / PISAMI_N_BLANKS);
    record.blank_sig578 = (uint16_t)(sumS5 / PISAMI_N_BLANKS);

    if (record.blank_sig434 == 0 || record.blank_ref434 == 0 ||
        record.blank_sig578 == 0 || record.blank_ref578 == 0) {
        record.errorCode = PISAMI_ERR_BLANK_ZERO;
        return PISAMI_ERR_BLANK_ZERO;
    }

    // ── Calcul du pH ──
    // Points de mesure = points[4] à points[26] (23 points après les blancs)
    record.pH = calculatePH(
        &record.points[PISAMI_N_BLANKS],
        PISAMI_N_MEASUREMENTS - PISAMI_N_BLANKS,
        (float)record.blank_sig434, (float)record.blank_ref434,
        (float)record.blank_sig578, (float)record.blank_ref578,
        record.tempInternal,
        salinity
    );

    record.valid     = true;
    record.errorCode = PISAMI_OK;
    return PISAMI_OK;
}

// ─── Affichage ─────────────────────────────────────────────────────────────────

void PiSAMI_pH::printRecord(const PiSAMI_Record& record, Stream& serial) {
    serial.println(F("=== PiSAMI-pH Record ==="));
    serial.print(F("  Valide        : ")); serial.println(record.valid ? F("OUI") : F("NON"));

    if (!record.valid) {
        serial.print(F("  Erreur code   : ")); serial.println(record.errorCode);
        return;
    }

    serial.print(F("  Type record   : 0x"));
    serial.println(record.recordType, HEX);

    // Timestamp
    serial.print(F("  Timestamp SAMI: ")); serial.print(record.timestamp);
    serial.print(F(" (Unix: ")); serial.print(record.unixTimestamp); serial.println(F(")"));

    // Temperatures
    serial.print(F("  T interne     : "));
    serial.print(record.tempInternal, 3); serial.println(F(" °C"));
    serial.print(F("  T externe     : "));
    serial.print(record.tempExternal, 3); serial.println(F(" °C"));

    // Batterie
    serial.print(F("  Batterie      : "));
    serial.print(record.batteryV, 3); serial.println(F(" V"));

    // Blancs
    serial.println(F("  Blancs (moy.) :"));
    serial.print(F("    Ref434=")); serial.print(record.blank_ref434);
    serial.print(F(" Sig434=")); serial.print(record.blank_sig434);
    serial.print(F(" Ref578=")); serial.print(record.blank_ref578);
    serial.print(F(" Sig578=")); serial.println(record.blank_sig578);

    // pH
    serial.print(F("  pH calculé    : "));
    if (isnan(record.pH)) {
        serial.println(F("N/A"));
    } else {
        serial.print(record.pH, 4); serial.println(F(" (mCP, sal.const.)"));
    }

    // Points de mesure
    serial.println(F("  Points (idx | R434 | S434 | R578 | S578) :"));
    for (uint8_t i = 0; i < PISAMI_N_MEASUREMENTS; i++) {
        serial.print(i < PISAMI_N_BLANKS ? F("  [B") : F("  [M"));
        if (i < 10) serial.print(F("0"));
        serial.print(i < PISAMI_N_BLANKS ? i : i - PISAMI_N_BLANKS);
        serial.print(F("] "));
        serial.print(record.points[i].ref434); serial.print('\t');
        serial.print(record.points[i].sig434); serial.print('\t');
        serial.print(record.points[i].ref578); serial.print('\t');
        serial.println(record.points[i].sig578);
    }
    serial.println(F("========================"));
}
