/**
 * PiSAMI_pH.h
 * Bibliothèque Arduino pour parser et décoder les trames du capteur PiSAMI-pH
 * (Sunburst Sensors, LLC)
 *
 * Structure de la trame (nibble-alignée, ADC 14 bits) :
 *   [0-1]   Type record     : 2 nibbles (0x1B = pH data)
 *   [2-3]   Champ status    : 2 nibbles
 *   [4-6]   Champ dark/misc : 3 nibbles
 *   [7-14]  Timestamp       : 8 nibbles (uint32, secondes depuis 01/01/1904 UTC)
 *   [15-18] T_init          : 4 nibbles (ADC 14 bits, thermistance)
 *   [19-450] 27 × (Ref434 + Sig434 + Ref578 + Sig578) × 4 nibbles chacun
 *   [451-454] T_finale      : 4 nibbles
 *   [455-458] Batterie      : 4 nibbles
 *   [459-462] T_externe     : 4 nibbles
 *   [463]   Nibble trailing
 *
 * Conversions :
 *   Température  : Steinhart-Hart, R_ref=11340 Ω, ADC 14 bits
 *   Batterie     : V = count × 3.3 × 3.73 / 16383
 *   Absorbance   : A = -log10( (I/I_ref) / (I0/I0_ref) )
 *   pH           : équation Henderson-Hasselbalch avec mCP (Liu 2011)
 */

#ifndef PISAMI_PH_H
#define PISAMI_PH_H

#include <Arduino.h>
#include <math.h>

// ─── Constantes ────────────────────────────────────────────────────────────────

#define PISAMI_RECORD_TYPE_PH    0x1B   // type de record pH data
#define PISAMI_N_MEASUREMENTS    27     // 4 blancs + 23 mesures
#define PISAMI_N_BLANKS          4
#define PISAMI_HEX_LEN           465    // longueur de la chaîne hex (465 chars)
#define PISAMI_NIB_LEN           464    // nibbles utilisables (dernier nibble = trailing)

// Epoch SAMI : 01/01/1904 00:00:00 UTC exprimé en secondes Unix (epoch 1970)
// = -2082844800 secondes depuis Unix epoch
#define PISAMI_EPOCH_OFFSET      2082844800UL

// Constantes Steinhart-Hart pour la thermistance SAMI
#define THERM_A      0.001010799
#define THERM_B      0.000252164
#define THERM_C      1.979e-7
#define THERM_RREF   11340.0f        // résistance de référence (Ω)
#define THERM_ADC_MAX 16384.0f       // résolution ADC 14 bits

// Constantes batterie
#define BAT_VCC      3.3f
#define BAT_DIVIDER  3.73f
#define BAT_ADC_MAX  16383.0f

// Constantes pH (mCP purifié, Liu 2011, équations 3-10 du manuel)
// Utilisées avec T en Kelvin et salinité S
// pH = pKa' + log( (R - e1) / (e2 - R*e3) )


// ─── Structures de données ─────────────────────────────────────────────────────

/** Un point de mesure optique (4 canaux ADC 14 bits) */
struct PiSAMI_Point {
    uint16_t ref434;   // intensité référence 434 nm
    uint16_t sig434;   // intensité signal    434 nm
    uint16_t ref578;   // intensité référence 578 nm
    uint16_t sig578;   // intensité signal    578 nm
};

/** Record complet décodé d'une trame PiSAMI-pH */
struct PiSAMI_Record {
    // ── En-tête ──
    uint8_t  recordType;      // type de record (0x1B = pH)
    uint8_t  field1;          // champ status/count (nibbles 2-3)
    uint16_t field2;          // champ misc/dark (nibbles 4-6, 12 bits)
    uint32_t timestamp;       // secondes depuis 01/01/1904 UTC
    uint32_t unixTimestamp;   // secondes depuis 01/01/1970 UTC

    // ── Températures et batterie ──
    uint16_t tInitRaw;        // comptage ADC température initiale
    uint16_t tFinalRaw;       // comptage ADC température finale
    uint16_t batteryRaw;      // comptage ADC batterie
    uint16_t tExtRaw;         // comptage ADC température externe

    float    tempInternal;    // température interne moyenne (°C)
    float    tempExternal;    // température externe (°C)
    float    batteryV;        // tension batterie (V)

    // ── Points de mesure optique ──
    PiSAMI_Point points[PISAMI_N_MEASUREMENTS];

