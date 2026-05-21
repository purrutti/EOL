#pragma once
#include <Arduino.h>

// Initialise le W5500 (DHCP) et synchronise l'horloge système via NTP (UTC).
// Retourne true si la sync a réussi.
// Appel bloquant : timeout partagé moitié DHCP / moitié NTP.
bool ntpSync(uint32_t timeoutMs = 20000UL);
