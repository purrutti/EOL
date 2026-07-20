/*
  EOL.ino - PiSAMI pH + CTD + FLNTU on shared Serial2, relay-multiplexed
  Hardware: Industrial Shields ESP32 PLC 21
*/

//#include <Ethernet.h>
//#include <NTPClient.h>
#include "src/PiSAMI.h"
#include "src/PiSAMI_decode.h"
#include "src/CTD.h"
#include "src/FLNTU.h"
#include "src/WebUI.h"
#include "src/SDLogger.h"
#include "src/EthernetManager.h"
#include <time.h>
#include <esp_system.h>

const char* version = "EOL v1.0 - " __DATE__ " " __TIME__;

// ── Raison du redemarrage (diagnostic des trous de mesure) ──────────────────
static const char* resetReasonStr(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "power-on";
        case ESP_RST_EXT:       return "reset externe";
        case ESP_RST_SW:        return "reset logiciel";
        case ESP_RST_PANIC:     return "panic/exception";
        case ESP_RST_INT_WDT:   return "watchdog interruption";
        case ESP_RST_TASK_WDT:  return "watchdog tache";
        case ESP_RST_WDT:       return "watchdog (autre)";
        case ESP_RST_DEEPSLEEP: return "reveil deep-sleep";
        case ESP_RST_BROWNOUT:  return "brownout (sous-tension)";
        case ESP_RST_SDIO:      return "reset SDIO";
        default:                return "inconnue";
    }
}

// ── Pin configuration ────────────────────────────────────────────────────────
#define GSM_RELAY_PIN     Q0_5
#define SPARE_RELAY_PIN     Q0_4
#define PISAMI_RELAY_PIN  Q0_2
#define CTD_RELAY_PIN     Q0_0
#define FLNTU_RELAY_PIN   Q0_1

#define PISAMI_BAUD  57600
#define CTD_BAUD      9600
#define FLNTU_BAUD   19200

//#define MEAS_INTERVAL_MS  (5UL * 60UL * 1000UL)
#define MEAS_INTERVAL_MS  (30UL * 60UL * 1000UL)

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
    S_PISAMI,        // F5A → Q5A 0 → R5A 0 → ACK → donnée, 120 s
    S_FTP_GSM_ON,   // relay GSM LOW (modem ON), attente power-up
    S_FTP_NTP,      // poll NTP → confirme connexion internet, met à jour RTC
    S_FTP_UPLOAD,   // upload FTP puis relay GSM HIGH (modem OFF)
};

State         loopState     = S_IDLE;
unsigned long nextCycle = 0;
unsigned long settleT0  = 0;
#define DEFAULT_SALINITY 35.0f
float         gSalinity = DEFAULT_SALINITY;

// Retries CTD en cas de trame illisible (caracteres parasites sur la liaison RS232)
#define CTD_MAX_RETRIES 5
static uint8_t ctdRetryCount = 0;

// ── Scheduling (mesures à HH:00/HH:30, FTP à 00:00/12:00 UTC) ───────────────
static time_t        _lastMeasTime  = 0;
static time_t        _lastFtpTime   = 0;
static unsigned long _ftpRetryAfter = 0;   // backoff après échec FTP (millis)
static unsigned long _ftpStateT0    = 0;   // timer états FTP
static unsigned long _ftpNtpNextTry = 0;   // rate-limit poll NTP

#define GSM_SETTLE_MS    5000UL   // power-up modem
#define NTP_TIMEOUT_MS  360000UL  // timeout attente connexion GSM + NTP
//#define NTP_TIMEOUT_MS  30000UL  // timeout attente connexion GSM + NTP

