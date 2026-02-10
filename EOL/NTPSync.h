#ifndef NTP_SYNC_H
#define NTP_SYNC_H

// ============================================================
// NTPSync.h - NTP time synchronization for RTC
// ============================================================

#include <EthernetUdp.h>
#include <Dns.h>

#define NTP_PACKET_SIZE 48
#define NTP_PORT        123
#define NTP_TIMEOUT     5000

static EthernetUDP _ntpUDP;
static byte _ntpBuffer[NTP_PACKET_SIZE];

// Build an NTP request packet
static void ntpBuildPacket() {
  memset(_ntpBuffer, 0, NTP_PACKET_SIZE);
  _ntpBuffer[0]  = 0b11100011;  // LI=3, Version=4, Mode=3 (client)
  _ntpBuffer[1]  = 0;           // Stratum
  _ntpBuffer[2]  = 6;           // Polling interval
  _ntpBuffer[3]  = 0xEC;        // Peer clock precision
  // Bytes 4-11: root delay and dispersion (leave 0)
  _ntpBuffer[12] = 49;          // Reference ID: "1N"
  _ntpBuffer[13] = 0x4E;
  _ntpBuffer[14] = 49;
  _ntpBuffer[15] = 52;
}

// Synchronize RTC via NTP through TRB500 network
// Returns true on success
bool syncNTP(RTC_DS3231& rtc) {
  Serial.println(F("[NTP] Starting time sync..."));

  // Resolve NTP server hostname
  DNSClient dnsClient;
  dnsClient.begin(Ethernet.dnsServerIP());

  IPAddress ntpIP;
  if (dnsClient.getHostByName(config.ntpServer, ntpIP) != 1) {
    Serial.print(F("[NTP] DNS resolution failed for: "));
    Serial.println(config.ntpServer);
    return false;
  }

  Serial.print(F("[NTP] Server IP: "));
  Serial.println(ntpIP);

  _ntpUDP.begin(8888);  // Local port for NTP

  // Send NTP request
  ntpBuildPacket();
  _ntpUDP.beginPacket(ntpIP, NTP_PORT);
  _ntpUDP.write(_ntpBuffer, NTP_PACKET_SIZE);
  _ntpUDP.endPacket();

  // Wait for response
  unsigned long start = millis();
  int packetSize = 0;
  while (millis() - start < NTP_TIMEOUT) {
    packetSize = _ntpUDP.parsePacket();
    if (packetSize >= NTP_PACKET_SIZE) break;
    delay(50);
  }

  if (packetSize < NTP_PACKET_SIZE) {
    Serial.println(F("[NTP] No response (timeout)"));
    _ntpUDP.stop();
    return false;
  }

  _ntpUDP.read(_ntpBuffer, NTP_PACKET_SIZE);
  _ntpUDP.stop();

  // Extract transmit timestamp (bytes 40-43 = seconds since 1900-01-01)
  unsigned long secsSince1900 =
    ((unsigned long)_ntpBuffer[40] << 24) |
    ((unsigned long)_ntpBuffer[41] << 16) |
    ((unsigned long)_ntpBuffer[42] <<  8) |
    ((unsigned long)_ntpBuffer[43]);

  // Convert to Unix time (seconds since 1970-01-01)
  const unsigned long seventyYears = 2208988800UL;
  unsigned long epoch = secsSince1900 - seventyYears;

  // Apply GMT offset
  epoch += (long)config.gmtOffset * 3600L;

  // Update RTC
  DateTime ntpTime(epoch);
  rtc.adjust(ntpTime);

  Serial.print(F("[NTP] RTC synced: "));
  char buf[20];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
    ntpTime.year(), ntpTime.month(), ntpTime.day(),
    ntpTime.hour(), ntpTime.minute(), ntpTime.second());
  Serial.println(buf);

  return true;
}

#endif
