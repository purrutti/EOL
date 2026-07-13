#include "EthernetManager.h"
#include "SDLogger.h"
#include <Ethernet.h>
#include <Dns.h>
#include <NTPClient.h>
#include <RTClib.h>
#include <SD.h>
#include <sys/time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ── RTC DS3231 ───────────────────────────────────────────────────────────────
static RTC_DS3231 _rtc;
static bool       _rtcOk = false;

bool rtcBegin() {
    _rtcOk = _rtc.begin();
    return _rtcOk;
}

bool rtcSyncToSystem() {
    if (!_rtcOk) return false;
    DateTime dt = _rtc.now();
    if (dt.year() < 2020 || dt.year() > 2100) return false;
    struct timeval tv = { (time_t)dt.unixtime(), 0 };
    settimeofday(&tv, nullptr);
    return true;
}

bool rtcSetFromSystem() {
    if (!_rtcOk) return false;
    time_t t = time(nullptr);
    if (t < 1000000000UL) return false;
    _rtc.adjust(DateTime((uint32_t)t));
    return true;
}

// ── NTP ───────────────────────────────────────────────────────────────────────
static byte      _mac[]     = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
static IPAddress _ethIP     (192, 168,   2, 231);
static IPAddress _ethDNS    (192, 168,   2,   1);
static IPAddress _ethGW     (192, 168,   2,   1);
static IPAddress _ethSubnet (255, 255, 255,   0);

struct _NtpCtx {
    uint32_t      timeoutMs;
    volatile bool done;
    volatile bool synced;
};

