#ifndef BD855F76_0F67_4C33_A9AC_BFC741205190
#define BD855F76_0F67_4C33_A9AC_BFC741205190

#ifdef __cplusplus
  extern "C" {
#endif

#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "esp_event.h"

// WiFi mode change event base for asynchronous mode changes (uses default event loop)
ESP_EVENT_DECLARE_BASE(WIFI_MODE_CHANGE_EVENT);

// WiFi mode change event IDs
enum {
    WIFI_MODE_CHANGE_REQUEST = 0,  // Request WiFi mode change (AP ↔ STA ↔ APSTA)
};

// WiFi event group bits
#define WIFI_CONNECTED_BIT BIT0     // STA connected and has IP
#define WIFI_FAIL_BIT BIT1          // STA connection failed
#define WIFI_TIME_SYNC_BIT BIT2     // Time synchronized via SNTP
#define WIFI_AP_READY_BIT BIT3      // AP mode fully initialized
#define WIFI_STA_CONNECTING_BIT BIT4 // STA is in the process of connecting
#define WIFI_SCANNING_BIT BIT5      // WiFi scan in progress

const char * wifi_event_strings(int id);

struct cfg_item {
  char ssid[32];
  char password[64];
  uint8_t ipv4_address[4];
  uint8_t ipv4_netmask[4];
  uint8_t ipv4_gw[4];
};

#define WIFI_CFG_ITEM_DEFAULT(a) { \
    .ssid = a, \
        .password = {0}, \
        .ipv4_address = {0, 0, 0, 0}, \
        .ipv4_netmask = {0, 0, 0, 0}, \
        .ipv4_gw = {0, 0, 0, 0} \
}

#define M_WIFI_STA_MAX 4

struct m_wifi_context {
  // bool s_ap_connection;      // TODO: Redundant with WIFI_AP_READY_BIT in event group
  bool s_wifi_started;
  bool s_wifi_initialized;
  uint8_t s_sta_num_connect;
  uint8_t s_retry_num;
  // uint8_t s_wifi_mode;       // TODO: Can be queried from ESP-IDF with wifi_get_current_mode()
  struct cfg_item ap;
  struct cfg_item stas[M_WIFI_STA_MAX];
  char hostname[32];
  float offset;
  EventGroupHandle_t s_wifi_event_group;  // Event group for WiFi state synchronization
};

#define WIFI_CONTEXT_DEFAULT() { \
    .s_wifi_started = 0,  \
    .s_wifi_initialized = 0, \
    .s_retry_num=10,  \
    .s_sta_num_connect = M_WIFI_STA_MAX + 1, \
    .ap = {"ESP32AP","password",{10,10,10,1},{255,255,255,0},{10,10,10,1}}, \
    .stas = { \
        WIFI_CFG_ITEM_DEFAULT(""), \
        WIFI_CFG_ITEM_DEFAULT(""), \
        WIFI_CFG_ITEM_DEFAULT(""), \
        WIFI_CFG_ITEM_DEFAULT(""), \
    }, \
    .hostname = {0}, \
    .offset = 0.0, \
    .s_wifi_event_group = NULL, \
}

extern struct m_wifi_context wifi_context;

int wifi_ap_start();
void wifi_init();
int wifi_uninit();
int wifi_mode(uint8_t sta, uint8_t ap);
int wifi_status();
#if defined(ENABLE_WAIT_FOR_STATE)
int wifi_wait_for_connection(uint32_t timeout_ms);
int wifi_wait_for_ap_ready(uint32_t timeout_ms);
#endif
int wifi_wait_for_time_sync(uint32_t timeout_ms);
int wifi_set_config(const char *ap_ssid, const char *ap_password, const char *sta_ssid, const char *sta_password);
int wifi_sta_set_config(int num, const char *sta_ssid, const char *sta_password);
int wifi_ap_set_config(const char *ap_ssid, const char *ap_password);

// WiFi mode change handling with callback support for external dependencies
typedef struct {
    void (*before_mode_change)(void);      // Called before mode change (e.g., ADC suppression, config sync)
    void (*after_mode_change_complete)(void); // Called after mode change is complete (e.g., ADC resumption)
} wifi_mode_change_callbacks_t;

// Unified WiFi state access functions
bool wifi_is_sta_connecting(void);  // Check if STA is started (has WIFI_STA_CONNECTING_BIT)
bool wifi_is_sta_scanning(void);    // Check if STA is scanning (has WIFI_SCANNING_BIT)
bool wifi_is_sta_connected(void);      // Check if STA is connected (has WIFI_CONNECTED_BIT)
bool wifi_is_sta_connection_failed(void);  // Check if connection failed (has WIFI_FAIL_BIT)
bool wifi_is_ap_ready(void);           // Check if AP is ready (has WIFI_AP_READY_BIT)
bool wifi_is_time_synced(void);        // Check if time is synced (has WIFI_TIME_SYNC_BIT)
wifi_mode_t wifi_get_current_mode(void); // Get current mode from ESP-IDF (not cached)

void wifi_set_mode_change_callbacks(const wifi_mode_change_callbacks_t* callbacks);
int wifi_request_mode_change(void);  // Unified mode change with callback support

// Memory monitoring and diagnostics
void wifi_log_memory_usage(const char* context);
void wifi_prepare_memory_for_gps(void);  // Aggressive memory cleanup for GPS mode

uint8_t set_time_zone(float offset);
void initialize_sntp(float offset);
int uninitialize_sntp();
int wifi_wait_for_time_sync(uint32_t timeout_ms);

// SNTP

uint8_t set_time_zone(float offset);
void initialize_sntp(float offset);
int uninitialize_sntp(void);
//void obtain_sntp_time(void);
void print_local_time();

#ifdef __cplusplus
}
#endif
#endif /* BD855F76_0F67_4C33_A9AC_BFC741205190 */
