#ifndef BD855F76_0F67_4C33_A9AC_BFC741205190
#define BD855F76_0F67_4C33_A9AC_BFC741205190

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_wifi.h"

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
        .password = "", \
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
  bool s_nvs_initialized;

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
  //struct context_s * m_context;
};

#define WIFI_CONTEXT_DEFAULT() { \
    .Wifi_on = 0, \
        .s_ap_connection = 0, \
        .s_wifi_started = 0,  \
        .s_wifi_initialized = 0, \
        .s_nvs_initialized = 0, \
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
        .hostname = {0} \
}

int wifi_ap_start();
int wifi_sta_connect(uint16_t slot);
int wifi_sta_connect_scan();
void wifi_init();
int wifi_uninit();
int wifi_disconnect();
int wifi_mode(uint8_t sta, uint8_t ap);
int wifi_status();
int wifi_set_config(const char *ap_ssid, const char *ap_password, const char *sta_ssid, const char *sta_password);
int wifi_sta_set_config(int num, const char *sta_ssid, const char *sta_password);
int wifi_ap_set_config(const char *ap_ssid, const char *ap_password);

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
