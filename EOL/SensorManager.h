#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

// ============================================================
// SensorManager.h - Non-blocking sensor reading state machine
// ============================================================

// State machine states
enum SensorState {
  SS_IDLE,        // Waiting for next scheduled reading
  SS_POWER_ON,    // Relay on, waiting for warmup
  SS_SEND_CMD,    // Send RS232 command
  SS_WAIT_RESP,   // Waiting for response
  SS_PROCESS,     // Process received data
  SS_POWER_OFF    // Turn off relay
};

#define RESP_BUFFER_SIZE 256

static SensorState   _sensorState      = SS_IDLE;
static uint8_t       _currentSensor    = 0;
static unsigned long _sensorTimerStart = 0;
static unsigned long _lastRead[NUM_SENSORS] = {0, 0, 0, 0};
static char          _respBuffer[RESP_BUFFER_SIZE];
static uint16_t      _respPos          = 0;

// RTC reference (set from EOL.ino)
static RTC_DS3231*   _sensorRTC = nullptr;

void initSensors(RTC_DS3231* rtcRef) {
  _sensorRTC = rtcRef;
  _sensorState = SS_IDLE;

  // Initialize relay pins
  for (uint8_t i = 0; i < NUM_SENSORS; i++) {
    pinMode(RELAY_SENSOR[i], OUTPUT);
    digitalWrite(RELAY_SENSOR[i], LOW);
    _lastRead[i] = millis();  // Start counting from now
  }

  Serial.println(F("[SENS] Sensor manager initialized"));
}

// Find the next sensor that is due for reading.
// Returns sensor index, or -1 if none due.
static int8_t findNextSensor() {
  unsigned long now = millis();
  unsigned long oldestDue = 0;
  int8_t best = -1;

  for (uint8_t i = 0; i < NUM_SENSORS; i++) {
    if (!config.sensors[i].enabled) continue;
    if (config.sensors[i].interval == 0) continue;

    unsigned long elapsed = now - _lastRead[i];
    if (elapsed >= config.sensors[i].interval) {
      // This sensor is due; pick the most overdue one
      if (best < 0 || elapsed > oldestDue) {
        oldestDue = elapsed;
        best = i;
      }
    }
  }
  return best;
}

