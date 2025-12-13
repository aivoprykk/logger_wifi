#ifndef BD855F76_0F67_4C33_A9AC_BFC741205190
#define BD855F76_0F67_4C33_A9AC_BFC741205190

#ifdef __cplusplus
  extern "C" {
#endif

#include "esp_wifi.h"
#include "freertos/event_groups.h"

// WiFi event group bits
#define WIFI_CONNECTED_BIT BIT0     // STA connected and has IP
#define WIFI_FAIL_BIT BIT1          // STA connection failed
#define WIFI_TIME_SYNC_BIT BIT2     // Time synchronized via SNTP
#define WIFI_AP_READY_BIT BIT3      // AP mode fully initialized

// struct context_s;
#define WIFI_MODES_LIST(l) \
  l(null, 0) \
  l(sta, 1) \
  l(ap, 2) \
  l(apsta, 3)

#define WIFI_MODES_ENUM(l, m) wifi_mode_##l = m,

enum wifi_modes_s {
  WIFI_MODES_LIST(WIFI_MODES_ENUM)
};

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
  bool Wifi_on;
  bool s_ap_connection;
  bool s_wifi_started;
  bool s_wifi_initialized;
  // bool s_nvs_initialized;

  bool s_sta_connection;
  bool s_sta_connecting;
  bool s_sta_connected;
  bool s_sta_got_ip;
  bool s_sta_connect_not_found;

  bool s_sta_connect_error;
  uint8_t s_sta_num_connect;
  uint8_t s_retry_num;
  uint8_t s_wifi_mode;
  struct cfg_item ap;
  struct cfg_item stas[M_WIFI_STA_MAX];
  char hostname[32];
  float offset;
  EventGroupHandle_t s_wifi_event_group;  // Event group for WiFi state synchronization
};

#define WIFI_CONTEXT_DEFAULT() { \
    .Wifi_on = 0, \
        .s_ap_connection = 0, \
        .s_wifi_started = 0,  \
        .s_wifi_initialized = 0, \
        .s_sta_connection = 0,  \
        .s_sta_connecting = 0, \
        .s_sta_connected = 0, \
        .s_sta_got_ip = 0, \
        .s_sta_connect_not_found = 0,  \
        .s_sta_connect_error = 0, \
        .s_retry_num=10,  \
        .s_sta_num_connect = M_WIFI_STA_MAX + 1, \
        .s_wifi_mode = wifi_mode_ap, \
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
int wifi_sta_connect(uint16_t slot);
int wifi_sta_connect_scan();
void wifi_init();
int wifi_uninit();
int wifi_mode(uint8_t sta, uint8_t ap);
int wifi_status();
int wifi_wait_for_connection(uint32_t timeout_ms);
int wifi_wait_for_ap_ready(uint32_t timeout_ms);
int wifi_wait_for_time_sync(uint32_t timeout_ms);
int wifi_set_config(const char *ap_ssid, const char *ap_password, const char *sta_ssid, const char *sta_password);
int wifi_sta_set_config(int num, const char *sta_ssid, const char *sta_password);
int wifi_ap_set_config(const char *ap_ssid, const char *ap_password);

// WiFi mode change handling with callback support for external dependencies
typedef struct {
    void (*before_mode_change)(void);      // Called before mode change (e.g., ADC suppression, config sync)
    void (*after_mode_change_complete)(void); // Called after mode change is complete (e.g., ADC resumption)
} wifi_mode_change_callbacks_t;

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
