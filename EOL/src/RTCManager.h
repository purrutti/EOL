#pragma once
#include <Arduino.h>

// Initialise le DS3231 (I2C). Retourne true si le chip répond.
bool rtcBegin();

// Lit le RTC et applique l'heure à l'horloge système (POSIX).
// Retourne false si le RTC est absent ou si l'année est hors plage 2020-2100.
bool rtcSyncToSystem();

// Écrit l'heure système (issue d'un NTP réussi) dans le RTC.
// Retourne false si le RTC est absent ou si l'heure système est invalide.
bool rtcSetFromSystem();
