#ifndef FTP_CLIENT_H
#define FTP_CLIENT_H

// ============================================================
// FTPClient.h - FTP passive-mode client for file upload
// ============================================================

#include <Dns.h>

static EthernetClient _ftpCtrl;
static EthernetClient _ftpData;
static IPAddress      _ftpServerIP;
static bool           _ftpConnected = false;

#define FTP_TIMEOUT  10000
#define FTP_BUF_SIZE 128

// Read FTP response, return the 3-digit status code (or 0 on timeout)
static int ftpReadResponse(char* buf, uint16_t bufSize) {
  unsigned long start = millis();
  uint16_t pos = 0;
  buf[0] = '\0';

  while (millis() - start < FTP_TIMEOUT) {
    while (_ftpCtrl.available() && pos < bufSize - 1) {
      char c = _ftpCtrl.read();
      buf[pos++] = c;
      buf[pos] = '\0';

      // FTP response ends with \r\n after a line starting with 3 digits + space
      if (c == '\n' && pos >= 4) {
        // Check if this is the final response line (not multi-line)
        if (buf[3] == ' ' || (pos > 4 && buf[pos - 2] == '\r')) {
          int code = atoi(buf);
          return code;
        }
      }
    }
    delay(10);
  }
  return 0;
}

// Send FTP command and read response
static int ftpSendCmd(const char* cmd, char* buf, uint16_t bufSize) {
  _ftpCtrl.print(cmd);
  _ftpCtrl.print(F("\r\n"));
  delay(100);
  return ftpReadResponse(buf, bufSize);
}

// Send FTP command with argument
static int ftpSendCmd2(const char* cmd, const char* arg, char* buf, uint16_t bufSize) {
  _ftpCtrl.print(cmd);
  _ftpCtrl.print(' ');
  _ftpCtrl.print(arg);
  _ftpCtrl.print(F("\r\n"));
  delay(100);
  return ftpReadResponse(buf, bufSize);
}

// Parse PASV response to extract data port
// Format: 227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)
static bool ftpParsePASV(const char* resp, uint16_t* port) {
  const char* p = strchr(resp, '(');
  if (!p) return false;
  p++;

  // Skip h1,h2,h3,h4
  for (int i = 0; i < 4; i++) {
    p = strchr(p, ',');
    if (!p) return false;
    p++;
  }

  uint16_t p1 = atoi(p);
  p = strchr(p, ',');
  if (!p) return false;
  p++;
  uint16_t p2 = atoi(p);

  *port = p1 * 256 + p2;
  return true;
}

// Connect to FTP server and authenticate
bool ftpConnect() {
  char buf[FTP_BUF_SIZE];

  Serial.print(F("[FTP] Connecting to "));
  Serial.println(config.ftpServer);

  // Resolve hostname
  DNSClient dns;
  dns.begin(Ethernet.dnsServerIP());
  if (dns.getHostByName(config.ftpServer, _ftpServerIP) != 1) {
    Serial.println(F("[FTP] DNS resolution failed"));
    return false;
  }

  // Connect control channel
  if (!_ftpCtrl.connect(_ftpServerIP, config.ftpPort)) {
    Serial.println(F("[FTP] Connection failed"));
    return false;
  }

  // Read welcome message (220)
  int code = ftpReadResponse(buf, FTP_BUF_SIZE);
  if (code != 220) {
    Serial.print(F("[FTP] Bad welcome: "));
    Serial.println(code);
    _ftpCtrl.stop();
    return false;
  }

  // USER
  code = ftpSendCmd2("USER", config.ftpUser, buf, FTP_BUF_SIZE);
  if (code != 331 && code != 230) {
    Serial.print(F("[FTP] USER failed: "));
    Serial.println(code);
    _ftpCtrl.stop();
    return false;
  }

  // PASS
  if (code == 331) {
    code = ftpSendCmd2("PASS", config.ftpPass, buf, FTP_BUF_SIZE);
    if (code != 230) {
      Serial.print(F("[FTP] PASS failed: "));
      Serial.println(code);
      _ftpCtrl.stop();
      return false;
    }
  }

  // Binary mode
  ftpSendCmd("TYPE I", buf, FTP_BUF_SIZE);

  // CWD to target directory
  if (strlen(config.ftpPath) > 0) {
    code = ftpSendCmd2("CWD", config.ftpPath, buf, FTP_BUF_SIZE);
    if (code != 250) {
      Serial.print(F("[FTP] CWD failed: "));
      Serial.println(code);
    }
  }

  _ftpConnected = true;
  Serial.println(F("[FTP] Connected and authenticated"));
  return true;
}

