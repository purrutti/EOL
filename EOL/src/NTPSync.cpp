#include "NTPSync.h"
#include "RTCManager.h"
#include <Ethernet.h>
#include <NTPClient.h>
#include <RTClib.h>
#include <sys/time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ── RTC DS3231 ───────────────────────────────────────────────────────────────
static RTC_DS3231 _rtc;
static bool       _rtcOk = false;

bool rtcBegin() {
    _rtcOk = _rtc.begin();
    return _rtcOk;
}

bool rtcSyncToSystem() {
    if (!_rtcOk) return false;
    DateTime dt = _rtc.now();
    if (dt.year() < 2020 || dt.year() > 2100) return false;
    struct timeval tv = { (time_t)dt.unixtime(), 0 };
    settimeofday(&tv, nullptr);
    return true;
}

bool rtcSetFromSystem() {
    if (!_rtcOk) return false;
    time_t t = time(nullptr);
    if (t < 1000000000UL) return false;
    _rtc.adjust(DateTime((uint32_t)t));
    return true;
}

// ── NTP ───────────────────────────────────────────────────────────────────────
static byte _mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

struct _NtpCtx {
    uint32_t      timeoutMs;
    volatile bool done;
    volatile bool synced;
};

static void _ntpTask(void* pv) {
    auto* ctx = (_NtpCtx*)pv;
    uint32_t half = ctx->timeoutMs / 2;

    //Ethernet.init(ETH_CS_PIN);
    if (Ethernet.begin(_mac, half) != 0) {
        EthernetUDP udp;
        NTPClient   client(udp, "pool.ntp.org", 0, 0);
        client.begin();

        uint32_t t0 = millis();
        while (millis() - t0 < half) {
            if (client.update()) {
                unsigned long epoch = client.getEpochTime();
                if (epoch > 1000000000UL) {
                    struct timeval tv = { (time_t)epoch, 0 };
                    settimeofday(&tv, nullptr);
                    ctx->synced = true;
                    break;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    ctx->done = true;
    vTaskDelete(nullptr);
}

bool ntpSync(uint32_t timeoutMs) {
    _NtpCtx ctx = { timeoutMs, false, false };
    xTaskCreate(_ntpTask, "ntpSync", 8192, &ctx, 1, nullptr);

    // delay(100) cède le CPU aux idle tasks → watchdog nourri pendant le blocage DHCP
    uint32_t deadline = millis() + timeoutMs + 3000;
    while (!ctx.done && millis() < deadline) {
        delay(100);
    }
    if (ctx.synced) rtcSetFromSystem();
    return ctx.synced;
}
