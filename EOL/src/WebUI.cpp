#include "WebUI.h"
#include <WebServer.h>
#include <Update.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>
#include <string.h>

// ── Ring buffer ───────────────────────────────────────────────────────────────

#define LOG_LINES     80
#define LOG_LINE_LEN  120

static char              _ring[LOG_LINES][LOG_LINE_LEN];
static int               _head  = 0;
static int               _count = 0;
static char              _pending[LOG_LINE_LEN];
static size_t            _pendLen = 0;
static SemaphoreHandle_t _mutex   = nullptr;

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

static inline int lineIdx(int i) {
    return (_count < LOG_LINES) ? i : (_head + i) % LOG_LINES;
}

// ── Public log API ────────────────────────────────────────────────────────────

void webLog(const char* msg) {
    Serial.print(msg);
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    pushChars(msg);
    if (_mutex) xSemaphoreGive(_mutex);
}

void webLogln(const char* msg) {
    Serial.println(msg);
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    pushChars(msg);
    flushLine();
    if (_mutex) xSemaphoreGive(_mutex);
}

void webLogf(const char* fmt, ...) {
    char tmp[LOG_LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    Serial.print(tmp);
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    pushChars(tmp);
    if (_mutex) xSemaphoreGive(_mutex);
}

// ── Web server ────────────────────────────────────────────────────────────────

static WebServer _srv(80);

static void handleRoot() {
    // Copie du buffer sous mutex, construction HTML hors mutex
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    int  snapCount = _count;
    int  snapHead  = _head;
    char snap[LOG_LINES][LOG_LINE_LEN];
    memcpy(snap, _ring, sizeof(_ring));
    if (_mutex) xSemaphoreGive(_mutex);

    auto snapIdx = [&](int i) {
        return (snapCount < LOG_LINES) ? i : (snapHead + i) % LOG_LINES;
    };

    String log;
    log.reserve(snapCount * 60);
    for (int i = snapCount - 1; i >= 0; --i)
        log += snap[snapIdx(i)], log += '\n';

    String html;
    html.reserve(1024 + log.length());
    html  = F("<!DOCTYPE html><html><head>"
              "<meta charset='utf-8'>"
              "<meta http-equiv='refresh' content='10'>"
              "<title>EOL Debug</title>"
              "<style>"
              "body{background:#111;color:#cfc;font-family:monospace;margin:20px}"
              "h1{color:#7f7;border-bottom:1px solid #3a3;padding-bottom:8px;margin-bottom:6px}"
              "nav{margin-bottom:12px}nav a{color:#7af;margin-right:16px}"
              "pre{background:#000;padding:12px;border-radius:6px;"
                  "overflow-x:auto;font-size:13px;white-space:pre-wrap}"
              ".up{color:#fa4}span{color:#888;font-size:11px}"
              "</style></head><body>"
              "<h1>EOL-PLC21 &#8212; Debug</h1>"
              "<nav>"
                "<a href='/'>&#8635; Rafraichir</a>"
                "<a href='/update' class='up'>&#8593; Mise a jour firmware</a>"
              "</nav>"
              "<span>Uptime: ");
    html += millis() / 1000;
    html += F(" s &mdash; ");
    html += _count;
    html += F(" lignes &mdash; auto-refresh 10 s</span>"
              "<pre>");
    html += log;
    html += F("</pre></body></html>");

    _srv.send(200, "text/html", html);
}

static void handleUpdatePage() {
    _srv.send(200, "text/html", F(
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<title>EOL &mdash; Mise a jour</title>"
        "<style>"
        "body{font-family:sans-serif;max-width:520px;margin:60px auto;background:#f4f4f4;color:#222}"
        "h1{color:#333;margin-bottom:4px}"
        "p.sub{color:#666;margin-top:0}"
        "form{background:#fff;padding:24px;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,.12)}"
        "label{display:block;margin-bottom:8px;font-weight:bold}"
        "input[type=file]{width:100%;margin-bottom:16px}"
        "button{background:#0066cc;color:#fff;border:none;padding:10px 28px;"
               "border-radius:4px;cursor:pointer;font-size:15px}"
        "button:hover{background:#0052a3}"
        "a{color:#0066cc}"
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
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP.restart();
}

static void handleUpload() {
    HTTPUpload& u = _srv.upload();
    if (u.status == UPLOAD_FILE_START) {
        webLogf("[OTA/HTTP] Debut: %s\n", u.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN))
            webLogln("[OTA/HTTP] begin() FAILED");
    } else if (u.status == UPLOAD_FILE_WRITE) {
        if (Update.write(u.buf, u.currentSize) != u.currentSize)
            webLogln("[OTA/HTTP] write() FAILED");
    } else if (u.status == UPLOAD_FILE_END) {
        if (Update.end(true))
            webLogf("[OTA/HTTP] Flash OK (%u octets)\n", u.totalSize);
        else
            webLogln("[OTA/HTTP] end() FAILED");
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void webUIBegin() {
    _mutex = xSemaphoreCreateMutex();
    _srv.on("/",       HTTP_GET,  handleRoot);
    _srv.on("/update", HTTP_GET,  handleUpdatePage);
    _srv.on("/update", HTTP_POST, handleUpdatePost, handleUpload);
    _srv.onNotFound([]() {
        _srv.send(404, "text/plain", "Not found");
    });
    _srv.begin();
    webLogln("[WEB] Serveur demarre sur port 80");
}

void webUIHandle() {
    _srv.handleClient();
}
