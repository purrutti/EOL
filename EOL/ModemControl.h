#ifndef MODEM_CONTROL_H
#define MODEM_CONTROL_H

// ============================================================
// ModemControl.h - GSM modem (TRB500) relay control and timer
// ============================================================

static bool          _modemOn          = false;
static unsigned long _modemTimerStart  = 0;
static unsigned long _modemOnDuration  = 0;
static bool          _modemTimerActive = false;

void modemPowerOn() {
  if (!_modemOn) {
    digitalWrite(RELAY_MODEM, HIGH);
    _modemOn = true;
    Serial.println(F("[GSM] Modem powered ON"));
  }
}

void modemPowerOff() {
  if (_modemOn) {
    digitalWrite(RELAY_MODEM, LOW);
    _modemOn = false;
    _modemTimerActive = false;
    Serial.println(F("[GSM] Modem powered OFF"));
  }
}

bool modemIsOn() {
  return _modemOn;
}

// Wait for TRB500 to boot and get network connectivity
void modemWaitBoot() {
  Serial.print(F("[GSM] Waiting for modem boot ("));
  Serial.print(config.modemBootTime / 1000);
  Serial.println(F("s)..."));

  unsigned long start = millis();
  while (millis() - start < config.modemBootTime) {
    delay(1000);
    Serial.print('.');
  }
  Serial.println();
  Serial.println(F("[GSM] Modem boot wait complete"));
}

// Start the auto-off timer
void modemStartTimer() {
  _modemOnDuration  = config.gsmOnDuration;
  _modemTimerStart  = millis();
  _modemTimerActive = true;
  Serial.print(F("[GSM] Auto-off timer started: "));
  Serial.print(_modemOnDuration / 1000);
  Serial.println(F("s"));
}

// Reset the auto-off timer (call on web activity)
void modemResetTimer() {
  if (_modemTimerActive) {
    _modemTimerStart = millis();
  }
}

// Check timer and power off modem if expired. Call from loop().
void modemCheckTimer() {
  if (!_modemOn || !_modemTimerActive) return;

  if (millis() - _modemTimerStart >= _modemOnDuration) {
    Serial.println(F("[GSM] Auto-off timer expired"));
    modemPowerOff();
  }
}

#endif