// Call from loop() to drive the sensor reading state machine
void updateSensors() {
  unsigned long now = millis();

  switch (_sensorState) {

    case SS_IDLE: {
      int8_t next = findNextSensor();
      if (next < 0) return;  // Nothing due

      _currentSensor = next;
      _sensorState = SS_POWER_ON;
      _sensorTimerStart = now;

      // Power on sensor relay
      digitalWrite(RELAY_SENSOR[_currentSensor], HIGH);

      // Configure serial baud rate for this sensor
      SENSOR_SERIAL.end();
      SENSOR_SERIAL.begin(config.sensors[_currentSensor].baud);

      Serial.print(F("[SENS] Power ON sensor "));
      Serial.println(_currentSensor + 1);
      break;
    }

    case SS_POWER_ON: {
      // Wait for warmup
      if (now - _sensorTimerStart >= config.sensors[_currentSensor].warmup) {
        _sensorState = SS_SEND_CMD;
      }
      break;
    }

    case SS_SEND_CMD: {
      // Flush any stale data in serial buffer
      while (SENSOR_SERIAL.available()) SENSOR_SERIAL.read();

      // Send measurement command
      const char* cmd = config.sensors[_currentSensor].command;
      if (strlen(cmd) > 0) {
        SENSOR_SERIAL.print(cmd);
        // Send CR+LF if the command doesn't already end with them
        size_t cmdLen = strlen(cmd);
        if (cmdLen > 0 && cmd[cmdLen - 1] != '\n' && cmd[cmdLen - 1] != '\r') {
          SENSOR_SERIAL.print("\r\n");
        }

        Serial.print(F("[SENS] Sent command to sensor "));
        Serial.print(_currentSensor + 1);
        Serial.print(F(": "));
        Serial.println(cmd);
      }

      _respPos = 0;
      _respBuffer[0] = '\0';
      _sensorTimerStart = now;
      _sensorState = SS_WAIT_RESP;
      break;
    }

    case SS_WAIT_RESP: {
      // Read available bytes
      while (SENSOR_SERIAL.available() && _respPos < RESP_BUFFER_SIZE - 1) {
        char c = SENSOR_SERIAL.read();
        _respBuffer[_respPos++] = c;
        _respBuffer[_respPos] = '\0';

        // Check for end marker
        const char* marker = config.sensors[_currentSensor].endMarker;
        if (strlen(marker) > 0 && _respPos >= strlen(marker)) {
          if (strstr(_respBuffer + _respPos - strlen(marker), marker)) {
            // End marker found
            _sensorState = SS_PROCESS;
            return;
          }
        }
      }

      // Check timeout
      if (now - _sensorTimerStart >= config.sensors[_currentSensor].timeout) {
        Serial.print(F("[SENS] Timeout on sensor "));
        Serial.println(_currentSensor + 1);
        _sensorState = SS_PROCESS;
      }
      break;
    }

    case SS_PROCESS: {
      // Trim trailing whitespace and end markers
      while (_respPos > 0 &&
             (_respBuffer[_respPos - 1] == '\r' ||
              _respBuffer[_respPos - 1] == '\n' ||
              _respBuffer[_respPos - 1] == ' ')) {
        _respPos--;
        _respBuffer[_respPos] = '\0';
      }

      Serial.print(F("[SENS] Response from sensor "));
      Serial.print(_currentSensor + 1);
      Serial.print(F(": ["));
      Serial.print(_respBuffer);
      Serial.println(']');

      // Log to SD card
      if (_sensorRTC && _respPos > 0) {
        DateTime dt = _sensorRTC->now();
        logSensorData(dt, _currentSensor, _respBuffer);
      }

      _lastRead[_currentSensor] = now;
      _sensorState = SS_POWER_OFF;
      break;
    }

    case SS_POWER_OFF: {
      // Power off sensor relay
      digitalWrite(RELAY_SENSOR[_currentSensor], LOW);

      Serial.print(F("[SENS] Power OFF sensor "));
      Serial.println(_currentSensor + 1);

      _sensorState = SS_IDLE;
      break;
    }
  }
}

// Force an immediate reading of a specific sensor (blocking).
// Used for testing from the web UI.
bool readSensorBlocking(uint8_t idx, char* outBuf, uint16_t outBufSize) {
  if (idx >= NUM_SENSORS) return false;
  if (_sensorState != SS_IDLE) return false;  // Busy

  SensorConfig& sc = config.sensors[idx];

  // Power on
  digitalWrite(RELAY_SENSOR[idx], HIGH);
  SENSOR_SERIAL.end();
  SENSOR_SERIAL.begin(sc.baud);
  delay(sc.warmup);

  // Flush and send command
  while (SENSOR_SERIAL.available()) SENSOR_SERIAL.read();
  if (strlen(sc.command) > 0) {
    SENSOR_SERIAL.print(sc.command);
    size_t cmdLen = strlen(sc.command);
    if (cmdLen > 0 && sc.command[cmdLen - 1] != '\n' && sc.command[cmdLen - 1] != '\r') {
      SENSOR_SERIAL.print("\r\n");
    }
  }

  // Wait for response
  uint16_t pos = 0;
  unsigned long start = millis();
  while (millis() - start < sc.timeout && pos < outBufSize - 1) {
    if (SENSOR_SERIAL.available()) {
      char c = SENSOR_SERIAL.read();
      outBuf[pos++] = c;
      outBuf[pos] = '\0';

      if (strlen(sc.endMarker) > 0 && pos >= strlen(sc.endMarker)) {
        if (strstr(outBuf + pos - strlen(sc.endMarker), sc.endMarker)) {
          break;
        }
      }
    }
  }

  // Power off
  digitalWrite(RELAY_SENSOR[idx], LOW);

  // Trim
  while (pos > 0 && (outBuf[pos - 1] == '\r' || outBuf[pos - 1] == '\n' || outBuf[pos - 1] == ' ')) {
    pos--;
    outBuf[pos] = '\0';
  }

  return pos > 0;
}

#endif
