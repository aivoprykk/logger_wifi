
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "lwip/err.h"
#include "lwip/ip_addr.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "logger_wifi.h"
#include "logger_wifi_private.h"

#define TAG "wifi"
#define IPIPSTR(a) a[0], a[1], a[2], a[3]
const char * wifi_event_strings[] = {
  "WIFI_EVENT_WIFI_READY",
  "WIFI_EVENT_SCAN_DONE",
  "WIFI_EVENT_STA_START",
  "WIFI_EVENT_STA_STOP",
  "WIFI_EVENT_STA_CONNECTED",
  "WIFI_EVENT_STA_DISCONNECTED",
  "WIFI_EVENT_AP_START",
  "WIFI_EVENT_AP_STOP",
  0,0,0,0,
  "WIFI_EVENT_AP_START",
  "WIFI_EVENT_AP_STOP",
  "WIFI_EVENT_AP_STACONNECTED",
  "WIFI_EVENT_AP_STADISCONNECTED",
};

/* const char *soft_ap_ssid = "ESP32AP";      // accespoint ssid
 const char *soft_ap_password = "password"; // accespoint password
 const char *ssid = "";                     // WiFi SSID
 const char *password = "";                 // WiFi Password */

struct m_wifi_context wifi_context = {
    .Wifi_on = 0,
        .s_ap_connection = 0,
        .s_wifi_started = 0,
        .s_wifi_initialized = 0,
        .s_nvs_initialized = 0,
        .s_sta_connection = 0,
        .s_sta_connecting = 0,
        .s_sta_connected = 0,
        .s_sta_got_ip = 0,
        .s_sta_connect_not_found = 0, 
        .s_sta_connect_error = 0,
        .s_retry_num=10,
        .s_sta_num_connect = M_WIFI_STA_MAX + 1,
        .s_wifi_mode = WIFI_MODE_APSTA,
        .ap = {"ESP32AP", "password",{10, 10, 10, 1}, {255, 255, 255, 0}, {10, 10, 10, 1}},
        .stas = {
            {"", "", {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
            {"", "", {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
            {"", "", {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
            {"", "", {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
        },
        .hostname = {0}};

static esp_netif_t *s_sta_netif = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static esp_netif_t *s_ap_netif = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
esp_event_handler_instance_t instance_wifi_id = 0, instance_ip_id = 0;

#define MAX_STA_CONN (8)
#define ESP_MAXIMUM_RETRY (10)

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group = 0;

/* The event group allows multiple bits for each event, but we only care about
 * two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

// void network_reset() {
//     wifi_config_t current_conf;
//     esp_wifi_get_config((wifi_interface_t)ESP_IF_WIFI_STA, &current_conf);
//     memset(current_conf.sta.ssid, 0, sizeof(current_conf.sta.ssid));
//     memset(current_conf.sta.password, 0, sizeof(current_conf.sta.password));
//     esp_wifi_set_config((wifi_interface_t)ESP_IF_WIFI_STA, &current_conf);
// }

void uint8_to_hex_string(uint8_t value, char *hex_str) {
    ILOG(TAG, "[%s] %2x", __FUNCTION__, value);
    const char hex_chars[] = "0123456789ABCDEF";
    hex_str[0] = hex_chars[(value >> 4) & 0x0F]; // Extract high nibble
    hex_str[1] = hex_chars[value & 0x0F];        // Extract low nibble
    hex_str[2] = '\0';                           // Null-terminate the string
}

static void uint32_to_uint8_array(uint32_t value, uint8_t array[4]) {
    array[3] = (uint8_t)((value >> 24) & 0xFF);
    array[2] = (uint8_t)((value >> 16) & 0xFF);
    array[1] = (uint8_t)((value >> 8) & 0xFF);
    array[0] = (uint8_t)(value & 0xFF);
}

void mac_to_char(uint8_t *mac, char *mac_str, uint8_t start) {
    uint8_t i = start, j = 6;
    for (int i = start; i < j; i++) {
        uint8_to_hex_string(mac[i], &mac_str[(i-start) * 2]);
    }
    mac_str[(j-start) * 2] = 0;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    // ILOG(TAG, "[%s] base: %s event: %" PRId32 "\n", __FUNCTION__, event_base, event_id);
    if (event_base == WIFI_EVENT) {
        wifi_event_sta_connected_t *staconnevent;
        wifi_event_sta_disconnected_t *stadisconnevent;
        wifi_event_ap_staconnected_t *apstaconnevent;
        wifi_event_ap_stadisconnected_t *apstadisconnevent;
        switch (event_id) {
            case WIFI_EVENT_STA_START:  // 2
                ILOG(TAG, "[%s] %s.", __FUNCTION__, wifi_event_strings[event_id]);
                // xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                wifi_context.s_sta_connection = 1;
                wifi_sta_connect_scan();  // try station mode first
                break;
            case WIFI_EVENT_STA_STOP:  // 3
                ILOG(TAG, "[%s] %s.", __FUNCTION__, wifi_event_strings[event_id]);
                wifi_context.s_sta_connection = 0;
                if(s_wifi_event_group)
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                break;
            case WIFI_EVENT_STA_CONNECTED:  // 4
                staconnevent = (wifi_event_sta_connected_t *)event_data;
                ILOG(TAG, "[%s] %s. ssid:%s", __FUNCTION__, wifi_event_strings[event_id], staconnevent->ssid);
                wifi_context.s_sta_connected = 1;
                break;
            case WIFI_EVENT_STA_DISCONNECTED:  // 5
                stadisconnevent = (wifi_event_sta_disconnected_t *)event_data;
                ILOG(TAG, "[%s] %s. ssid:%s", __FUNCTION__, wifi_event_strings[event_id], stadisconnevent->ssid);
                if(s_wifi_event_group)
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                wifi_context.s_sta_connected = 0;
                if (wifi_context.s_retry_num && (wifi_context.s_wifi_mode == WIFI_MODE_STA || wifi_context.s_wifi_mode == WIFI_MODE_APSTA)) {
                    wifi_sta_connect_scan();
                    wifi_context.s_retry_num--;
                }
                break;
            case WIFI_EVENT_AP_START:
                ILOG(TAG, "[%s] %s.", __FUNCTION__, wifi_event_strings[event_id]);
                // sprintf(wifi_context.ip_address, IPSTR, IPIPSTR(wifi_context.ap.ipv4_address));
                wifi_context.s_ap_connection = 1;
                break;
            case WIFI_EVENT_AP_STOP:
                ILOG(TAG, "[%s] %s.", __FUNCTION__, wifi_event_strings[event_id]);
                wifi_context.s_ap_connection = 0;
                break;
            case WIFI_EVENT_AP_STACONNECTED:
                apstaconnevent = (wifi_event_ap_staconnected_t *)event_data;
                //memcpy(wifi_context.m_context->mac, apstaconnevent->mac, 6);
                ILOG(TAG, "[%s] %s. " MACSTR " join, AID=%d", __FUNCTION__, wifi_event_strings[event_id], MAC2STR(apstaconnevent->mac), apstaconnevent->aid);
                break;
            case WIFI_EVENT_AP_STADISCONNECTED:
                apstadisconnevent = (wifi_event_ap_stadisconnected_t *)event_data;
                ILOG(TAG, "[%s] %s. " MACSTR " leave, AID=%d", __FUNCTION__, wifi_event_strings[event_id], MAC2STR(apstadisconnevent->mac), apstadisconnevent->aid);
                break;
            case WIFI_EVENT_MAX:
                ILOG(TAG, "[%s]  WIFI_EVENT_MAX.", __FUNCTION__);
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        ip_event_got_ip_t *event;
        switch(event_id) {
            case IP_EVENT_STA_GOT_IP:
                event = (ip_event_got_ip_t *)event_data;
                ILOG(TAG, "[%s] IP_EVENT_STA_GOT_IP: " IPSTR, __FUNCTION__, IP2STR(&event->ip_info.ip));
                uint32_to_uint8_array(event->ip_info.ip.addr, wifi_context.stas[wifi_context.s_sta_num_connect].ipv4_address);
                uint32_to_uint8_array(event->ip_info.netmask.addr, wifi_context.stas[wifi_context.s_sta_num_connect].ipv4_netmask);
                uint32_to_uint8_array(event->ip_info.gw.addr, wifi_context.stas[wifi_context.s_sta_num_connect].ipv4_gw);
                wifi_context.s_sta_got_ip = 1;
                if(s_wifi_event_group)
                    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                break;
            case IP_EVENT_STA_LOST_IP:
                event = (ip_event_got_ip_t *)event_data;
                ILOG(TAG, "[%s] IP_EVENT_STA_LOST_IP:" IPSTR, __FUNCTION__, IP2STR(&event->ip_info.ip));
                memset(wifi_context.stas[wifi_context.s_sta_num_connect].ipv4_address,0, 4);
                memset(wifi_context.stas[wifi_context.s_sta_num_connect].ipv4_netmask,0, 4);
                memset(wifi_context.stas[wifi_context.s_sta_num_connect].ipv4_gw,0, 4);
                wifi_context.s_sta_got_ip = 0;
                if(s_wifi_event_group)
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                break;
            default:
                break;
        }
    } else if (event_id == WIFI_EVENT_STA_STOP) {
        ILOG(TAG, "[%s] %s ...", __FUNCTION__, wifi_event_strings[3]);
        if(s_wifi_event_group)
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        wifi_context.s_sta_connection = 0;
    } else if (event_id == WIFI_EVENT_STA_START) {
        ILOG(TAG, "[%s] %s.", __FUNCTION__, wifi_event_strings[event_id]);
        wifi_context.s_sta_connection = 1;
        wifi_sta_connect_scan();  // try station mode first
    } else if (event_id == WIFI_EVENT_AP_START) {
        ILOG(TAG, "[%s] %s ...", __FUNCTION__, wifi_event_strings[6]);
        // wifi_ap_start();
        wifi_context.s_ap_connection = 1;
    } else if (event_id == WIFI_EVENT_AP_STOP) {
        ILOG(TAG, "[%s] %s", __FUNCTION__, wifi_event_strings[7]);
        wifi_context.s_ap_connection = 0;
    }
}

esp_err_t wifi_disconnect() {
    ILOG(TAG, "[%s]", __FUNCTION__);
    return esp_wifi_disconnect();
}

int wifi_uninit() {
    ILOG(TAG, "[%s]", __FUNCTION__);
    esp_err_t err = ESP_OK;
    /* The event will not be processed after unregister */
    // wifi_context.s_sta_connecting = false;
    // wifi_context.s_sta_connected = false;
    // wifi_context.s_sta_got_ip = false;
    // wifi_context.s_sta_connect_error = false;
    // wifi_context.s_sta_connect_not_found = false;
    wifi_context.s_retry_num = 0;
    err = wifi_disconnect();
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "[%s] wifi_disconnect failed: %s", __FUNCTION__, esp_err_to_name(err));
    }
    err = esp_wifi_stop();
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "[%s] esp_wifi_stop failed: %s", __FUNCTION__, esp_err_to_name(err));
    }
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = 0;
    }
    
    err = uninitialize_sntp();
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "[%s] uninitialize_sntp failed: %s", __FUNCTION__, esp_err_to_name(err));
    }
    
