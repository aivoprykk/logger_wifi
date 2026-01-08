#ifndef A19B2FB6_9F61_42E6_B99E_D5E4011612C4
#define A19B2FB6_9F61_42E6_B99E_D5E4011612C4

#ifdef __cplusplus
extern "C" {
#endif

#include "sdkconfig.h"
#ifdef CONFIG_LOGGER_WIFI_ENABLED
#include "esp_wifi.h"
#endif


#include "logger_wifi.h"
#if (defined(CONFIG_LOGGER_USE_GLOBAL_LOG_LEVEL) && CONFIG_LOGGER_GLOBAL_LOG_LEVEL < CONFIG_LOGGER_WIFI_LOG_LEVEL)
#define C_LOG_LEVEL CONFIG_LOGGER_GLOBAL_LOG_LEVEL
#else
#define C_LOG_LEVEL CONFIG_LOGGER_WIFI_LOG_LEVEL
#endif
#include "common_log.h"


// Forward declarations
static void _wifi_mode_change_internal(void);
static int wifi_sta_config_init(void);
static int wifi_sta_connect(uint16_t num);
int wifi_sta_connect_scan();

#ifdef __cplusplus
}
#endif


#endif /* A19B2FB6_9F61_42E6_B99E_D5E4011612C4 */
