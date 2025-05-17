#include "logger_wifi_private.h"
#ifdef CONFIG_LOGGER_WIFI_ENABLED

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_netif_sntp.h"
#include "esp_sntp.h"

#include "logger_wifi.h"
#include "logger_events.h"
#ifdef CONFIG_GPS_LOG_ENABLED
#include "gps_user_cfg.h"
#endif
static const char *TAG = "sntp";

void print_local_time() {
    struct tm timeinfo;
    time_t now = 0;
    time(&now);
    localtime_r(&now, &timeinfo);
    WLOG(TAG, "NTP Time = %02d-%02d-%d %02d:%02d:%02d", timeinfo.tm_mday, (timeinfo.tm_mon) + 1, (timeinfo.tm_year) + 1900, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

uint8_t set_time_zone(float offset) {
    //ESP_LOGI(TAG, "[%s]", __FUNCTION__);
    uint8_t ret = 0;
#if defined(DLS)
    const char *cst = 0;
    uint16_t offh = offset > 1000 ? SEC_TO_HOUR(offset) : offset;
    if (offh == 0) {
        cst = "GMTGMT-1,M3.4.0/01,M10.4.0/02";
    } else if (offh == 1) {
        cst = "CET-1CEST,M3.5.0,M10.5.0/3";
    } else if (offh == 2 || offh == 3) { // 3 is for daylight saving time
        cst = "EET-2EEST,M3.5.0/3,M10.5.0/4";
    } else {
        cst = "GMT0";
        ret = 1;
    }
    ILOG(TAG, "posix timezone: %s", cst);
    setenv("TZ", cst, 1);
#else
    setenv("TZ", "UTC", 0);
#endif
    tzset();
    return ret;
}

// #ifdef CONFIG_SNTP_TIME_SYNC_METHOD_CUSTOM
void sntp_sync_time(struct timeval *tv) {
#if CONFIG_GPS_LOG_ENABLED
    tv->tv_sec += (time_t)HOUR_TO_SEC(c_gps_cfg.timezone);
#endif
    tv->tv_usec = 0;  // clean utc time !!
    settimeofday(tv, NULL);
    ILOG(TAG, "[%s] Time is synchronized from custom code", __FUNCTION__);
    sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
    //m_context.NTP_time_set = 1;
    esp_event_post(LOGGER_EVENT, LOGGER_EVENT_DATETIME_SET, NULL,0, portMAX_DELAY);
    print_local_time();
}
// #endif

static void time_sync_notification_cb(struct timeval *tv) {
    ILOG(TAG, "[%s] Notification of a time synchronization event", __FUNCTION__);
    esp_event_post(LOGGER_EVENT, LOGGER_EVENT_DATETIME_SET, NULL,0, portMAX_DELAY);
}

static uint8_t sntp_initialized = 0;
void initialize_sntp(float offset) {
    if (sntp_initialized)
        return;
    //ESP_LOGI(TAG, "[%s]", __FUNCTION__);
    set_time_zone(offset);
#if CONFIG_LWIP_SNTP_MAX_SERVERS > 1
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
                                                                      2, ESP_SNTP_SERVER_LIST(CONFIG_SNTP_TIME_SERVER, "pool.ntp.org"));
#else
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);
#endif
    config.start = false;
    // config.server_from_dhcp = true;
    // config.renew_servers_after_new_IP = true;
    // config.index_of_first_server = 1;
    config.ip_event_to_renew = IP_EVENT_STA_GOT_IP;
    config.sync_cb = time_sync_notification_cb;
    esp_netif_sntp_init(&config);
    sntp_initialized = 1;
}

int uninitialize_sntp() {
    //ESP_LOGI(TAG, "[%s]", __FUNCTION__);
    if (sntp_initialized) {
        esp_netif_sntp_deinit();
        sntp_initialized = 0;
    }
    return ESP_OK;
}

// void obtain_sntp_time(void) {
//     //ESP_LOGI(TAG, "[%s]", __FUNCTION__);
//     if (!sntp_initialized)
//         initialize_sntp();
//     time_t now = 0;
//     struct tm timeinfo;
//     memset(&timeinfo, 0, sizeof(struct tm));
//     time(&now);
//     localtime_r(&now, &timeinfo);
//     print_local_time();
// }

#endif  // CONFIG_LOGGER_WIFI_ENABLED