    // ── Résultats calculés ──
    uint16_t blank_ref434;    // moyenne I0 ref434 (blancs)
    uint16_t blank_sig434;    // moyenne I0 sig434 (blancs)
    uint16_t blank_ref578;    // moyenne I0 ref578 (blancs)
    uint16_t blank_sig578;    // moyenne I0 sig578 (blancs)

    float    pH;              // pH calculé (extrapolation à [ind]=0)
    float    salinity;        // salinité utilisée pour le calcul

    // ── Flags ──
    bool     valid;           // true si parsing OK
    uint8_t  errorCode;       // 0=OK, voir PiSAMI_Error
};

/** Codes d'erreur */
enum PiSAMI_Error {
    PISAMI_OK              = 0,
    PISAMI_ERR_BAD_LENGTH  = 1,   // longueur de trame incorrecte
    PISAMI_ERR_BAD_TYPE    = 2,   // record type inconnu
    PISAMI_ERR_INVALID_HEX = 3,   // caractère non hexadécimal
    PISAMI_ERR_BLANK_ZERO  = 4,   // signal blanc nul (division par zéro)
};


// ─── Classe principale ─────────────────────────────────────────────────────────

class PiSAMI_pH {
public:

    /**
     * Parser une trame hexadécimale reçue en tant que String Arduino.
     * @param hexStr  Chaîne hex brute de 465 caractères
     * @param record  Structure de sortie remplie par le parser
     * @param salinity Salinité à utiliser pour le calcul du pH (PSU, défaut 35)
     * @return code d'erreur (0 = OK)
     */
    static uint8_t parse(const String& hexStr, PiSAMI_Record& record,
                         float salinity = 35.0f);

    /**
     * Parser depuis un tableau de caractères (buffer série).
     * Cherche la première séquence de 465 caractères hex valides.
     * @param buf     Buffer caractères
     * @param bufLen  Longueur du buffer
     * @param record  Sortie
     * @param salinity Salinité PSU
     * @return code d'erreur (0 = OK)
     */
    static uint8_t parseBuffer(const char* buf, size_t bufLen,
                                PiSAMI_Record& record, float salinity = 35.0f);

    /**
     * Afficher le record décodé sur un flux Serial.
     */
    static void printRecord(const PiSAMI_Record& record, Stream& serial = Serial);

    // ── Utilitaires publics ──────────────────────────────────────────────────

    /** Convertir un comptage ADC 14-bit en température °C */
    static float adcToTemp(uint16_t adc);

    /** Convertir un comptage ADC 14-bit en tension batterie V */
    static float adcToBattery(uint16_t adc);

    /** Timestamp SAMI (depuis 1904) vers Unix (depuis 1970) */
    static uint32_t samiToUnix(uint32_t samiTs);

    /**
     * Calculer le pH à partir d'une série de points de mesure.
     * Utilise l'extrapolation pH vs concentration indicateur (Seidel 2008).
     * @param points     tableau de 23 points de mesure (hors blancs)
     * @param nPoints    nombre de points (typiquement 23)
     * @param blank434   signal blanc moyen à 434 nm
     * @param blankRef434 référence blanc à 434 nm
     * @param blank578   signal blanc moyen à 578 nm
     * @param blankRef578 référence blanc à 578 nm
     * @param tempC      température en °C
     * @param salinity   salinité en PSU
     * @return pH calculé
     */
    static float calculatePH(const PiSAMI_Point* points, uint8_t nPoints,
                              float blank434, float blankRef434,
                              float blank578, float blankRef578,
                              float tempC, float salinity);

private:

    /** Lire n nibbles à partir de la position start dans le tableau nibbles[] */
    static uint32_t readNibbles(const uint8_t* nibs, uint16_t start, uint8_t len);

    /** Convertir un char hex en nibble (0-15), retourne 0xFF si invalide */
    static uint8_t hexToNibble(char c);

    /** Convertir la chaîne hex en tableau de nibbles */
    static bool hexToNibbleArray(const char* hex, uint16_t len,
                                 uint8_t* nibs, uint16_t nibLen);

    /** Calcul interne : pKa' de mCP selon T (K) et S */
    static double calcPKa(double T_K, double S);

    /** Calcul interne : absorptivités molaires selon T (°C) */
    static void calcEpsilons(double t_C, double& e1, double& e2, double& e3);

    /** Régression linéaire pH vs concentration indicateur */
    static float extrapolatePH(const float* pH_pts, const float* conc_pts,
                                uint8_t n);
};

#endif // PISAMI_PH_H
