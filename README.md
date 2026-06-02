# EOL — Autonomous Oceanographic Data Acquisition System

**EOL** is an autonomous embedded system deployed on a surface buoy. It cyclically interrogates three oceanographic sensors, logs data to SD card, and transmits daily CSV files to a remote FTP server via a 5G modem.

> **Status:** Development → Lab testing (upcoming) → Sea deployment (~1 month)

---

## System at a Glance

| Feature | Value |
|---|---|
| Controller | Industrial Shields ESP32 PLC 21 |
| Sensors | CTD SBE SMP ODO · FLNTU ECO FLNTURT · PiSAMI pH |
| Communication | RS232 multiplexed, relay-switched power |
| Storage | SD card, per-day CSV files |
| Connectivity | W5500 Ethernet + Teltonika TRB500 5G modem |
| Time sync | NTP via 5G → pool.ntp.org, DS3231 RTC backup |
| Measurement schedule | Every 30 min at **HH:00** and **HH:30** UTC |
| FTP upload | Daily at **00:00** and **12:00** UTC |
| FTP server | `oceane.obs-vlfr.fr` |
| Web UI | WiFi AP `EOL` / `Eol696969` or Ethernet `192.168.2.231` |

---

## Architecture

```
48 Vdc input
     │
  DCDC 48 V → 12 V
     │
  ESP32 PLC 21  ──── W5500 Ethernet (192.168.2.231) ────┐
  │  DS3231 RTC                                          │
  │  SD card                                          TRB500 5G modem (gateway 192.168.2.1)
  │                                                      │
  RS232 MUX                                       Internet
  ├─ Q0.0 ─ CTD SBE SMP ODO        9 600 baud     ├─ pool.ntp.org  (NTP)
  ├─ Q0.1 ─ FLNTU ECO FLNTURT     19 200 baud     └─ oceane.obs-vlfr.fr  (FTP)
  ├─ Q0.2 ─ PiSAMI pH             57 600 baud
  └─ Q0.5 ─ TRB500 power relay
```

---

## Repository Structure

```
EOL/
├── EOL.ino                   Main sketch: state machine, scheduling
└── src/
    ├── CTD.h / .cpp          CTD driver + PSS-78 salinity calculation
    ├── FLNTU.h / .cpp        Fluorescence/turbidity driver
    ├── PiSAMI.h / .cpp       pH sensor communication driver
    ├── PiSAMI_decode.h/.cpp  pH hex-frame decoding and pH calculation
    ├── EthernetManager.h     Ethernet init, NTP sync, RTC management, FTP upload
    ├── NTPSync.cpp           Ethernet/NTP/FTP/RTC implementation
    ├── SDLogger.h / .cpp     SD card CSV logging and error log
    └── WebUI.h / .cpp        Web interface (WiFi AP + Ethernet server)
```

---

## Data Files on SD Card

| File | Separator | Columns |
|---|---|---|
| `/data/YYYYMMDD_EOL_CTD.csv` | `,` | Temperature · Conductivity · Oxygen · Salinity · DateTime |
| `/data/YYYYMMDD_EOL_FLNTU.csv` | `;` | Date · Time · Chl_WL · Chl_Val · NTU_WL · NTU_Val · Thermistor |
| `/data/YYYYMMDD_EOL_SAMI.csv` | `;` | Date · Time · T_init · T_final · Salinity · pH · Battery |
| `/data/errors.log` | `,` | DateTime · Source · Message |

---

## Web Interface

| Access | Address |
|---|---|
| WiFi AP (local) | Connect to `EOL` / `Eol696969`, open `http://192.168.4.1` |
| Ethernet | `http://192.168.2.231` |

| Page | Description |
|---|---|
| `/` | Live debug log, auto-refreshed every second |
| `/files` | SD card file browser with CSV download |
| `/update` | OTA firmware update (upload `.bin`) |

---

## Measurement Cycle (per 30 min slot)

```
S_IDLE
  └─► S_CTD_SETTLE   relay ON, 500 ms warm-up
      └─► S_CTD1     wakeup "tps", 1 s
          └─► S_CTD2 sample 1, 30 s  → CTD CSV
              └─► S_CTD3 sample 2, 30 s  → CTD CSV
                  └─► S_FLNTU_SETTLE  relay ON, 3.5 s
                      └─► S_FLNTU  "$run", 30 s  → FLNTU CSV
                          └─► S_PISAMI_SETTLE  relay ON, 3.5 s
                              └─► S_PISAMI  120 s  → SAMI CSV
                                  └─► S_IDLE
```

## FTP Upload Cycle (00:00 and 12:00 UTC)

```
S_IDLE ─► S_FTP_GSM_ON  relay ON, 5 s power-up
           └─► S_FTP_NTP  poll NTP every 5 s (6 min timeout) → RTC update
               └─► S_FTP_UPLOAD  ftpUploadDaily() → relay OFF
                   └─► S_IDLE
```
Retry on failure: 15 min backoff.

---

## Dependencies

- [Ethernet](https://github.com/arduino-libraries/Ethernet) (W5500)
- [NTPClient](https://github.com/arduino-libraries/NTPClient)
- [RTClib](https://github.com/adafruit/RTClib) (DS3231)
- [SD](https://github.com/arduino-libraries/SD)
- [WebServer](https://github.com/espressif/arduino-esp32) (ESP32 core)
- Industrial Shields ESP32 board package

---

## Contact

CNRS — Observatoire Océanologique de Villefranche-sur-Mer
