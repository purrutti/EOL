#include "NTPSync.h"
#include <ETH.h>

#define NTP_SERVER1  "pool.ntp.org"
#define NTP_SERVER2  "time.nist.gov"

bool ntpSync(uint32_t timeoutMs) {
    uint32_t half = timeoutMs / 2;

    // ── Init Ethernet (LAN8720 RMII, clock sortie GPIO17) ────────────────────
    ETH.begin(NTP_ETH_PHY_ADDR,
              NTP_ETH_POWER_PIN,
              NTP_ETH_MDC_PIN,
              NTP_ETH_MDIO_PIN,
              ETH_PHY_LAN8720,
              ETH_CLOCK_GPIO17_OUT);

    // ── Attendre une adresse DHCP ────────────────────────────────────────────
    uint32_t t0 = millis();
    while (millis() - t0 < half) {
        if (ETH.localIP() != IPAddress(0, 0, 0, 0)) break;
        delay(200);
    }
    if (ETH.localIP() == IPAddress(0, 0, 0, 0)) return false;

    // ── Synchronisation NTP (UTC, pas de DST) ────────────────────────────────
    configTime(0, 0, NTP_SERVER1, NTP_SERVER2);

    struct tm ti;
    return getLocalTime(&ti, half);
}
