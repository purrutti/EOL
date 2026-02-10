#ifndef WEBUI_H
#define WEBUI_H

// ============================================================
// WebUI.h - Web server with HTML configuration interface
// ============================================================

// Shared between request parsing
#define HTTP_BUF_SIZE 128

static EthernetServer* _webServer = nullptr;

void initWebServer(EthernetServer* srv) {
  _webServer = srv;
  srv->begin();
  Serial.print(F("[WEB] Server started on "));
  Serial.print(config.ip[0]); Serial.print('.');
  Serial.print(config.ip[1]); Serial.print('.');
  Serial.print(config.ip[2]); Serial.print('.');
  Serial.println(config.ip[3]);
}

// --- HTML helpers ---

static void sendHtmlHeader(EthernetClient& c, const char* title) {
  c.println(F("HTTP/1.1 200 OK"));
  c.println(F("Content-Type: text/html; charset=UTF-8"));
  c.println(F("Connection: close"));
  c.println();
  c.print(F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"));
  c.print(F("<meta name='viewport' content='width=device-width,initial-scale=1'>"));
  c.print(F("<title>"));
  c.print(title);
  c.print(F("</title><style>"));
  c.print(F("body{font-family:monospace;max-width:800px;margin:0 auto;padding:10px;background:#f4f4f4;}"));
  c.print(F("h1{background:#2c3e50;color:#fff;padding:10px;margin:0 -10px 10px;}"));
  c.print(F("fieldset{background:#fff;margin:8px 0;padding:8px;border:1px solid #ccc;}"));
  c.print(F("legend{font-weight:bold;color:#2c3e50;}"));
  c.print(F("label{display:inline-block;width:140px;font-size:13px;}"));
  c.print(F("input[type=text],input[type=number]{width:180px;padding:2px 4px;margin:2px 0;font-family:monospace;}"));
  c.print(F(".btn{background:#27ae60;color:#fff;padding:8px 20px;border:none;cursor:pointer;font-size:14px;margin:8px 4px;}"));
  c.print(F(".btn:hover{background:#219a52;}.msg{background:#d4edda;padding:10px;border:1px solid #c3e6cb;margin:10px 0;}"));
  c.print(F(".chk{width:auto;}"));
  c.println(F("</style></head><body>"));
}

static void sendHtmlFooter(EthernetClient& c) {
  c.println(F("</body></html>"));
}

// Helper to print IP address fields
static void sendIpField(EthernetClient& c, const char* label, const char* name, const uint8_t ip[4]) {
  char val[16];
  snprintf(val, sizeof(val), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  c.print(F("<label>"));
  c.print(label);
  c.print(F("</label><input type='text' name='"));
  c.print(name);
  c.print(F("' value='"));
  c.print(val);
  c.println(F("'><br>"));
}

static void sendTextField(EthernetClient& c, const char* label, const char* name, const char* value) {
  c.print(F("<label>"));
  c.print(label);
  c.print(F("</label><input type='text' name='"));
  c.print(name);
  c.print(F("' value='"));
  c.print(value);
  c.println(F("'><br>"));
}

static void sendNumberField(EthernetClient& c, const char* label, const char* name, unsigned long value) {
  c.print(F("<label>"));
  c.print(label);
  c.print(F("</label><input type='number' name='"));
  c.print(name);
  c.print(F("' value='"));
  c.print(value);
  c.println(F("'><br>"));
}

// --- Serve the configuration page ---
static void serveConfigPage(EthernetClient& c, bool saved) {
  sendHtmlHeader(c, "EOL Configuration");
  c.println(F("<h1>EOL Configuration</h1>"));

  if (saved) {
    c.println(F("<div class='msg'>Configuration saved.</div>"));
  }

  c.println(F("<form method='POST' action='/save'>"));

  // -- Network --
  c.println(F("<fieldset><legend>Network</legend>"));
  sendIpField(c, "IP Address:", "ip", config.ip);
  sendIpField(c, "Gateway:", "gw", config.gateway);
  sendIpField(c, "Subnet:", "sn", config.subnet);
  sendIpField(c, "DNS:", "dns", config.dns);
  // MAC
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
    config.mac[0], config.mac[1], config.mac[2],
    config.mac[3], config.mac[4], config.mac[5]);
  sendTextField(c, "MAC:", "mac", macStr);
  c.println(F("</fieldset>"));

  // -- FTP --
  c.println(F("<fieldset><legend>FTP Server</legend>"));
  sendTextField(c, "Server:", "ftps", config.ftpServer);
  sendNumberField(c, "Port:", "ftpp", config.ftpPort);
  sendTextField(c, "User:", "ftpu", config.ftpUser);
  sendTextField(c, "Password:", "ftpw", config.ftpPass);
  sendTextField(c, "Path:", "ftpd", config.ftpPath);
  sendNumberField(c, "Upload hour:", "uph", config.uploadHour);
  sendNumberField(c, "Upload minute:", "upm", config.uploadMinute);
  c.println(F("</fieldset>"));

  // -- NTP --
  c.println(F("<fieldset><legend>Time (NTP)</legend>"));
  sendTextField(c, "NTP Server:", "ntps", config.ntpServer);
  sendNumberField(c, "GMT Offset (h):", "gmto", (unsigned long)(config.gmtOffset + 128));
  c.println(F("</fieldset>"));

  // -- Modem --
  c.println(F("<fieldset><legend>Modem (TRB500)</legend>"));
  sendNumberField(c, "Boot time (ms):", "mbt", config.modemBootTime);
  sendNumberField(c, "On duration (ms):", "mod", config.gsmOnDuration);
  c.println(F("</fieldset>"));

  // -- Sensors --
  for (uint8_t i = 0; i < NUM_SENSORS; i++) {
    SensorConfig& sc = config.sensors[i];
    char legend[20];
    snprintf(legend, sizeof(legend), "Sensor %d", i + 1);
    c.print(F("<fieldset><legend>"));
    c.print(legend);
    c.println(F("</legend>"));

    // Prefix for field names: s0, s1, s2, s3
    char pre[3];
    snprintf(pre, sizeof(pre), "s%d", i);

    // Enabled checkbox
    c.print(F("<label>Enabled:</label><input type='checkbox' class='chk' name='"));
    c.print(pre); c.print(F("en"));
    c.print(F("' value='1'"));
    if (sc.enabled) c.print(F(" checked"));
    c.println(F("><br>"));

    // Fields
    char fname[8];
    snprintf(fname, sizeof(fname), "%sn", pre);
    sendTextField(c, "Name:", fname, sc.name);

    snprintf(fname, sizeof(fname), "%sc", pre);
    sendTextField(c, "Command:", fname, sc.command);

    snprintf(fname, sizeof(fname), "%se", pre);
    sendTextField(c, "End marker:", fname, sc.endMarker);

    snprintf(fname, sizeof(fname), "%sb", pre);
    sendNumberField(c, "Baud rate:", fname, sc.baud);

    snprintf(fname, sizeof(fname), "%sw", pre);
    sendNumberField(c, "Warmup (ms):", fname, sc.warmup);

    snprintf(fname, sizeof(fname), "%st", pre);
    sendNumberField(c, "Timeout (ms):", fname, sc.timeout);

    snprintf(fname, sizeof(fname), "%si", pre);
    sendNumberField(c, "Interval (ms):", fname, sc.interval);

    c.println(F("</fieldset>"));
  }

  c.println(F("<input type='submit' class='btn' value='Save Configuration'>"));
  c.println(F("</form>"));

  // Status info
  c.println(F("<fieldset><legend>System Status</legend>"));
  c.print(F("<p>Modem: "));
  c.print(modemIsOn() ? F("ON") : F("OFF"));
  c.print(F(" | SD: "));
  c.print(sdIsReady() ? F("OK") : F("FAIL"));
  c.println(F("</p></fieldset>"));

  sendHtmlFooter(c);
}

// --- Parse helpers ---

// Parse an IP address string "x.x.x.x" into 4 bytes
static bool parseIP(const char* str, uint8_t* ip) {
  unsigned int a, b, c, d;
  if (sscanf(str, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
    ip[0] = a; ip[1] = b; ip[2] = c; ip[3] = d;
    return true;
  }
  return false;
}

// Parse MAC address string "XX:XX:XX:XX:XX:XX"
static bool parseMAC(const char* str, byte* mac) {
  unsigned int m[6];
  if (sscanf(str, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
    for (int i = 0; i < 6; i++) mac[i] = m[i];
    return true;
  }
  return false;
}

// URL-decode a value in-place (handles %XX and +)
static void urlDecode(char* str) {
  char* dst = str;
  while (*str) {
    if (*str == '+') {
      *dst++ = ' ';
      str++;
    } else if (*str == '%' && str[1] && str[2]) {
      char hex[3] = { str[1], str[2], 0 };
      *dst++ = (char)strtol(hex, NULL, 16);
      str += 3;
    } else {
      *dst++ = *str++;
    }
  }
  *dst = '\0';
}

// --- Handle POST /save ---
// Reads POST body from client and applies key=value pairs to config
static void handleSaveConfig(EthernetClient& c, unsigned long contentLength) {
  // Reset checkbox fields (unchecked checkboxes are not sent)
  for (uint8_t i = 0; i < NUM_SENSORS; i++) {
    config.sensors[i].enabled = false;
  }

  char key[16];
  char val[64];
  uint8_t kpos = 0, vpos = 0;
  bool inValue = false;
  unsigned long bytesRead = 0;

  while (bytesRead < contentLength) {
    // Wait for data
    unsigned long start = millis();
    while (!c.available() && millis() - start < 5000) delay(1);
    if (!c.available()) break;

    char ch = c.read();
    bytesRead++;

    if (ch == '=') {
      key[kpos] = '\0';
      inValue = true;
      vpos = 0;
    } else if (ch == '&' || bytesRead >= contentLength) {
      if (ch != '&') {
        if (inValue && vpos < sizeof(val) - 1) val[vpos++] = ch;
        else if (!inValue && kpos < sizeof(key) - 1) key[kpos++] = ch;
      }
      val[vpos] = '\0';
      if (!inValue) key[kpos] = '\0';

      urlDecode(val);

      // Apply key-value pair to config
      // Network
      if      (strcmp(key, "ip") == 0)   parseIP(val, config.ip);
      else if (strcmp(key, "gw") == 0)   parseIP(val, config.gateway);
      else if (strcmp(key, "sn") == 0)   parseIP(val, config.subnet);
      else if (strcmp(key, "dns") == 0)  parseIP(val, config.dns);
      else if (strcmp(key, "mac") == 0)  parseMAC(val, config.mac);
      // FTP
      else if (strcmp(key, "ftps") == 0) strncpy(config.ftpServer, val, sizeof(config.ftpServer));
      else if (strcmp(key, "ftpp") == 0) config.ftpPort = atoi(val);
      else if (strcmp(key, "ftpu") == 0) strncpy(config.ftpUser, val, sizeof(config.ftpUser));
      else if (strcmp(key, "ftpw") == 0) strncpy(config.ftpPass, val, sizeof(config.ftpPass));
      else if (strcmp(key, "ftpd") == 0) strncpy(config.ftpPath, val, sizeof(config.ftpPath));
      else if (strcmp(key, "uph") == 0)  config.uploadHour = atoi(val);
      else if (strcmp(key, "upm") == 0)  config.uploadMinute = atoi(val);
      // NTP
      else if (strcmp(key, "ntps") == 0) strncpy(config.ntpServer, val, sizeof(config.ntpServer));
      else if (strcmp(key, "gmto") == 0) config.gmtOffset = atoi(val) - 128;
      // Modem
      else if (strcmp(key, "mbt") == 0)  config.modemBootTime = atol(val);
      else if (strcmp(key, "mod") == 0)  config.gsmOnDuration = atol(val);
      // Sensors (s0en, s0n, s0c, s0e, s0b, s0w, s0t, s0i, etc.)
      else if (key[0] == 's' && key[1] >= '0' && key[1] <= '3') {
        uint8_t idx = key[1] - '0';
        const char* field = key + 2;
        SensorConfig& sc = config.sensors[idx];

        if      (strcmp(field, "en") == 0) sc.enabled = (atoi(val) == 1);
        else if (strcmp(field, "n") == 0)  strncpy(sc.name, val, sizeof(sc.name));
        else if (strcmp(field, "c") == 0)  strncpy(sc.command, val, sizeof(sc.command));
        else if (strcmp(field, "e") == 0)  strncpy(sc.endMarker, val, sizeof(sc.endMarker));
        else if (strcmp(field, "b") == 0)  sc.baud = atol(val);
        else if (strcmp(field, "w") == 0)  sc.warmup = atol(val);
        else if (strcmp(field, "t") == 0)  sc.timeout = atol(val);
        else if (strcmp(field, "i") == 0)  sc.interval = atol(val);
      }

      // Reset for next pair
      kpos = 0;
      vpos = 0;
      inValue = false;
    } else {
      if (inValue) {
        if (vpos < sizeof(val) - 1) val[vpos++] = ch;
      } else {
        if (kpos < sizeof(key) - 1) key[kpos++] = ch;
      }
    }
  }

  saveConfig();
  Serial.println(F("[WEB] Configuration saved via web UI"));
}

// --- Main web request handler (call from loop) ---
void handleWebClient() {
  if (!_webServer) return;

  EthernetClient client = _webServer->available();
  if (!client) return;

  // Reset modem timer on any web activity
  modemResetTimer();

  // Read request line
  char reqLine[80];
  uint8_t reqPos = 0;
  unsigned long start = millis();
  while (client.connected() && millis() - start < 5000) {
    if (client.available()) {
      char c = client.read();
      if (c == '\n') break;
      if (c != '\r' && reqPos < sizeof(reqLine) - 1) {
        reqLine[reqPos++] = c;
      }
    }
  }
  reqLine[reqPos] = '\0';

  // Determine method and path
  bool isPost = (strncmp(reqLine, "POST", 4) == 0);
  bool isSave = (strstr(reqLine, "/save") != NULL);

  // Read headers to find Content-Length
  unsigned long contentLength = 0;
  char headerLine[80];
  while (client.connected()) {
    uint8_t hpos = 0;
    while (client.available() && hpos < sizeof(headerLine) - 1) {
      char c = client.read();
      if (c == '\n') break;
      if (c != '\r') headerLine[hpos++] = c;
    }
    headerLine[hpos] = '\0';

    if (hpos == 0) break;  // Empty line = end of headers

    if (strncasecmp(headerLine, "Content-Length:", 15) == 0) {
      contentLength = atol(headerLine + 15);
    }
  }

  // Route request
  if (isPost && isSave && contentLength > 0) {
    handleSaveConfig(client, contentLength);
    serveConfigPage(client, true);
  } else {
    serveConfigPage(client, false);
  }

  delay(1);
  client.stop();
}

#endif