    err = esp_wifi_deinit();
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "[%s] esp_wifi_deinit failed: %s", __FUNCTION__, esp_err_to_name(err));
    }
    
    esp_netif_deinit();
    
    err = esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_ip_id);
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "[%s] esp_event_handler_instance_unregister IP_EVENT failed: %s", __FUNCTION__, esp_err_to_name(err));
        }
    err = esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_wifi_id);
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "[%s] esp_event_handler_instance_unregister WIFI_EVENT failed: %s", __FUNCTION__, esp_err_to_name(err));
    }

    // esp_netif_destroy(s_sta_netif);
    // s_sta_netif = 0;
    // esp_netif_destroy(s_ap_netif);
    // s_ap_netif = 0;
    wifi_context.s_wifi_initialized = 0;
    return err;
}

int wifi_mode(uint8_t sta, uint8_t ap) {
    ILOG(TAG, "[%s]", __FUNCTION__);
    esp_err_t err = ESP_OK;
    wifi_mode_t current_mode = WIFI_MODE_NULL;
    wifi_context.s_retry_num = 10;
    if (wifi_context.s_wifi_initialized && wifi_context.s_wifi_started) {
        err = esp_wifi_get_mode(&current_mode);
        if (err != ERR_OK) {
            ESP_LOGW(TAG, "[%s] esp_wifi_get_mode failed: %s", __FUNCTION__, esp_err_to_name(err));
            return 0;
        }
    }
    ILOG(TAG, "[%s] mode (sta:%d, ap:%d, current:%d)", __FUNCTION__, sta, ap, current_mode);
    uint8_t current_sta = current_mode == WIFI_MODE_STA || current_mode == WIFI_MODE_APSTA;
    uint8_t current_ap = current_mode == WIFI_MODE_AP || current_mode == WIFI_MODE_APSTA;
    
    uint8_t set_sta = sta ? sta : 0;
    uint8_t set_ap = ap ? ap : 0;
    wifi_mode_t set_mode;
    if (set_sta && set_ap) {
        set_mode = WIFI_MODE_APSTA;
    } else if (set_sta && !set_ap) {
        set_mode = WIFI_MODE_STA;
    } else if (!set_sta && set_ap) {
        set_mode = WIFI_MODE_AP;
    } else {
        set_mode = WIFI_MODE_NULL;
    }
    
    ILOG(TAG, "[%s] set (sta: %d, ap: %d, next: %d)", __FUNCTION__, set_sta, set_ap, set_mode);
    
    if (current_mode == set_mode)
        return 1;
    
    if (set_sta && !current_sta) {
        ILOG(TAG, "[%s] Enabling STA.", __FUNCTION__);
    } else if (!set_sta && current_sta) {
        ILOG(TAG, "[%s] Disabling STA.", __FUNCTION__);
    }
    if (set_ap && !current_ap) {
        ILOG(TAG, "[%s] Enabling AP.", __FUNCTION__);
        wifi_ap_start();
    } else if (!set_ap && current_ap) {
        ILOG(TAG, "[%s] Disabling AP.", __FUNCTION__);
    }
    
    if (set_mode == WIFI_MODE_NULL && wifi_context.s_wifi_started) {
        err = esp_wifi_stop();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "[%s] esp_wifi_stop failed: %s", __FUNCTION__,
                     esp_err_to_name(err));
            return 0;
        }
        wifi_context.s_wifi_started = false;
        return 1;
    }
    
    wifi_context.s_wifi_mode = set_mode;
    err = esp_wifi_set_mode(set_mode);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "[%s] esp_wifi_set_mode failed: %s", __FUNCTION__, esp_err_to_name(err));
        wifi_context.s_wifi_mode = current_mode;
        return 0;
    }
    
    if (set_mode != WIFI_MODE_NULL && !wifi_context.s_wifi_started) {
        err = esp_wifi_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
            return 0;
        }
        wifi_context.s_wifi_started = true;
    }
    
    return 1;
}

