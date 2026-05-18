#pragma once
#include <Arduino.h>

// Configuration Ethernet – Industrial Shields ESP32 PLC 21 (LAN8720 RMII)
// Ajuster si la version de la carte diffère.
#ifndef NTP_ETH_PHY_ADDR
#  define NTP_ETH_PHY_ADDR    0
#endif
#ifndef NTP_ETH_POWER_PIN
#  define NTP_ETH_POWER_PIN  -1
#endif
#ifndef NTP_ETH_MDC_PIN
#  define NTP_ETH_MDC_PIN    23
#endif
#ifndef NTP_ETH_MDIO_PIN
#  define NTP_ETH_MDIO_PIN   18
#endif

// Initialise l'Ethernet (DHCP) et synchronise l'horloge système via NTP (UTC).
// Retourne true si la synchronisation a réussi.
// Appel bloquant : timeout total partagé moitié DHCP / moitié NTP.
bool ntpSync(uint32_t timeoutMs = 20000UL);
