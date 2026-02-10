#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// Config.h - Configuration structures and EEPROM management
// ============================================================

#include <EEPROM.h>

#define CONFIG_MAGIC   0xE01B
#define CONFIG_VERSION 1
#define NUM_SENSORS    4
#define EEPROM_ADDR    0

// --- Per-sensor configuration ---
struct __attribute__((packed)) SensorConfig {
  bool     enabled;          // Sensor active
  char     name[16];         // Display name
  char     command[64];      // RS232 command to request measurement
  char     endMarker[8];     // Response end marker (e.g. "\r\n")
  uint32_t baud;             // Serial baud rate for this sensor
  uint32_t warmup;           // Warmup time after power on (ms)
  uint32_t timeout;          // Response timeout (ms)
  uint32_t interval;         // Time between measurements (ms)
};

// --- System-wide configuration ---
struct __attribute__((packed)) SystemConfig {
  uint16_t magic;
  uint8_t  version;

  // Network
  byte     mac[6];
  uint8_t  ip[4];
  uint8_t  gateway[4];
  uint8_t  subnet[4];
  uint8_t  dns[4];

  // FTP
  char     ftpServer[64];
  uint16_t ftpPort;
  char     ftpUser[32];
  char     ftpPass[32];
  char     ftpPath[64];

  // Daily upload time
  uint8_t  uploadHour;
  uint8_t  uploadMinute;

  // NTP
  char     ntpServer[64];
  int8_t   gmtOffset;        // GMT offset in hours

  // Modem
  uint32_t modemBootTime;    // Time to wait for TRB500 boot (ms)
  uint32_t gsmOnDuration;    // Auto-off timer after startup (ms)

  // Sensors
  SensorConfig sensors[NUM_SENSORS];

  // Integrity
  uint16_t checksum;
};

// Global config instance (defined in EOL.ino)
extern SystemConfig config;

// --- Checksum calculation ---
static uint16_t configChecksum(const SystemConfig* cfg) {
  uint16_t sum = 0;
  const uint8_t* p = (const uint8_t*)cfg;
  // Sum all bytes except the checksum field itself (last 2 bytes)
  for (size_t i = 0; i < sizeof(SystemConfig) - sizeof(uint16_t); i++) {
    sum += p[i];
  }
  return sum;
}

// --- Load default configuration ---
static void loadDefaults() {
  memset(&config, 0, sizeof(config));
  config.magic   = CONFIG_MAGIC;
  config.version = CONFIG_VERSION;

  // Default MAC address
  config.mac[0] = 0xDE; config.mac[1] = 0xAD;
  config.mac[2] = 0xBE; config.mac[3] = 0xEF;
  config.mac[4] = 0x01; config.mac[5] = 0x01;

  // Default network: Arduino on TRB500 LAN
  config.ip[0] = 192;  config.ip[1] = 168;  config.ip[2] = 2;   config.ip[3] = 100;
  config.gateway[0] = 192; config.gateway[1] = 168; config.gateway[2] = 2; config.gateway[3] = 1;
  config.subnet[0] = 255;  config.subnet[1] = 255;  config.subnet[2] = 255; config.subnet[3] = 0;
  config.dns[0] = 192; config.dns[1] = 168; config.dns[2] = 2; config.dns[3] = 1;

  // FTP defaults
  strncpy(config.ftpServer, "oceane.obs-vlfr.fr", sizeof(config.ftpServer));
  config.ftpPort = 21;
  strncpy(config.ftpUser, "leo", sizeof(config.ftpUser));
  strncpy(config.ftpPass, "leocnrs", sizeof(config.ftpPass));
  strncpy(config.ftpPath, "/1236", sizeof(config.ftpPath));

  // Upload at 02:00 by default
  config.uploadHour   = 2;
  config.uploadMinute = 0;

  // NTP
  strncpy(config.ntpServer, "pool.ntp.org", sizeof(config.ntpServer));
  config.gmtOffset = 1;  // CET

  // Modem timing
  config.modemBootTime = 30000;  // 1 minutes for TRB500 boot
  config.gsmOnDuration = 600000;  // 10 minutes

  // Default sensor config
  for (uint8_t i = 0; i < NUM_SENSORS; i++) {
    config.sensors[i].enabled  = false;
    snprintf(config.sensors[i].name, sizeof(config.sensors[i].name), "Sensor %d", i + 1);
    config.sensors[i].command[0]   = '\0';
    strncpy(config.sensors[i].endMarker, "\r\n", sizeof(config.sensors[i].endMarker));
    config.sensors[i].baud     = 19200;
    config.sensors[i].warmup   = 5000;   // 5 s warmup
    config.sensors[i].timeout  = 10000;  // 10 s response timeout
    config.sensors[i].interval = 600000; // 10 min between readings
  }

  config.checksum = configChecksum(&config);
}

// --- Save configuration to EEPROM ---
static void saveConfig() {
  config.magic    = CONFIG_MAGIC;
  config.version  = CONFIG_VERSION;
  config.checksum = configChecksum(&config);
  EEPROM.put(EEPROM_ADDR, config);
  Serial.println(F("[CFG] Config saved to EEPROM"));
}

// --- Load configuration from EEPROM ---
static void loadConfig() {
  EEPROM.get(EEPROM_ADDR, config);

  if (config.magic != CONFIG_MAGIC || config.version != CONFIG_VERSION) {
    Serial.println(F("[CFG] No valid config found, loading defaults"));
    loadDefaults();
    saveConfig();
    return;
  }

  uint16_t cs = configChecksum(&config);
  if (cs != config.checksum) {
    Serial.println(F("[CFG] Checksum mismatch, loading defaults"));
    loadDefaults();
    saveConfig();
    return;
  }

  Serial.println(F("[CFG] Config loaded from EEPROM"));
}

#endif