void ftpDisconnect() {
  if (_ftpConnected) {
    char buf[FTP_BUF_SIZE];
    ftpSendCmd("QUIT", buf, FTP_BUF_SIZE);
    _ftpCtrl.stop();
    _ftpConnected = false;
    Serial.println(F("[FTP] Disconnected"));
  }
}

// Upload a single file from SD to FTP server
bool ftpUploadFile(const char* filename) {
  if (!_ftpConnected) return false;

  char buf[FTP_BUF_SIZE];

  // Enter passive mode
  int code = ftpSendCmd("PASV", buf, FTP_BUF_SIZE);
  if (code != 227) {
    Serial.print(F("[FTP] PASV failed: "));
    Serial.println(code);
    return false;
  }

  uint16_t dataPort;
  if (!ftpParsePASV(buf, &dataPort)) {
    Serial.println(F("[FTP] PASV parse failed"));
    return false;
  }

  // Connect data channel
  if (!_ftpData.connect(_ftpServerIP, dataPort)) {
    Serial.println(F("[FTP] Data connection failed"));
    return false;
  }

  // Send STOR command
  code = ftpSendCmd2("STOR", filename, buf, FTP_BUF_SIZE);
  if (code != 150 && code != 125) {
    Serial.print(F("[FTP] STOR failed: "));
    Serial.println(code);
    _ftpData.stop();
    return false;
  }

  // Open local file and send content
  digitalWrite(PIN_ETH_CS, HIGH);  // Deselect Ethernet for SD access
  File f = SD.open(filename, FILE_READ);
  if (!f) {
    Serial.print(F("[FTP] Cannot open local file: "));
    Serial.println(filename);
    _ftpData.stop();
    return false;
  }

  uint32_t totalBytes = 0;
  byte sdBuf[128];
  while (f.available()) {
    int n = f.read(sdBuf, sizeof(sdBuf));
    if (n > 0) {
      _ftpData.write(sdBuf, n);
      totalBytes += n;
    }
  }
  f.close();

  // Close data connection (signals end of transfer)
  _ftpData.stop();

  // Read transfer complete (226)
  code = ftpReadResponse(buf, FTP_BUF_SIZE);
  if (code != 226) {
    Serial.print(F("[FTP] Transfer response: "));
    Serial.println(code);
    // Not necessarily an error; some servers send 250
  }

  Serial.print(F("[FTP] Uploaded "));
  Serial.print(filename);
  Serial.print(F(" ("));
  Serial.print(totalBytes);
  Serial.println(F(" bytes)"));

  return true;
}

