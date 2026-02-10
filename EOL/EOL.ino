/*
  EOL.ino - Environmental monitoring station
  Hardware: Industrial Shields M-Duino 42+
  Modem:    Teltonika TRB500 (192.168.2.1)

  4 RS232 sensors multiplexed via relays on a shared serial port.
  Data logged to SD card, uploaded daily to FTP server.
  Web interface for configuration.

  Startup sequence:
    1. Init hardware (serial, pins, SD, RTC)
    2. Load config from EEPROM
    3. Power on GSM modem, wait for boot
    4. Init Ethernet through TRB500
    5. Sync RTC via NTP
    6. Upload missing files to FTP
    7. Start web server
    8. Start sensor reading cycle
    9. GSM auto-off after configured duration

  Created: 09/02/2026
*/

#include <SPI.h>
#include <Ethernet.h>
#include <SD.h>
#include <Wire.h>
#include <RTClib.h>
#include <avr/wdt.h>

// Project modules (include order matters: dependencies go first)
#include "Pins.h"
#include "Config.h"

// Global objects
SystemConfig   config;
RTC_DS3231     rtc;
EthernetServer webServer(80);

// Modules that reference globals above
#include "ModemControl.h"
#include "NTPSync.h"
#include "SDLogger.h"
#include "SensorManager.h"
#include "FTPClient.h"
#include "WebUI.h"

// --- FTP daily upload scheduler ---
static uint8_t       _lastUploadDay    = 0;

enum FTPScheduleState {
  FTP_IDLE,
  FTP_MODEM_BOOT,
  FTP_SYNCING,
  FTP_DONE
};
static FTPScheduleState _ftpState       = FTP_IDLE;
static unsigned long    _ftpBootStart   = 0;

void ftpCheckSchedule() {
  DateTime now = rtc.now();

  switch (_ftpState) {

    case FTP_IDLE:
      // Check if it's upload time and we haven't uploaded today
      if (now.day() != _lastUploadDay &&
          now.hour() == config.uploadHour &&
          now.minute() == config.uploadMinute) {
        Serial.println(F("[SCHED] Daily FTP upload triggered"));
        if (!modemIsOn()) {
          modemPowerOn();
          _ftpBootStart = millis();
          _ftpState = FTP_MODEM_BOOT;
        } else {
          _ftpState = FTP_SYNCING;
        }
      }
      break;

    case FTP_MODEM_BOOT:
      if (millis() - _ftpBootStart >= config.modemBootTime) {
        _ftpState = FTP_SYNCING;
      }
      break;

    case FTP_SYNCING:
      ftpSyncFiles(rtc);
      _lastUploadDay = now.day();
      modemStartTimer();  // Start auto-off timer after upload
      _ftpState = FTP_DONE;
      break;

    case FTP_DONE:
      _ftpState = FTP_IDLE;
      break;
  }
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  wdt_disable();

  // --- 1. Serial debug ---
  Serial.begin(19200);
  delay(1000);
  Serial.println(F("========================================"));
  Serial.println(F("  EOL - Environmental Monitoring Station"));
  Serial.println(F("========================================"));

  // --- 2. Init relay pins ---
  for (uint8_t i = 0; i < NUM_SENSORS; i++) {
    pinMode(RELAY_SENSOR[i], OUTPUT);
    digitalWrite(RELAY_SENSOR[i], LOW);
  }
  pinMode(RELAY_MODEM, OUTPUT);
  digitalWrite(RELAY_MODEM, LOW);
  Serial.println(F("[INIT] Relay pins configured"));

  // --- 3. Load configuration ---
  loadConfig();
  Serial.print(F("[INIT] IP: "));
  Serial.print(config.ip[0]); Serial.print('.');
  Serial.print(config.ip[1]); Serial.print('.');
  Serial.print(config.ip[2]); Serial.print('.');
  Serial.println(config.ip[3]);

  // --- 4. Init SD card ---
  initSD();

  // --- 5. Init RTC ---
  Wire.begin();
  if (!rtc.begin()) {
    Serial.println(F("[INIT] RTC not found!"));
  } else {
    DateTime now = rtc.now();
    char buf[20];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
      now.year(), now.month(), now.day(),
      now.hour(), now.minute(), now.second());
    Serial.print(F("[INIT] RTC time: "));
    Serial.println(buf);
  }

  // --- 6. Power on GSM modem ---
  Serial.println(F("[INIT] Starting GSM modem 30..."));
  modemPowerOn();
  modemWaitBoot();

  // --- 7. Init Ethernet ---
  Serial.println(F("[INIT] Starting Ethernet..."));
  IPAddress ip(config.ip[0], config.ip[1], config.ip[2], config.ip[3]);
  IPAddress gw(config.gateway[0], config.gateway[1], config.gateway[2], config.gateway[3]);
  IPAddress sn(config.subnet[0], config.subnet[1], config.subnet[2], config.subnet[3]);
  IPAddress dns(config.dns[0], config.dns[1], config.dns[2], config.dns[3]);
  Ethernet.begin(config.mac, ip, dns, gw, sn);

  // Allow Ethernet to initialize
  delay(2000);
  Serial.print(F("[INIT] Ethernet IP: "));
  Serial.println(Ethernet.localIP());

  // --- 8. Sync RTC via NTP ---
  if (!syncNTP(rtc)) {
    Serial.println(F("[INIT] NTP sync failed, using RTC time"));
  }

  // --- 9. Sync files to FTP ---
  Serial.println(F("[INIT] Starting FTP sync..."));
  int uploaded = ftpSyncFiles(rtc);
  if (uploaded >= 0) {
    Serial.print(F("[INIT] FTP sync: "));
    Serial.print(uploaded);
    Serial.println(F(" files uploaded"));
  } else {
    Serial.println(F("[INIT] FTP sync failed"));
  }

  // --- 10. Start web server ---
  initWebServer(&webServer);

  // --- 11. Start GSM auto-off timer ---
  modemStartTimer();

  // --- 12. Init sensor reading ---
  SENSOR_SERIAL.begin(19200);
  initSensors(&rtc);

  // --- 13. Enable watchdog ---
  wdt_enable(WDTO_8S);

  Serial.println(F("========================================"));
  Serial.println(F("  INIT COMPLETE - Entering main loop"));
  Serial.println(F("========================================"));
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
  wdt_reset();

  // 1. Manage modem auto-off timer
  modemCheckTimer();

  // 2. Sensor reading state machine
  updateSensors();

  // 3. Handle web server requests (only if modem is on = network available)
  if (modemIsOn()) {
    handleWebClient();
  }

  // 4. Check FTP upload schedule
  ftpCheckSchedule();

  // 5. Maintain Ethernet stack
  Ethernet.maintain();
}
