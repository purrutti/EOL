#include "WebUI.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <SD.h>
#include <stdarg.h>
#include <string.h>

// ── Ring buffer ───────────────────────────────────────────────────────────────

#define LOG_LINES    80
#define LOG_LINE_LEN 120

static char   _ring[LOG_LINES][LOG_LINE_LEN];
static int    _head    = 0;
static int    _count   = 0;
static char   _pending[LOG_LINE_LEN];
static size_t _pendLen = 0;

static void flushLine() {
    if (_pendLen == 0) return;
    _pending[_pendLen] = '\0';
    strlcpy(_ring[_head], _pending, LOG_LINE_LEN);
    _head = (_head + 1) % LOG_LINES;
    if (_count < LOG_LINES) _count++;
    _pendLen = 0;
}

static void pushChars(const char* s) {
    for (; *s; ++s) {
        if (*s == '\n' || *s == '\r') {
            flushLine();
        } else if (_pendLen < LOG_LINE_LEN - 1) {
            _pending[_pendLen++] = *s;
        }
    }
}

// ── Public log API ────────────────────────────────────────────────────────────

void webLog(const char* msg) {
    Serial.print(msg);
    pushChars(msg);
}

void webLogln(const char* msg) {
    Serial.println(msg);
    pushChars(msg);
    flushLine();
}

void webLogf(const char* fmt, ...) {
    char tmp[LOG_LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    Serial.print(tmp);
    pushChars(tmp);
}

// ── Web server ────────────────────────────────────────────────────────────────

static WebServer _srv(80);

static void handleRoot() {
    String log;
    log.reserve(_count * 60);
    for (int i = _count - 1; i >= 0; --i) {
        int idx = (_count < LOG_LINES) ? i : (_head + i) % LOG_LINES;
        log += _ring[idx];
        log += '\n';
    }

    String html;
    html.reserve(1024 + log.length());
    html  = F("<!DOCTYPE html><html><head>"
              "<meta charset='utf-8'>"
              "<meta http-equiv='refresh' content='1'>"
              "<title>EOL Debug</title>"
              "<style>"
              "body{background:#111;color:#cfc;font-family:monospace;margin:20px}"
              "h1{color:#7f7;border-bottom:1px solid #3a3;padding-bottom:8px}"
              "nav{margin-bottom:12px}nav a{color:#7af;margin-right:16px}"
              "pre{background:#000;padding:12px;border-radius:6px;"
                  "overflow-x:auto;font-size:13px;white-space:pre-wrap}"
              ".up{color:#fa4}span{color:#888;font-size:11px}"
              "</style></head><body>"
              "<h1>EOL-PLC21</h1>"
              "<nav>"
                "<a href='/'>&#8635; Rafraichir</a>"
                "<a href='/files'>&#128190; Fichiers SD</a>"
                "<a href='/update' class='up'>&#8593; Mise a jour firmware</a>"
              "</nav>"
              "<span>Uptime: ");
    html += millis() / 1000;
    html += F(" s &mdash; ");
    html += _count;
    html += F(" lignes &mdash; auto-refresh 1 s</span><pre>");
    html += log;
    html += F("</pre></body></html>");

    _srv.send(200, "text/html", html);
}

static void handleFiles() {
    String html;
    html.reserve(2048);
    html = F("<!DOCTYPE html><html><head>"
             "<meta charset='utf-8'>"
             "<title>EOL &mdash; Fichiers SD</title>"
             "<style>"
             "body{font-family:sans-serif;max-width:720px;margin:40px auto;"
                  "background:#f4f4f4;color:#222}"
             "h1{color:#333}"
             "nav{margin-bottom:16px}nav a{color:#0066cc;margin-right:16px}"
             "table{width:100%;border-collapse:collapse;background:#fff;"
                   "border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,.1)}"
             "th{background:#0066cc;color:#fff;padding:10px 14px;text-align:left}"
             "td{padding:8px 14px;border-bottom:1px solid #eee}"
             "tr:last-child td{border-bottom:none}"
             "tr:hover td{background:#f0f6ff}"
             "a{color:#0066cc;text-decoration:none}"
             "</style></head><body>"
             "<h1>&#128190; Fichiers SD &mdash; /data</h1>"
             "<nav>"
               "<a href='/'>&#8592; Debug</a>"
               "<a href='/update'>&#8593; Firmware</a>"
             "</nav>");

    File dir = SD.open("/data");
    if (!dir) {
        html += F("<p style='color:red'>Carte SD indisponible ou /data absent.</p>");
    } else {
        html += F("<table>"
                  "<tr><th>Fichier</th><th>Taille</th><th></th></tr>");
        bool any = false;
        File f = dir.openNextFile();
        while (f) {
            if (!f.isDirectory()) {
                any = true;
                const char* full = f.name();
                const char* slash = strrchr(full, '/');
                const char* name  = slash ? slash + 1 : full;
                uint32_t sz = f.size();

                html += F("<tr><td>");
                html += name;
                html += F("</td><td>");
                if (sz >= 1024) { html += sz / 1024; html += F(" Ko"); }
                else            { html += sz;        html += F(" o"); }
                html += F("</td><td>"
                          "<a href='/download?f=");
                html += name;
                html += F("'>&#11015; Telecharger</a></td></tr>");
            }
            f.close();
            f = dir.openNextFile();
        }
        dir.close();
        if (!any) {
            html += F("<tr><td colspan='3' style='color:#888;text-align:center'>"
                      "Aucun fichier dans /data</td></tr>");
        }
        html += F("</table>");
    }
    html += F("</body></html>");
    _srv.send(200, "text/html", html);
}

static void handleDownload() {
    if (!_srv.hasArg("f")) {
        _srv.send(400, "text/plain", "Parametre f manquant");
        return;
    }
    String fname = _srv.arg("f");
    // Rejeter toute tentative de path traversal
    if (fname.indexOf("..") >= 0 || fname.indexOf('/') >= 0 ||
        fname.indexOf('\\') >= 0) {
        _srv.send(400, "text/plain", "Nom de fichier invalide");
        return;
    }
    String path = "/data/" + fname;
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f || f.isDirectory()) {
        if (f) f.close();
        _srv.send(404, "text/plain", "Fichier introuvable");
        return;
    }
    _srv.sendHeader("Content-Disposition",
                    "attachment; filename=\"" + fname + "\"");
    _srv.streamFile(f, "text/csv");
    f.close();
}