static int nvs_init() {
    ILOG(TAG, "[%s]", __FUNCTION__);
    int ret = ESP_OK;
        ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ret = nvs_flash_erase();
        if (ret != ERR_OK) {
            ESP_LOGE(TAG, "esp_flash_erase failed: %s", esp_err_to_name(ret));
        }
        ret = nvs_flash_init();
        if (ret != ERR_OK) {
            ESP_LOGE(TAG, "esp_flash_init failed: %s", esp_err_to_name(ret));
        }
        wifi_context.s_nvs_initialized = true;
    } else if (ret != ERR_OK) {
        ESP_LOGE(TAG, "esp_flash_init failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

void wifi_init() {
    ILOG(TAG, "[%s]", __FUNCTION__);
    if(!wifi_context.s_nvs_initialized)
        nvs_init();

    ILOG(TAG, "[%s] init netif...", __FUNCTION__);
    esp_err_t err = esp_netif_init();
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        goto end;
    }
    if(!s_wifi_event_group) {
        ILOG(TAG, "[%s] create event group...", __FUNCTION__);
        s_wifi_event_group = xEventGroupCreate();
        if (s_wifi_event_group == 0) {
            ESP_LOGE(TAG, "xEventGroupCreate failed");
            goto end;
        }
    }
    
    ILOG(TAG, "[%s] register event handlers...", __FUNCTION__);
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, 0, &instance_wifi_id);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "esp_event_handler_instance_register failed: %s",
                 esp_err_to_name(err));
        goto end;
    }
    err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, 0, &instance_ip_id);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "esp_event_handler_instance_register failed: %s", esp_err_to_name(err));
        goto end;
    }
    
    ILOG(TAG, "[%s] create default sta and ap...", __FUNCTION__);
    if(!s_sta_netif)
        s_sta_netif = esp_netif_create_default_wifi_sta();
    if(!s_ap_netif)
        s_ap_netif = esp_netif_create_default_wifi_ap();
    
    esp_netif_ip_info_t ipInfo;
    const struct cfg_item * item = &wifi_context.ap;
    IP4_ADDR(&ipInfo.ip, item->ipv4_address[0], item->ipv4_address[1], item->ipv4_address[2], item->ipv4_address[3]);
    IP4_ADDR(&ipInfo.gw, item->ipv4_gw[0], item->ipv4_gw[1], item->ipv4_gw[2], item->ipv4_gw[3]);
    IP4_ADDR(&ipInfo.netmask, item->ipv4_netmask[0], item->ipv4_netmask[1], item->ipv4_netmask[2], item->ipv4_netmask[3]);
    esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_set_ip_info(s_ap_netif, &ipInfo);
    esp_netif_dhcps_start(s_ap_netif);
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    
    ILOG(TAG, "[%s] esp_wifi_init\n", __FUNCTION__);
    err = esp_wifi_init(&cfg);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        goto end;
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_storage failed: %s", esp_err_to_name(err));
        goto end;
    }
    wifi_context.s_wifi_initialized = true;
    end:
}