// Get remote file listing using NLST and save to temp file on SD
// Returns number of files listed, or -1 on error
static int ftpListRemoteToSD(const char* tempFile) {
  if (!_ftpConnected) return -1;

  char buf[FTP_BUF_SIZE];

  // PASV
  int code = ftpSendCmd("PASV", buf, FTP_BUF_SIZE);
  if (code != 227) return -1;

  uint16_t dataPort;
  if (!ftpParsePASV(buf, &dataPort)) return -1;

  if (!_ftpData.connect(_ftpServerIP, dataPort)) return -1;

  // NLST
  code = ftpSendCmd("NLST", buf, FTP_BUF_SIZE);
  if (code != 150 && code != 125) {
    _ftpData.stop();
    // 550 = empty directory - not an error for us
    if (code == 550) return 0;
    return -1;
  }

  // Read listing and save to temp file on SD
  digitalWrite(PIN_ETH_CS, HIGH);
  SD.remove(tempFile);
  File f = SD.open(tempFile, FILE_WRITE);
  if (!f) {
    _ftpData.stop();
    return -1;
  }

  int count = 0;
  unsigned long start = millis();
  while (millis() - start < FTP_TIMEOUT) {
    while (_ftpData.available()) {
      char c = _ftpData.read();
      f.write(c);
      if (c == '\n') count++;
      start = millis();  // Reset timeout on data
    }
    if (!_ftpData.connected()) break;
    delay(10);
  }
  f.close();
  _ftpData.stop();

  // Read final response
  ftpReadResponse(buf, FTP_BUF_SIZE);

  Serial.print(F("[FTP] Remote listing: "));
  Serial.print(count);
  Serial.println(F(" files"));

  return count;
}

// Check if a filename exists in a text file (one name per line)
static bool fileExistsInList(const char* listFile, const char* filename) {
  digitalWrite(PIN_ETH_CS, HIGH);
  File f = SD.open(listFile, FILE_READ);
  if (!f) return false;

  char line[16];
  while (f.available()) {
    uint8_t len = 0;
    while (f.available() && len < sizeof(line) - 1) {
      char c = f.read();
      if (c == '\n') break;
      if (c != '\r') line[len++] = c;
    }
    line[len] = '\0';
    if (strcmp(line, filename) == 0) {
      f.close();
      return true;
    }
  }
  f.close();
  return false;
}

// Synchronize: upload local files that are absent from FTP server.
// Skips today's file (still being written).
// Returns number of files uploaded.
int ftpSyncFiles(RTC_DS3231& rtc) {
  Serial.println(F("[FTP] Starting file sync..."));

  if (!ftpConnect()) return -1;

  // Get remote listing
  const char* tempFile = "_remote.tmp";
  int remoteCount = ftpListRemoteToSD(tempFile);
  if (remoteCount < 0) {
    Serial.println(F("[FTP] Could not get remote listing"));
    // Fall back: upload everything not in uploaded.txt
  }

  // Determine today's filename to skip it
  DateTime now = rtc.now();
  char todayFile[13];
  sdMakeFilename(todayFile, now);

  // Collect local files to upload
  int uploaded = 0;

  digitalWrite(PIN_ETH_CS, HIGH);
  File dir = SD.open("/");
  if (!dir) {
    ftpDisconnect();
    return -1;
  }

  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;

    if (!entry.isDirectory()) {
      const char* name = entry.name();
      size_t len = strlen(name);

      // Check if it's a data file
      if (len == 12 && name[8] == '.' &&
          (name[9] == 'C' || name[9] == 'c') &&
          (name[10] == 'S' || name[10] == 's') &&
          (name[11] == 'V' || name[11] == 'v')) {

        // Skip today's file (still being written)
        bool isToday = (strcmp(name, todayFile) == 0);

        // Check if already on remote server
        bool onRemote = false;
        if (remoteCount >= 0) {
          onRemote = fileExistsInList(tempFile, name);
        }

        if (!onRemote) {
          Serial.print(F("[FTP] Uploading "));
          Serial.println(name);

          // Need a mutable copy of filename
          char nameCopy[13];
          strncpy(nameCopy, name, sizeof(nameCopy));

          if (ftpUploadFile(nameCopy)) {
            uploaded++;
          }
        } else {
          Serial.print(F("[FTP] Already on server: "));
          Serial.println(name);
        }
      }
    }
    entry.close();
  }
  dir.close();

  // Cleanup temp file
  SD.remove(tempFile);

  ftpDisconnect();

  Serial.print(F("[FTP] Sync complete: "));
  Serial.print(uploaded);
  Serial.println(F(" files uploaded"));

  return uploaded;
}

#endif
