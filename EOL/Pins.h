#ifndef PINS_H
#define PINS_H

// ============================================================
// Pins.h - Hardware pin definitions for M-Duino 42+
// ============================================================

// Relay outputs for sensor power supply (one relay per sensor)
const uint8_t RELAY_SENSOR[4] = {36, 37, 38, 39};

// Relay output for GSM modem (TRB500)
const uint8_t RELAY_MODEM = 40;

// SD card SPI chip select
const uint8_t PIN_SD_CS = 53;

// Ethernet W5500 SPI chip select
const uint8_t PIN_ETH_CS = 10;

// RS232 serial port used for sensor communication
#define SENSOR_SERIAL Serial2

#endif