int wifi_ap_start() {
    ILOG(TAG, "[%s]", __FUNCTION__);
    esp_err_t err = ESP_OK;
    wifi_config_t conf;
    memset(&conf, 0, sizeof(conf));
    if(strcmp(&wifi_context.ap.ssid[0], "ESP32AP") == 0){
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char mac_str[8]={0};
        mac_to_char(mac, mac_str, 4);
        snprintf(&wifi_context.ap.ssid[0], 32, "ESP%s", mac_str);
    }
    conf.ap.channel = 1;
    strncpy((char *)conf.ap.ssid, &wifi_context.ap.ssid[0], 32);
    conf.ap.max_connection = 5;
    conf.ap.beacon_interval = 100;
    conf.ap.ssid_len = strlen(wifi_context.ap.ssid);
    
    if (!*wifi_context.ap.password) {
        conf.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        conf.ap.authmode = WIFI_AUTH_WPA2_PSK;
        strncpy((char *)conf.ap.password, &wifi_context.ap.password[0], 64);
    }
    
#if ESP_IDF_VERSION_MAJOR >= 4
    // pairwise cipher of SoftAP, group cipher will be derived using this.
    conf.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
#endif
    
    err = esp_wifi_set_config(WIFI_IF_AP, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config  WIFI_IF_AP failed: %s", esp_err_to_name(err));
        goto end;
    }
    
    /*if (!this->wifi_ap_ip_config_(ap.get_manual_ip())) {
     ESP_LOGV(TAG, "wifi_ap_ip_config_ failed!");
     return false;
     }
     
     return true;*/
    end:
    return err;
}

