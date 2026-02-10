#ifndef SDLOGGER_H
#define SDLOGGER_H

// ============================================================
// SDLogger.h - SD card initialization and data logging
// ============================================================

#include <SD.h>

static bool _sdReady = false;

// Initialize SD card
bool initSD() {
  // Deselect Ethernet before initializing SD
  digitalWrite(PIN_ETH_CS, HIGH);

  if (!SD.begin(PIN_SD_CS)) {
    Serial.println(F("[SD] Card init FAILED"));
    _sdReady = false;
    return false;
  }

  Serial.println(F("[SD] Card initialized"));
  _sdReady = true;
  return true;
}

bool sdIsReady() {
  return _sdReady;
}

// Generate filename from date: YYYYMMDD.CSV
void sdMakeFilename(char* buf, uint16_t year, uint8_t month, uint8_t day) {
  snprintf(buf, 13, "%04u%02u%02u.CSV", year, month, day);
}

// Generate filename from RTC DateTime
void sdMakeFilename(char* buf, const DateTime& dt) {
  sdMakeFilename(buf, dt.year(), dt.month(), dt.day());
}

// Append a sensor measurement to today's data file
// Format: YYYY-MM-DD HH:MM:SS,sensorIndex,sensorName,rawData
bool logSensorData(const DateTime& now, uint8_t sensorIndex, const char* rawData) {
  if (!_sdReady) return false;

  char filename[13];
  sdMakeFilename(filename, now);

  // Deselect Ethernet
  digitalWrite(PIN_ETH_CS, HIGH);

  File f = SD.open(filename, FILE_WRITE);
  if (!f) {
    Serial.print(F("[SD] Cannot open "));
    Serial.println(filename);
    return false;
  }

  // Write CSV line
  char timestamp[20];
  snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d",
    now.year(), now.month(), now.day(),
    now.hour(), now.minute(), now.second());

  f.print(timestamp);
  f.print(',');
  f.print(sensorIndex + 1);
  f.print(',');
  f.print(config.sensors[sensorIndex].name);
  f.print(',');
  f.println(rawData);

  f.close();

  Serial.print(F("[SD] Logged to "));
  Serial.print(filename);
  Serial.print(F(" : S"));
  Serial.println(sensorIndex + 1);

  return true;
}

// List data files on SD card. Calls callback for each file.
// Callback receives the filename (8.3 format).
// Returns number of data files found.
uint16_t sdListDataFiles(void (*callback)(const char* filename)) {
  if (!_sdReady) return 0;

  digitalWrite(PIN_ETH_CS, HIGH);

  File dir = SD.open("/");
  if (!dir) return 0;

  uint16_t count = 0;
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;

    if (!entry.isDirectory()) {
      const char* name = entry.name();
      // Check if it looks like a data file: 8 digits + .CSV
      size_t len = strlen(name);
      if (len == 12 && name[8] == '.' &&
          (name[9] == 'C' || name[9] == 'c') &&
          (name[10] == 'S' || name[10] == 's') &&
          (name[11] == 'V' || name[11] == 'v')) {
        if (callback) callback(name);
        count++;
      }
    }
    entry.close();
  }
  dir.close();
  return count;
}

#endif