// ── Affichage des records décodés ─────────────────────────────────────────────
static void printFLNTU(const FLNTUData& d) {
    webLogln("--- FLNTU -----------------------------------------");
    webLogf("  Chl signal : signal: %5u  (wavelength %5u)\n",  d.chlVal, d.chl);
    webLogf("  NTU signal : signal: %5u  (wavelength %5u)\n", d.ntuVal, d.ntu);
    webLogf("  Thermistor : %5u\n", d.thermistor);
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
    pinMode(GSM_RELAY_PIN,    OUTPUT);
    pinMode(PISAMI_RELAY_PIN, OUTPUT);
    pinMode(CTD_RELAY_PIN,    OUTPUT);
    pinMode(FLNTU_RELAY_PIN,  OUTPUT);
    digitalWrite(GSM_RELAY_PIN,    LOW);   // modem GSM allumé au démarrage - laisse le temps d'etablir la connexion avant le 1er cycle FTP
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
    sdLogError("BOOT", "redemarrage - raison: %s", resetReasonStr(esp_reset_reason()));

    // ── RTC init + restauration heure ──
    webLog("[INIT] RTC (DS3231)... ");
    bool rtcOk = rtcBegin();
    webLogln(rtcOk ? "OK" : "FAILED");
    if (rtcOk && rtcSyncToSystem()) {
        time_t now = time(nullptr);
        char buf[24];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", gmtime(&now));
        webLogf("[RTC] Heure restauree [%s UTC]\n", buf);
    } else if (rtcOk) {
        webLogln("[RTC] Heure invalide (RTC jamais sync)");
    }

    // ── Ethernet : IP fixe uniquement — le NTP est tente plus tard dans la
    // machine a etats, apres le 1er cycle de mesures (cf. S_IDLE), pour
    // laisser au modem GSM (allume depuis le debut du setup) le temps
    // d'etablir sa connexion avant le premier essai.
    webLog("[INIT] Ethernet... ");
    ethernetInit();
    webLogln("OK");

    // ── Serveur web Ethernet (démarre si DHCP a fourni une IP) ──
    webUIEthernetBegin();
}

// ── Drain inline (non-bloquant) ───────────────────────────────────────────────
static void drainSerial() {
    while (Serial2.available()) Serial2.read();
}

// ── Scheduling helpers ────────────────────────────────────────────────────────
// Dernier créneau mesure écoulé (HH:00 ou HH:30 UTC).
static time_t lastMeasSlot() {
    time_t now = time(nullptr);
    time_t secsInHour = now % 3600UL;
    time_t hourStart  = now - secsInHour;
    return (secsInHour >= 1800UL) ? hourStart + 1800UL : hourStart;
}

// Dernier créneau FTP écoulé (00:00 UTC uniquement, une fois par jour).
static time_t lastFtpSlot() {
    time_t now = time(nullptr);
    return now - (now % 86400UL);  // minuit UTC du jour courant
}

static bool measDue() {
    time_t now = time(nullptr);
    if (now < 1000000000UL) return millis() >= nextCycle;  // fallback sans horloge
    return _lastMeasTime < lastMeasSlot();
}

static bool ftpDue() {
    time_t now = time(nullptr);
    if (now < 1000000000UL) return false;
    if (millis() < _ftpRetryAfter) return false;
    return _lastFtpTime < lastFtpSlot();
}