int wifi_sta_connect(uint16_t slot) {
    ILOG(TAG, "[%s] slot: %d", __FUNCTION__, slot);
    esp_err_t err = ESP_OK;
    if (slot >= M_WIFI_STA_MAX || !wifi_context.stas[slot].ssid[slot])
        goto end;
    // if (!wifi_mode(1, 0))
    //     goto end;
    wifi_config_t conf;
    memset(&conf, 0, sizeof(conf));
    strncpy((char *)conf.sta.ssid, wifi_context.stas[slot].ssid, 31);
    conf.sta.listen_interval = 0;
    /* #if ESP_IDF_VERSION_MAJOR >= 4
     // Protected Management Frame
     // Device will prefer to connect in PMF mode if other device also advertises
     // PMF capability.
     conf.sta.pmf_cfg.capable = true;
     conf.sta.pmf_cfg.required = false;
     #endif */
    // note, we do our own filtering
    // The minimum rssi to accept in the fast scan mode
    conf.sta.threshold.rssi = -127;
    if (!*wifi_context.stas[slot].password) {
        conf.sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else {
        conf.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
        strncpy((char *)conf.sta.password, wifi_context.stas[slot].password,
                63);
    }
    
    wifi_config_t current_conf;
     err = esp_wifi_get_config(WIFI_IF_STA, &current_conf);
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "esp_wifi_get_config failed: %s", esp_err_to_name(err));
    }
    
    // reset cred...
    current_conf.sta.ssid[0] = 0;
    current_conf.sta.password[0] = 0;
    if (memcmp(&current_conf, &conf, sizeof(wifi_config_t)) != 0) {
        err = esp_wifi_disconnect();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_disconnect failed: %s", esp_err_to_name(err));
            goto end;
        }
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config WIFI_IF_STA failed: %s", esp_err_to_name(err));
        goto end;
    }
    wifi_context.s_sta_num_connect = slot;
    wifi_context.s_sta_connecting = true;
    wifi_context.s_sta_connected = false;
    wifi_context.s_sta_got_ip = false;
    wifi_context.s_sta_connect_error = false;
    wifi_context.s_sta_connect_not_found = false;
    
    ILOG(TAG, "[%s] esp_wifi_connect %s", __FUNCTION__, conf.sta.ssid);
    initialize_sntp(wifi_context.offset);
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        goto end;
    }
    // ILOG(TAG, "wifi_init_sta finished.");
    esp_netif_sntp_start();
    end:
    return err;
}