static void _ntpTask(void* pv) {
    auto* ctx = (_NtpCtx*)pv;

    //Ethernet.init(ETH_CS_PIN);
    Ethernet.begin(_mac, _ethIP, _ethDNS, _ethGW, _ethSubnet);

    EthernetUDP udp;
    NTPClient   client(udp, "pool.ntp.org", 0, 0);
    client.begin();

    uint32_t t0 = millis();
    while (millis() - t0 < ctx->timeoutMs) {
        if (client.update()) {
            unsigned long epoch = client.getEpochTime();
            if (epoch > 1000000000UL) {
                struct timeval tv = { (time_t)epoch, 0 };
                settimeofday(&tv, nullptr);
                ctx->synced = true;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    client.end();  // libère le socket UDP W5500 — vTaskDelete ne déclenche pas les destructeurs C++
    ctx->done = true;
    vTaskDelete(nullptr);
}

bool ethernetBegin(uint32_t timeoutMs) {
    _NtpCtx ctx = { timeoutMs, false, false };
    xTaskCreate(_ntpTask, "ntpSync", 8192, &ctx, 1, nullptr);

    // delay(100) cède le CPU aux idle tasks → watchdog nourri pendant la requête NTP
    uint32_t deadline = millis() + timeoutMs + 3000;
    while (!ctx.done && millis() < deadline) {
        delay(100);
    }
    if (ctx.synced) rtcSetFromSystem();
    return ctx.synced;
}

// ── FTP upload ────────────────────────────────────────────────────────────────
#define FTP_HOST       "oceane.obs-vlfr.fr"
#define FTP_PORT       21
#define FTP_USER       "leo"
#define FTP_PASS       "leocnrs"
#define FTP_TOUT_MS    15000UL
#define FTP_NDAYS_CHECK 10

extern void webLogf(const char* fmt, ...);
extern void webLogln(const char* msg);

static int ftpReadCode(EthernetClient& c, String* lineOut = nullptr) {
    uint32_t t0 = millis();
    while (millis() - t0 < FTP_TOUT_MS) {
        if (c.available()) {
            String line = c.readStringUntil('\n');
            if (line.length() < 3) continue;
            if (line.length() >= 4 && line[3] == '-') continue;  // continuation
            if (lineOut) *lineOut = line;
            return line.substring(0, 3).toInt();
        }
        delay(20);
    }
    return -1;
}

static bool ftpCmd(EthernetClient& c, const String& cmd, int expected) {
    c.print(cmd); c.print("\r\n");
    return ftpReadCode(c) == expected;
}

static bool parsePasv(const String& line, IPAddress& ip, uint16_t& port) {
    int s = line.indexOf('('), e = line.indexOf(')');
    if (s < 0 || e <= s) return false;
    int n[6] = {}, idx = 0, prev = s + 1;
    for (int i = s + 1; i <= e && idx < 6; i++) {
        if (line[i] == ',' || line[i] == ')') {
            n[idx++] = line.substring(prev, i).toInt();
            prev = i + 1;
        }
    }
    if (idx < 6) return false;
    ip = IPAddress(n[0], n[1], n[2], n[3]);
    port = (uint16_t)n[4] * 256 + n[5];
    return true;
}

// Envoie le fichier local /data/<localName> sous le nom <remoteName> dans
// remoteDir. Les deux noms diffèrent pour errors.log (daté à l'upload).
static bool ftpUploadFileAs(EthernetClient& ctrl, const char* localName, const char* remoteName,
                            const char* remoteDir, IPAddress serverIP) {
    String sdPath = String("/data/") + localName;
    if (!SD.exists(sdPath.c_str())) {
        webLogf("[FTP] %s absent, ignore\n", localName);
        return false;
    }
    if (!ftpCmd(ctrl, String("CWD ") + remoteDir, 250)) {
        webLogf("[FTP] CWD %s echec\n", remoteDir);
        sdLogError("FTP", "CWD %s echec", remoteDir);
        return false;
    }
    if (!ftpCmd(ctrl, "TYPE I", 200)) return false;

    ctrl.print("PASV\r\n");
    String pasvLine;
    if (ftpReadCode(ctrl, &pasvLine) != 227) {
        webLogln("[FTP] PASV echec");
        sdLogError("FTP", "PASV echec pour %s", remoteName);
        return false;
    }
    IPAddress dataIP; uint16_t dataPort;
    if (!parsePasv(pasvLine, dataIP, dataPort)) {
        webLogln("[FTP] PASV parse echec");
        sdLogError("FTP", "PASV parse echec pour %s", remoteName);
        return false;
    }
    // serverIP est résolu une fois en amont (DNS) — ignore l'IP du PASV (NAT)
    webLogf("[FTP] data → %d.%d.%d.%d:%u\n",
            serverIP[0], serverIP[1], serverIP[2], serverIP[3], dataPort);

    EthernetClient data;
    if (!data.connect(serverIP, dataPort)) {
        webLogln("[FTP] connexion data echec");
        sdLogError("FTP", "connexion data %d.%d.%d.%d:%u echec (%s)",
                   serverIP[0], serverIP[1], serverIP[2], serverIP[3], dataPort, remoteName);
        return false;
    }

    ctrl.print("STOR "); ctrl.print(remoteName); ctrl.print("\r\n");
    if (ftpReadCode(ctrl) != 150) {
        data.stop();
        webLogf("[FTP] STOR %s echec\n", remoteName);
        sdLogError("FTP", "STOR %s echec", remoteName);
        return false;
    }

    File f = SD.open(sdPath.c_str(), FILE_READ);
    if (!f) { data.stop(); return false; }

    uint8_t buf[512];
    uint32_t total = 0;
    while (f.available()) {
        int n = f.read(buf, sizeof(buf));
        if (n > 0) { data.write(buf, n); total += n; }
    }
    f.close();
    data.stop();  // EOF → déclenche le 226 côté serveur

    bool ok = (ftpReadCode(ctrl) == 226);
    if (ok) webLogf("[FTP] %s → %s (%lu o) OK\n", remoteName, remoteDir, total);
    else  { webLogf("[FTP] %s transfert incomplet\n", remoteName);
            sdLogError("FTP", "%s transfert incomplet (%lu o)", remoteName, total); }
    return ok;
}

static bool ftpUploadFile(EthernetClient& ctrl, const char* fname,
                          const char* remoteDir, IPAddress serverIP) {
    return ftpUploadFileAs(ctrl, fname, fname, remoteDir, serverIP);
}

struct _FtpEntry { const char* suffix; const char* dir; };
static const _FtpEntry _ftpMap[4] = {
    { "CTD",      "/Data/CTD"  },
    { "FLNTU",    "/Data/FLUO" },
    { "SAMI",     "/Data/SAMI" },
    { "SAMI_RAW", "/Data/SAMI" },
};
static const uint8_t _ftpMapCount = sizeof(_ftpMap) / sizeof(_ftpMap[0]);

// Retourne true si fname existe dans le répertoire distant (commande SIZE).
// 213 = présent, 550 = absent, autre = serveur ne supporte pas SIZE → false.
static bool ftpFileExists(EthernetClient& ctrl, const char* dir, const char* fname) {
    if (!ftpCmd(ctrl, String("CWD ") + dir, 250)) return false;
    ctrl.print("SIZE "); ctrl.print(fname); ctrl.print("\r\n");
    return ftpReadCode(ctrl) == 213;
}

// Vérifie les FTP_NDAYS_CHECK derniers jours pour chaque capteur et renvoi
// les fichiers présents sur SD mais absents du FTP.
// Retourne le nombre de fichiers rattrapés.
static uint8_t ftpCheckAndRepair(EthernetClient& ctrl, IPAddress serverIP, time_t now) {
    uint8_t repaired = 0;
    for (int d = 1; d <= FTP_NDAYS_CHECK; d++) {
        time_t day = now - (time_t)d * 86400UL;
        char dayStr[9];
        strftime(dayStr, sizeof(dayStr), "%Y%m%d", gmtime(&day));

        for (uint8_t i = 0; i < _ftpMapCount; i++) {
            char fname[32];
            snprintf(fname, sizeof(fname), "%s_EOL_%s.csv", dayStr, _ftpMap[i].suffix);

            char sdPath[42];
            snprintf(sdPath, sizeof(sdPath), "/data/%s", fname);
            if (!SD.exists(sdPath)) continue;

            if (ftpFileExists(ctrl, _ftpMap[i].dir, fname)) continue;

            webLogf("[FTP] Rattrapage J-%d: %s\n", d, fname);
            if (ftpUploadFile(ctrl, fname, _ftpMap[i].dir, serverIP)) repaired++;
        }
    }
    return repaired;
}

uint8_t ftpUploadDaily() {
    time_t now = time(nullptr);
    if (now < 1000000000UL) {
        webLogln("[FTP] Heure invalide, upload annule");
        return 0;
    }
    time_t yesterday = now - 86400UL;
    char date[9];
    strftime(date, sizeof(date), "%Y%m%d", gmtime(&yesterday));

    // Résolution DNS une seule fois — évite ctrl.remoteIP() qui peut être 0 sur ESP32
    IPAddress serverIP;
    DNSClient dns;
    dns.begin(Ethernet.dnsServerIP());
    if (dns.getHostByName(FTP_HOST, serverIP) != 1) {
        webLogln("[FTP] DNS echec");
        sdLogError("FTP", "DNS echec pour %s", FTP_HOST);
        return 0;
    }
    webLogf("[FTP] Connexion %s (%d.%d.%d.%d)...\n",
            FTP_HOST, serverIP[0], serverIP[1], serverIP[2], serverIP[3]);

    EthernetClient ctrl;
    if (!ctrl.connect(serverIP, FTP_PORT)) {
        webLogln("[FTP] Connexion echouee");
        sdLogError("FTP", "connexion %s echouee", FTP_HOST);
        return 0;
    }

    if (ftpReadCode(ctrl) != 220)                          { ctrl.stop(); return 0; }
    if (!ftpCmd(ctrl, String("USER ") + FTP_USER, 331))    { ctrl.stop(); return 0; }
    if (!ftpCmd(ctrl, String("PASS ") + FTP_PASS, 230)) {
        webLogln("[FTP] Auth echouee");
        sdLogError("FTP", "auth echouee sur %s", FTP_HOST);
        ctrl.stop(); return 0;
    }
    webLogln("[FTP] Connecte");

    uint8_t count = 0;
    for (uint8_t i = 0; i < _ftpMapCount; i++) {
        char fname[32];
        snprintf(fname, sizeof(fname), "%s_EOL_%s.csv", date, _ftpMap[i].suffix);
        if (ftpUploadFile(ctrl, fname, _ftpMap[i].dir, serverIP)) count++;
    }

    webLogf("[FTP] J-1 : %d/%d fichiers envoyes - verification %d derniers jours...\n",
            count, _ftpMapCount, FTP_NDAYS_CHECK);
    uint8_t repaired = ftpCheckAndRepair(ctrl, serverIP, now);
    if (repaired > 0)
        webLogf("[FTP] Rattrapage : %u fichier(s) manquant(s) renvoyes\n", repaired);
    else
        webLogln("[FTP] Verification N derniers jours : RAS");

    // errors.log : nom distant date sur sa date de creation (et non celle du
    // jour d'upload) pour ne jamais ecraser une version precedente sur le FTP
    // apres une rotation (suppression locale > 50 Mo, cf. sdLogError).
    if (SD.exists("/data/errors.log")) {
        char createdDate[9];
        if (!sdErrorsLogCreationDate(createdDate)) {
            strftime(createdDate, sizeof(createdDate), "%Y%m%d", gmtime(&now));
        }
        char remoteName[24];
        snprintf(remoteName, sizeof(remoteName), "%s_errors.log", createdDate);
        ftpUploadFileAs(ctrl, "errors.log", remoteName, "/Data/LOG", serverIP);
    }

    ftpCmd(ctrl, "QUIT", 221);
    ctrl.stop();
    return count;
}

// ── NTP session non-bloquante ─────────────────────────────────────────────────
static EthernetUDP _sessUdp;
static NTPClient   _sessCli(_sessUdp, "pool.ntp.org", 0, 0);

void ntpSessionBegin() { _sessCli.begin(); }

bool ntpSessionPoll() {
    if (!_sessCli.update()) return false;
    unsigned long e = _sessCli.getEpochTime();
    if (e < 1000000000UL) return false;
    struct timeval tv = { (time_t)e, 0 };
    settimeofday(&tv, nullptr);
    rtcSetFromSystem();
    return true;
}

void ntpSessionEnd() { _sessCli.end(); }