// ── Loop (entièrement non-bloquant) ──────────────────────────────────────────
void loop() {
    webUIHandle();

    CTDRecord    crec;
    FLNTURecord  frec;
    PiSAMIRecord prec;

    switch (loopState) {

    // ── Attente du prochain cycle ─────────────────────────────────────────────
    case S_IDLE:
        // Priorite aux mesures : au boot, mesures et FTP sont typiquement dus
        // en meme temps, et faire les mesures d'abord laisse au modem GSM
        // (allume depuis le setup) le temps d'etablir sa connexion avant le
        // premier essai NTP/FTP.
        if (measDue()) {
            _lastMeasTime = lastMeasSlot();
            nextCycle = millis() + MEAS_INTERVAL_MS;  // fallback si perte d'horloge
            gSalinity = DEFAULT_SALINITY;              // reset, mis a jour si CTD valide
            time_t now = time(nullptr);
            struct tm* g = gmtime(&now);
            webLogf("\n[CTD] Cycle %02d:%02d UTC\n", g->tm_hour, g->tm_min);
            digitalWrite(PISAMI_RELAY_PIN, LOW);
            digitalWrite(FLNTU_RELAY_PIN,  LOW);
            digitalWrite(CTD_RELAY_PIN,    HIGH);
            settleT0 = millis();
            loopState = S_CTD_SETTLE;
            break;
        }
        if (ftpDue()) {
            webLogln("[FTP] Creneau d'envoi - activation modem GSM...");
            digitalWrite(GSM_RELAY_PIN, LOW);  // modem ON (deja allume au boot, sans effet sinon)
            _ftpStateT0 = millis();
            loopState = S_FTP_GSM_ON;
        }
        break;

    // ── Relay CTD stabilisé ? ─────────────────────────────────────────────────
    case S_CTD_SETTLE:
        if (millis() - settleT0 >= 500) {
            Serial2.begin(CTD_BAUD);
            drainSerial();
            ctdRetryCount = 0;
            ctd.startReading(1000);
            loopState = S_CTD1;
        }
        break;

    // ── CTD mesures ──────────────────────────────────────────────────────────
    case S_CTD1:
        if (ctd.poll(crec)) {
            webLog("[CTD1] ");
            webLogln(crec.ok ? crec.raw : crec.error);
            ctd.startReading(30000);
            loopState = S_CTD2;
        }
        break;

    case S_CTD2:
        if (ctd.poll(crec)) {
            webLog("[CTD2] ");
            webLogln(crec.ok ? crec.raw : crec.error);
            CTDData cd;
            cd.valid = false;
            if (crec.ok) cd = CTD::decode(crec);
            if (!cd.valid && ctdRetryCount < CTD_MAX_RETRIES) {
                ctdRetryCount++;
                webLogf("[CTD2] Trame invalide - nouvelle mesure (%u/%u)\n",
                        ctdRetryCount, CTD_MAX_RETRIES);
                ctd.startReading(30000);
                break;
            }
            ctdRetryCount = 0;
            if (cd.valid) {
                gSalinity = cd.salinity;
                webLogf("[CTD2] T=%.2f C  Cond=%.3f mS/cm  Sal=%.2f PSU\n",
                        cd.temperature, cd.conductivity, cd.salinity);
            } else if (crec.ok) {
                sdLogError("CTD", "decode echec: %s", crec.raw);
                webLogf("[CTD2] Decode invalide - salinite par defaut %.1f PSU\n", DEFAULT_SALINITY);
            } else {
                sdLogError("CTD", crec.error);
                webLogf("[CTD2] Echec mesure - salinite par defaut %.1f PSU\n", DEFAULT_SALINITY);
            }
            //sdLogCTD(cd);
            ctd.startReading(30000);
            loopState = S_CTD3;
        }
        break;

    case S_CTD3:
        if (ctd.poll(crec)) {
            webLog("[CTD3] ");
            webLogln(crec.ok ? crec.raw : crec.error);
            CTDData cd;
            cd.valid = false;
            if (crec.ok) cd = CTD::decode(crec);
            if (!cd.valid && ctdRetryCount < CTD_MAX_RETRIES) {
                ctdRetryCount++;
                webLogf("[CTD3] Trame invalide - nouvelle mesure (%u/%u)\n",
                        ctdRetryCount, CTD_MAX_RETRIES);
                ctd.startReading(30000);
                break;
            }
            ctdRetryCount = 0;
            if (cd.valid) {
                gSalinity = cd.salinity;
                webLogf("[CTD3] T=%.2f C  Cond=%.3f mS/cm  Sal=%.2f PSU\n",
                        cd.temperature, cd.conductivity, cd.salinity);
            } else if (crec.ok) {
                sdLogError("CTD", "decode echec: %s", crec.raw);
                webLogf("[CTD3] Decode invalide - salinite par defaut %.1f PSU\n", DEFAULT_SALINITY);
            } else {
                sdLogError("CTD", crec.error);
                webLogf("[CTD3] Echec mesure - salinite par defaut %.1f PSU\n", DEFAULT_SALINITY);
            }
            sdLogCTD(cd);
            Serial2.end();
            digitalWrite(CTD_RELAY_PIN,   LOW);
            digitalWrite(FLNTU_RELAY_PIN, HIGH);
            settleT0 = millis();
            webLogln("[FLNTU] Starting...");
            loopState = S_FLNTU_SETTLE;
        }
        break;

    // ── Relay FLNTU stabilisé ? ───────────────────────────────────────────────
    case S_FLNTU_SETTLE:
        if (millis() - settleT0 >= 3500) {
            Serial2.begin(FLNTU_BAUD);
            drainSerial();
            flntu.startReading(30000);
            loopState = S_FLNTU;
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
                else          sdLogError("FLNTU", "decode echec: %s", frec.raw);
                sdLogFLNTU(fd);
            } else {
                sdLogError("FLNTU", frec.error);
            }
            Serial2.end();
            digitalWrite(FLNTU_RELAY_PIN,  LOW);
            digitalWrite(PISAMI_RELAY_PIN, HIGH);
            settleT0 = millis();
            webLogln("[PiSAMI] Starting (~60 s)...");
            loopState = S_PISAMI_SETTLE;
        }
        break;

    // ── Relay PiSAMI stabilisé + PiSAMI booté (3500 ms) ? ───────────────────
    case S_PISAMI_SETTLE:
        if (millis() - settleT0 >= 3500) {
            Serial2.begin(PISAMI_BAUD);
            drainSerial();
            pisami.startMeasurement(120000);
            loopState = S_PISAMI;
        }
        break;

    // ── PiSAMI mesure ────────────────────────────────────────────────────────
    case S_PISAMI:
        if (pisami.poll(prec)) {
            if (prec.ok) {
                sdLogSAMIRaw(prec.raw);
                PiSAMI_Record decoded;
                webLogf("[PiSAMI] Calcul pH avec salinite = %.2f PSU%s\n",
                        gSalinity, (gSalinity == DEFAULT_SALINITY) ? " (defaut)" : " (CTD)");
                uint8_t err = PiSAMI_pH::parse(String(prec.raw), decoded, gSalinity);
                if (err == PISAMI_OK) {
                    printPiSAMIDecoded(decoded);
                    sdLogSAMI(decoded);
                } else {
                    webLog("[PiSAMI] raw: ");
                    webLogln(prec.raw);
                    webLogf("[PiSAMI decode] Erreur %d\n", err);
                    sdLogError("PISAMI", "decode erreur %d", err);
                }
            } else {
                webLog("[PiSAMI] FAIL: ");
                webLogln(prec.error);
                sdLogError("PISAMI", prec.error);
            }
            Serial2.end();
            digitalWrite(PISAMI_RELAY_PIN, LOW);
            nextCycle = millis() + MEAS_INTERVAL_MS;  // fallback si perte d'horloge
            loopState = S_IDLE;
        }
        break;
    // ── FTP : power-up modem GSM ─────────────────────────────────────────────
    case S_FTP_GSM_ON:
        if (millis() - _ftpStateT0 >= GSM_SETTLE_MS) {
            webLogln("[FTP] Modem GSM actif - attente NTP...");
            ntpSessionBegin();
            _ftpNtpNextTry = 0;
            _ftpStateT0    = millis();
            loopState = S_FTP_NTP;
        }
        break;

    // ── FTP : attente NTP (confirme connexion internet + sync RTC) ────────────
    case S_FTP_NTP:
        if (millis() >= _ftpNtpNextTry) {
            _ftpNtpNextTry = millis() + 5000;
            if (ntpSessionPoll()) {
                ntpSessionEnd();
                time_t n = time(nullptr);
                char buf[24];
                strftime(buf, sizeof(buf), "%d %b %Y, %H:%M:%S", gmtime(&n));
                webLogf("[FTP] NTP OK [%s UTC] - debut upload\n", buf);
                loopState = S_FTP_UPLOAD;
                break;
            }
        }
        if (millis() - _ftpStateT0 >= NTP_TIMEOUT_MS) {
            ntpSessionEnd();
            webLogln("[FTP] Timeout NTP - modem GSM reste allumé");
            sdLogError("FTP", "timeout NTP - upload annule");
            _ftpRetryAfter = millis() + 15UL * 60UL * 1000UL;
            //digitalWrite(GSM_RELAY_PIN, HIGH);  // modem OFF
            loopState = S_IDLE;
        }
        break;

    // ── FTP : upload puis extinction modem ────────────────────────────────────
    case S_FTP_UPLOAD: {
        uint8_t sent = ftpUploadDaily();
        if (sent > 0) {
            _lastFtpTime = time(nullptr);
            webLogf("[FTP] %u/3 fichiers envoyes - modem GSM eteint\n", sent);
            digitalWrite(GSM_RELAY_PIN, HIGH);  // modem OFF 
        } else {
            webLogln("[FTP] Echec upload - retry dans 15 min - modem GSM eteint");
            sdLogError("FTP", "upload echoue (0/3)");
            _ftpRetryAfter = millis() + 15UL * 60UL * 1000UL;
            digitalWrite(GSM_RELAY_PIN, HIGH);  // modem OFF
        }
        
        loopState = S_IDLE;
        break;
    }
    default: loopState = S_IDLE;
        break;

    }
}