int wifi_status() {
    // ILOG(TAG, "[%s]", __FUNCTION__);
    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT)
     * or connection failed for the maximum number of re-tries (WIFI_FAIL_BIT).
     * The bits are set by event_handler() (see above) */
    if (!wifi_context.s_wifi_initialized) {
        return -2;
    }
    if (wifi_context.s_ap_connection == 1)
        return 2;
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, 1000 / portTICK_PERIOD_MS);
    
    /* xEventGroupWaitBits() returns the bits before the call returned, hence we
     * can test which event actually happened. */
    
    if (bits & WIFI_CONNECTED_BIT) {
        // ILOG(TAG, "[%s] WIFI_CONNECTED_BIT %d\n", __FUNCTION__, wifi_context.s_ap_connection);
        // ILOG(TAG, "connected to ap SSID:%s password:%s", ssid, password);
        if (wifi_context.s_ap_connection == 1)
            return 2;
        return 1;
    } else if (bits & WIFI_FAIL_BIT) {
        // ILOG(TAG, "[%s] WIFI_FAIL_BIT\n", __FUNCTION__);
        //  ILOG(TAG, "Failed to connect to SSID:%s, password:%s", ssid, password);
        return 0;
    } else {
        // ILOG(TAG, "[%s] UNEXPECTED EVENT\n", __FUNCTION__);
        //  ESP_LOGE(TAG, "UNEXPECTED EVENT");
        return -1;
    }
}

#define SCAN_LIST_SIZE 10
static uint8_t scan_progress = 0;
int wifi_sta_connect_scan() {
    ILOG(TAG, "[%s]", __FUNCTION__);
    if(scan_progress)
        return 0;
    scan_progress = 1;
    int err = 0;
    uint16_t number = SCAN_LIST_SIZE;
    wifi_ap_record_t ap_info[SCAN_LIST_SIZE] = {0};
    uint16_t ap_count = 0;
    memset(ap_info, 0, sizeof(ap_info));
    err = esp_wifi_scan_start(NULL, true);
    err = esp_wifi_scan_get_ap_num(&ap_count);
    err = esp_wifi_scan_get_ap_records(&number, ap_info);
    ILOG(TAG, "[%s] ap_count: %d", __FUNCTION__, ap_count);
    uint8_t k = M_WIFI_STA_MAX + 1;
    if (ap_count > 0) {
        uint16_t i, j;
        for (i = 0; (i < SCAN_LIST_SIZE) && (i < ap_count); ++i) {
            ILOG(TAG, "* %s\t\t%d", ap_info[i].ssid, ap_info[i].rssi);
            for (j = 0; j < M_WIFI_STA_MAX; ++j) {
                if (!wifi_context.stas[j].ssid[0])
                    continue;
                if (!strcmp((char*)&(ap_info[i].ssid[0]), &(wifi_context.stas[j].ssid[0]))) {
                    ILOG(TAG, "[%s] found %s", __FUNCTION__, wifi_context.stas[j].ssid);
                    k = j;
                    goto found;
                }
            }
        }
    }
    found:
    if (k < M_WIFI_STA_MAX)
        err = wifi_sta_connect(k);
    else if (ap_count > M_WIFI_STA_MAX - 1 || !ap_count)
        err = wifi_sta_connect(0);
    scan_progress = 0;
    return err;
}

int wifi_set_config(const char *ap_ssid, const char *ap_password,
                    const char *sta_ssid, const char *sta_password) {
    ILOG(TAG, "[%s]", __FUNCTION__);
    strcpy(wifi_context.ap.ssid, ap_ssid);
    strcpy(wifi_context.ap.password, ap_password);
    strcpy(wifi_context.stas[0].ssid, sta_ssid);
    strcpy(wifi_context.stas[0].password, sta_password);
    return 0;
}

int wifi_sta_set_config(int num, const char *sta_ssid, const char *sta_password) {
    ILOG(TAG, "[%s]", __FUNCTION__);
    if(num>=M_WIFI_STA_MAX)
        return -1;
    strcpy(wifi_context.stas[num].ssid, sta_ssid);
    strcpy(wifi_context.stas[num].password, sta_password);
    return 0;
}

int wifi_ap_set_config(const char *ap_ssid, const char *ap_password) {
    ILOG(TAG, "[%s]", __FUNCTION__);
    strcpy(wifi_context.ap.ssid, ap_ssid);
    strcpy(wifi_context.ap.password, ap_password);
    return 0;
}
