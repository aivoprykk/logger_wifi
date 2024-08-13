#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"
#include "esp_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

struct context_s;

extern const char * wifi_event_strings[];

struct cfg_item {
  char ssid[32];
  char password[64];
  uint8_t ipv4_address[4];
  uint8_t ipv4_netmask[4];
  uint8_t ipv4_gw[4];
};

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

int wifi_ap_start();
int wifi_sta_connect(uint16_t slot);
int wifi_sta_connect_scan();
void wifi_init();
int wifi_uninit();
esp_err_t wifi_disconnect();
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
void uint8_to_hex_string(uint8_t value, char *hex_str);
void mac_to_char(uint8_t *mac, char *mac_str, uint8_t start);

#ifdef __cplusplus
}
#endif
#endif