static void handleUpdatePage() {
    _srv.send(200, "text/html", F(
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<title>EOL &mdash; Mise a jour</title>"
        "<style>"
        "body{font-family:sans-serif;max-width:520px;margin:60px auto;"
             "background:#f4f4f4;color:#222}"
        "h1{color:#333}p.sub{color:#666;margin-top:0}"
        "form{background:#fff;padding:24px;border-radius:8px;"
             "box-shadow:0 2px 8px rgba(0,0,0,.12)}"
        "label{display:block;margin-bottom:8px;font-weight:bold}"
        "input[type=file]{width:100%;margin-bottom:16px}"
        "button{background:#0066cc;color:#fff;border:none;padding:10px 28px;"
               "border-radius:4px;cursor:pointer;font-size:15px}"
        "button:hover{background:#0052a3}a{color:#0066cc}"
        "</style></head><body>"
        "<h1>&#8593; Mise a jour firmware</h1>"
        "<p class='sub'>Fichier .bin genere par Arduino IDE / Visual Micro</p>"
        "<form method='POST' action='/update' enctype='multipart/form-data'>"
          "<label>Firmware (.bin) :</label>"
          "<input type='file' name='firmware' accept='.bin' required>"
          "<button type='submit'>Telecharger et flasher</button>"
        "</form>"
        "<p><a href='/'>&#8592; Retour debug</a></p>"
        "</body></html>"
    ));
}

static void handleUpdatePost() {
    bool ok = !Update.hasError();
    _srv.sendHeader("Connection", "close");
    if (ok) {
        _srv.send(200, "text/html", F(
            "<html><body style='font-family:sans-serif;text-align:center;padding:60px'>"
            "<h2 style='color:green'>&#10003; Flash OK &mdash; Redemarrage...</h2>"
            "<script>setTimeout(()=>location.href='/',9000)</script>"
            "</body></html>"
        ));
    } else {
        _srv.send(200, "text/html", F(
            "<html><body style='font-family:sans-serif;text-align:center;padding:60px'>"
            "<h2 style='color:red'>&#10007; Erreur flash</h2>"
            "<p><a href='/update'>Reessayer</a></p>"
            "</body></html>"
        ));
    }
    delay(500);
    ESP.restart();
}

static void handleUpload() {
    HTTPUpload& u = _srv.upload();
    if (u.status == UPLOAD_FILE_START) {
        webLogf("[OTA] Debut: %s\n", u.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN))
            webLogln("[OTA] begin() FAILED");
    } else if (u.status == UPLOAD_FILE_WRITE) {
        if (Update.write(u.buf, u.currentSize) != u.currentSize)
            webLogln("[OTA] write() FAILED");
    } else if (u.status == UPLOAD_FILE_END) {
        if (Update.end(true))
            webLogf("[OTA] Flash OK (%u octets)\n", u.totalSize);
        else
            webLogln("[OTA] end() FAILED");
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void webUIBegin(const char* ssid, const char* pass) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, pass);
    delay(100);  // AP needs a moment to settle

    IPAddress ip = WiFi.softAPIP();
    char ipStr[16];
    snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    webLogf("[WiFi] AP '%s' sur http://%s\n", ssid, ipStr);

    _srv.on("/",         HTTP_GET,  handleRoot);
    _srv.on("/files",    HTTP_GET,  handleFiles);
    _srv.on("/download", HTTP_GET,  handleDownload);
    _srv.on("/update",   HTTP_GET,  handleUpdatePage);
    _srv.on("/update",   HTTP_POST, handleUpdatePost, handleUpload);
    _srv.onNotFound([]() { _srv.send(404, "text/plain", "Not found"); });
    _srv.begin();
    webLogln("[Web] Serveur demarre port 80");
}

void webUIHandle() {
    _srv.handleClient();
}
