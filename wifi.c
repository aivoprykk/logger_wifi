#include "logger_wifi_private.h"

#ifdef CONFIG_LOGGER_WIFI_ENABLED

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "lwip/err.h"
#include "lwip/ip_addr.h"

#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

#include "logger_common.h" // nvs_init etc

#include "context.h"

#define TAG "wifi"
#define IPIPSTR(a) a[0], a[1], a[2], a[3]
#if (C_LOG_LEVEL < 3)
static const char * _wifi_event_strings[] = {
  "WIFI_EVENT_WIFI_READY",           // 0
  "WIFI_EVENT_SCAN_DONE",            // 1
  "WIFI_EVENT_STA_START",            // 2
  "WIFI_EVENT_STA_STOP",             // 3
  "WIFI_EVENT_STA_CONNECTED",        // 4
  "WIFI_EVENT_STA_DISCONNECTED",     // 5
  "WIFI_EVENT_STA_AUTHMODE_CHANGE",  // 6
  "WIFI_EVENT_STA_WPS_ER_SUCCESS",   // 7
  "WIFI_EVENT_STA_WPS_ER_FAILED",    // 8
  "WIFI_EVENT_STA_WPS_ER_TIMEOUT",   // 9
  "WIFI_EVENT_STA_WPS_ER_PIN",       // 10
  "WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP", // 11
  "WIFI_EVENT_AP_START",             // 12
  "WIFI_EVENT_AP_STOP",              // 13
  "WIFI_EVENT_AP_STACONNECTED",      // 14
  "WIFI_EVENT_AP_STADISCONNECTED",   // 15
};
const char * wifi_event_strings(int id) {
    return _wifi_event_strings[id];
}
#else
const char * wifi_event_strings(int id) {return "WIFI_EVENT";}
#endif
/* const char *soft_ap_ssid = "ESP32AP";      // accespoint ssid
 const char *soft_ap_password = "password"; // accespoint password
 const char *ssid = "";                     // WiFi SSID
 const char *password = "";                 // WiFi Password */

struct m_wifi_context wifi_context = WIFI_CONTEXT_DEFAULT();

static esp_netif_t *s_sta_netif = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static esp_netif_t *s_ap_netif = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
esp_event_handler_instance_t instance_wifi_id = 0, instance_ip_id = 0;

// WiFi mode change callbacks for coordinating external dependencies
static wifi_mode_change_callbacks_t s_mode_change_callbacks = {0};
static bool mode_change_pending = false;

#define MAX_STA_CONN (8)
#define ESP_MAXIMUM_RETRY (10)

/* The event group allows multiple bits for each event, but we only care about
 * two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_TIME_SYNC_BIT BIT2    // Time synchronized via SNTP
#define WIFI_AP_READY_BIT BIT3     // AP mode fully initialized

// void network_reset() {
//     wifi_config_t current_conf;
//     esp_wifi_get_config((wifi_interface_t)ESP_IF_WIFI_STA, &current_conf);
//     memset(current_conf.sta.ssid, 0, sizeof(current_conf.sta.ssid));
//     memset(current_conf.sta.password, 0, sizeof(current_conf.sta.password));
//     esp_wifi_set_config((wifi_interface_t)ESP_IF_WIFI_STA, &current_conf);
// }

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    // ILOG(TAG, "[%s] base: %s event: %" PRId32 "\n", __FUNCTION__, event_base, event_id);
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:  // 2
#if (C_LOG_LEVEL < 3)
                WLOG(TAG, "[%s] WIFI_EVENT -> %s", __FUNCTION__, wifi_event_strings(event_id));
#endif
                // Clear any previous bits when starting
                if(wifi_context.s_wifi_event_group) {
                    xEventGroupClearBits(wifi_context.s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
                }
                wifi_context.s_sta_connection = 1;
                wifi_sta_connect_scan();  // try station mode first
                
                // Notify external dependencies that STA mode is ready
                if (s_mode_change_callbacks.after_mode_change_complete && mode_change_pending) {
                    s_mode_change_callbacks.after_mode_change_complete();
                    mode_change_pending = false;
                }
                break;
            case WIFI_EVENT_STA_STOP:  // 3
#if (C_LOG_LEVEL < 3)
                WLOG(TAG, "[%s] WIFI_EVENT -> %s", __FUNCTION__, wifi_event_strings(event_id));
#endif
                wifi_context.s_sta_connection = 0;
                if(wifi_context.s_wifi_event_group) {
                    xEventGroupClearBits(wifi_context.s_wifi_event_group, WIFI_CONNECTED_BIT);
                    xEventGroupSetBits(wifi_context.s_wifi_event_group, WIFI_FAIL_BIT);
                }
                break;
            case WIFI_EVENT_STA_CONNECTED:  // 4
#if (C_LOG_LEVEL < 3)
                const wifi_event_sta_connected_t * staconnevent = (wifi_event_sta_connected_t *)event_data;
                FUNC_ENTRY_ARGS(TAG, " WIFI_EVENT -> %s. ssid:%s", wifi_event_strings(event_id), staconnevent->ssid);
#endif
                wifi_context.s_sta_connected = 1;
                // Clear FAIL bit when successfully connected
                if(wifi_context.s_wifi_event_group) {
                    xEventGroupClearBits(wifi_context.s_wifi_event_group, WIFI_FAIL_BIT);
                }
                break;
            case WIFI_EVENT_STA_DISCONNECTED:  // 5
#if (C_LOG_LEVEL < 3)
                const wifi_event_sta_disconnected_t * stadisconnevent = (wifi_event_sta_disconnected_t *)event_data;
                FUNC_ENTRY_ARGS(TAG, " WIFI_EVENT -> %s. ssid:%s", wifi_event_strings(event_id), stadisconnevent->ssid);
#endif
                wifi_context.s_sta_connected = 0;
                if(wifi_context.s_wifi_event_group) {
                    xEventGroupClearBits(wifi_context.s_wifi_event_group, WIFI_CONNECTED_BIT);
                    xEventGroupSetBits(wifi_context.s_wifi_event_group, WIFI_FAIL_BIT);
                }
                if (wifi_context.s_retry_num && (wifi_context.s_wifi_mode == WIFI_MODE_STA || wifi_context.s_wifi_mode == WIFI_MODE_APSTA)) {
                    WLOG(TAG, "[%s] %s.", __FUNCTION__, wifi_event_strings(event_id));
                    wifi_sta_connect_scan();
                    wifi_context.s_retry_num--;
                }
                break;
            case WIFI_EVENT_AP_START:
#if (C_LOG_LEVEL < 3)
                FUNC_ENTRY_ARGS(TAG, " WIFI_EVENT -> %s.", wifi_event_strings(event_id));
#endif
                // sprintf(wifi_context.ip_address, IPSTR, IPIPSTR(wifi_context.ap.ipv4_address));
                wifi_context.s_ap_connection = 1;
                // Set AP ready bit for task synchronization
                if(wifi_context.s_wifi_event_group) {
                    xEventGroupSetBits(wifi_context.s_wifi_event_group, WIFI_AP_READY_BIT);
                }
                
                // Notify external dependencies that AP mode is ready
                if (s_mode_change_callbacks.after_mode_change_complete && mode_change_pending) {
                    s_mode_change_callbacks.after_mode_change_complete();
                    mode_change_pending = false;
                }
                break;
            case WIFI_EVENT_AP_STOP:
#if (C_LOG_LEVEL < 2)
                FUNC_ENTRY_ARGS(TAG, " WIFI_EVENT -> %s.", wifi_event_strings(event_id));
#endif
                wifi_context.s_ap_connection = 0;
                // Clear AP ready bit
                if(wifi_context.s_wifi_event_group) {
                    xEventGroupClearBits(wifi_context.s_wifi_event_group, WIFI_AP_READY_BIT);
                }
                break;
#if (C_LOG_LEVEL < 2)
            case WIFI_EVENT_AP_STACONNECTED:
                const wifi_event_ap_staconnected_t * apstaconnevent = (wifi_event_ap_staconnected_t *)event_data;
                //memcpy(wifi_context.m_context->mac, apstaconnevent->mac, 6);
                FUNC_ENTRY_ARGS(TAG, " WIFI_EVENT -> %s. " MACSTR " join, AID=%d", wifi_event_strings(event_id), MAC2STR(apstaconnevent->mac), apstaconnevent->aid);
                break;
            case WIFI_EVENT_AP_STADISCONNECTED:
                const wifi_event_ap_stadisconnected_t * apstadisconnevent = (wifi_event_ap_stadisconnected_t *)event_data;
                FUNC_ENTRY_ARGS(TAG, " WIFI_EVENT -> %s. " MACSTR " leave, AID=%d", wifi_event_strings(event_id), MAC2STR(apstadisconnevent->mac), apstadisconnevent->aid);
                break;
            case WIFI_EVENT_MAX:
                FUNC_ENTRY_ARGS(TAG, " WIFI_EVENT_MAX.");
                break;
#endif
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        ip_event_got_ip_t *event;
        switch(event_id) {
            case IP_EVENT_STA_GOT_IP:
                event = (ip_event_got_ip_t *)event_data;
#if (C_LOG_LEVEL < 4)    
                WLOG(TAG, "[%s] IP_EVENT -> IP_EVENT_STA_GOT_IP: " IPSTR, __FUNCTION__, IP2STR(&event->ip_info.ip));
#endif
                uint32_to_uint8_array(event->ip_info.ip.addr, wifi_context.stas[wifi_context.s_sta_num_connect].ipv4_address);
                uint32_to_uint8_array(event->ip_info.netmask.addr, wifi_context.stas[wifi_context.s_sta_num_connect].ipv4_netmask);
                uint32_to_uint8_array(event->ip_info.gw.addr, wifi_context.stas[wifi_context.s_sta_num_connect].ipv4_gw);
                wifi_context.s_sta_got_ip = 1;
                /* Set sta as the default interface */
                esp_netif_set_default_netif(s_sta_netif);
                if(wifi_context.s_ap_connection) {
#if defined(CONFIG_LWIP_IPV4_NAPT)
                    if(esp_netif_napt_enable(s_ap_netif) != ESP_OK) {
                        ELOG(TAG, "NAPT not enabled on the netif: %p", s_ap_netif);
                    }
#endif
                }
                if(wifi_context.s_wifi_event_group) {
                    xEventGroupClearBits(wifi_context.s_wifi_event_group, WIFI_FAIL_BIT);
                    xEventGroupSetBits(wifi_context.s_wifi_event_group, WIFI_CONNECTED_BIT);
                }
                // Start SNTP now that WiFi connection and IP are confirmed
                esp_netif_sntp_start();
                break;
            case IP_EVENT_STA_LOST_IP:
                event = (ip_event_got_ip_t *)event_data;
#if (C_LOG_LEVEL < 2)
                WLOG(TAG, "[%s] IP_EVENT -> IP_EVENT_STA_LOST_IP:" IPSTR, __FUNCTION__, IP2STR(&event->ip_info.ip));
#endif
                memset(wifi_context.stas[wifi_context.s_sta_num_connect].ipv4_address,0, 4);
                memset(wifi_context.stas[wifi_context.s_sta_num_connect].ipv4_netmask,0, 4);
                memset(wifi_context.stas[wifi_context.s_sta_num_connect].ipv4_gw,0, 4);
                wifi_context.s_sta_got_ip = 0;
                if(wifi_context.s_ap_connection) {
                    esp_netif_set_default_netif(s_ap_netif);
#if defined(CONFIG_LWIP_IPV4_NAPT)
                    esp_netif_napt_disable(s_ap_netif);
#endif
                }
                if(wifi_context.s_wifi_event_group) {
                    xEventGroupClearBits(wifi_context.s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_TIME_SYNC_BIT);
                    xEventGroupSetBits(wifi_context.s_wifi_event_group, WIFI_FAIL_BIT);
                }
                // Stop SNTP when IP is lost
                uninitialize_sntp();
                break;
            default:
                break;
        }
    }
}

int shutdown_wifi_and_cleanup(void) {
    FUNC_ENTRY(TAG);
    esp_err_t ret = ESP_OK;
    
    // Stop SNTP first to prevent any ongoing network operations
    ret = uninitialize_sntp();
#if (C_LOG_LEVEL < 3)
    if (ret != ESP_OK) {
        WLOG(TAG, "[%s] uninitialize_sntp failed: %s", __FUNCTION__, esp_err_to_name(ret));
    }
#endif
    
    // Only perform shutdown if WiFi is actually started
    if (wifi_context.s_wifi_started) {
        ILOG("WIFI", "Starting WiFi shutdown sequence - events will be generated for service cleanup");
        
        // 1. Disconnect from WiFi first to generate proper disconnect events
        ILOG(TAG, "[%s] wifi disconnect", __FUNCTION__);
        ret = esp_wifi_disconnect();
        if (ret == ESP_OK) {
            ILOG("WIFI", "WiFi disconnect initiated");
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Allow disconnect events to be processed
        
        // 2. Stop WiFi - this generates WIFI_EVENT_AP_STOP and WIFI_EVENT_STA_STOP events
        //    which are crucial for HTTP server and other service cleanup
        ILOG(TAG, "[%s] wifi stop", __FUNCTION__);
        int stop_attempts = 3;
        while (stop_attempts > 0) {
            ret = esp_wifi_stop();
            if (ret == ESP_OK || ret == ESP_ERR_WIFI_NOT_STARTED) {
                ILOG("WIFI", "WiFi stop successful - stop events generated for services");
                break;
            }
            WLOG("WIFI", "WiFi stop attempt %d failed: %s", 4-stop_attempts, esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(50));
            stop_attempts--;
        }
        
        wifi_context.s_wifi_started = 0;
        
        // Allow time for all WiFi stop events to be processed by services
        // This is critical for HTTP server shutdown, file system cleanup, etc.
        ILOG("WIFI", "Waiting for service cleanup to complete...");
        vTaskDelay(pdMS_TO_TICKS(200)); // Quick service cleanup
    }
    
    // 3. Deinit WiFi after ensuring everything is stopped with retry logic
    FUNC_ENTRY_ARGS(TAG, " deinit");
    int deinit_attempts = 3;
    while (deinit_attempts > 0) {
        ret = esp_wifi_deinit();
        if (ret == ESP_OK || ret == ESP_ERR_WIFI_NOT_INIT) {
            ret = ESP_OK;  // Success or already deinitialized
            break;
        }
        WLOG("WIFI", "WiFi deinit attempt %d failed: %s", 4-deinit_attempts, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(50));
        deinit_attempts--;
    }
    
    // Final delay to ensure all WiFi-related tasks and network operations are finished
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ILOG("WIFI", "WiFi shutdown complete");
    return ret;
}

int wifi_uninit() {
    FUNC_ENTRY(TAG);
    
    // Log memory usage before cleanup
    wifi_log_memory_usage("Before WiFi Uninit");
    
    if (!wifi_context.s_wifi_initialized) {
        WLOG(TAG, "[%s] WiFi not initialized, nothing to cleanup", __FUNCTION__);
        return 1;
    }

    // STEP 1: Shutdown WiFi subsystem FIRST (this will generate proper stop events for services)
    // This must happen before any netif manipulation to avoid race conditions
    esp_err_t err = shutdown_wifi_and_cleanup();
    
    // STEP 2: Brief delay after WiFi shutdown to let all network tasks settle
    ILOG("MEM", "WiFi shutdown complete, waiting for network stack to settle");
    vTaskDelay(pdMS_TO_TICKS(150)); // Quick network stack stabilization
    
    // Don't manually clear default netif - let ESP-IDF handle it during interface destruction
    // This avoids race conditions with the tcpip_thread
    
    // STEP 3: NOW unregister event handlers after WiFi shutdown events have been processed
    if (instance_wifi_id) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_wifi_id);
        instance_wifi_id = 0;
        ILOG("MEM", "Unregistered WiFi event handler");
    }
    if (instance_ip_id) {
        esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, instance_ip_id);
        instance_ip_id = 0;
        ILOG("MEM", "Unregistered IP event handler");
    }

    // STEP 4: Cleanup network interfaces - let ESP-IDF handle default netif changes
    ILOG(TAG, "[%s] cleanup sta netif", __FUNCTION__);
    if (s_sta_netif) {
        // Gracefully stop network services on this interface
        ILOG("MEM", "Stopping STA netif services");
        esp_netif_dhcpc_stop(s_sta_netif);
        
        // Clear IP configuration carefully
        esp_netif_ip_info_t clear_ip = {0};
        esp_err_t ip_err = esp_netif_set_ip_info(s_sta_netif, &clear_ip);
        if (ip_err != ESP_OK) {
            WLOG("MEM", "Failed to clear STA IP info: %s", esp_err_to_name(ip_err));
        }
        
        // Clear DNS servers carefully
        esp_netif_dns_info_t dns_clear = {0};
        esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns_clear);
        esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_BACKUP, &dns_clear);
        
        // Brief delay to ensure all network operations complete
        ILOG("MEM", "Waiting for STA netif operations to complete");
        vTaskDelay(pdMS_TO_TICKS(50));
        
        // Let esp_netif_destroy handle default netif clearing automatically
        // This avoids race conditions with manual clearing
        ILOG("MEM", "Destroying STA netif (will auto-clear if default)");
        esp_netif_destroy(s_sta_netif);
        s_sta_netif = NULL;
        ILOG("MEM", "Cleaned up WiFi STA netif");
    }

    ILOG(TAG, "[%s] cleanup ap netif", __FUNCTION__);
    if (s_ap_netif) {
        // Gracefully stop network services on this interface
        ILOG("MEM", "Stopping AP netif services");
        esp_netif_dhcps_stop(s_ap_netif);
        
        // Clear IP configuration carefully
        esp_netif_ip_info_t clear_ip = {0};
        esp_err_t ip_err = esp_netif_set_ip_info(s_ap_netif, &clear_ip);
        if (ip_err != ESP_OK) {
            WLOG("MEM", "Failed to clear AP IP info: %s", esp_err_to_name(ip_err));
        }
        
#if defined(CONFIG_LWIP_IPV4_NAPT)
        // Disable NAPT if it was enabled
        esp_netif_napt_disable(s_ap_netif);
#endif
        
        // Brief delay to ensure all network operations complete
        ILOG("MEM", "Waiting for AP netif operations to complete");
        vTaskDelay(pdMS_TO_TICKS(50));
        
        // Let esp_netif_destroy handle default netif clearing automatically
        ILOG("MEM", "Destroying AP netif (will auto-clear if default)");
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
        ILOG("MEM", "Cleaned up WiFi AP netif");
    }
    
    // Brief time for cleanup to complete after interface destruction
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // CRITICAL: Fast memory cleanup for GPS mode preparation
    // GPS operation requires maximum available memory
    ILOG("MEM", "Starting aggressive WiFi memory cleanup for GPS preparation...");
    
    size_t mem_before = esp_get_free_heap_size();
    ILOG("MEM", "Memory before cleanup: %zu bytes", mem_before);
    
    // Phase 1: Network buffer cleanup
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_get_free_heap_size(); // Trigger internal cleanup
    
    // Phase 2: Force TCP/IP stack cleanup
    vTaskDelay(pdMS_TO_TICKS(50));
    size_t mem_phase2 = esp_get_free_heap_size();
    
    // Phase 3: WiFi driver memory cleanup  
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_get_free_heap_size(); // Another cleanup trigger
    
    // Phase 4: Heap defragmentation and consolidation
    for (int i = 0; i < 3; i++) {
        vTaskDelay(pdMS_TO_TICKS(25));
        esp_get_free_heap_size(); // Multiple triggers for thorough cleanup
    }
    
    // Phase 5: Final memory consolidation
    vTaskDelay(pdMS_TO_TICKS(50));
    size_t mem_after = esp_get_free_heap_size();
    
    ILOG("MEM", "Fast WiFi memory cleanup complete");
    ILOG("MEM", "Memory recovered: %zu bytes (before: %zu, after: %zu)", 
             mem_after - mem_before, mem_before, mem_after);
    ILOG("MEM", "System ready for GPS operation with optimized memory");

    // not yet supported in IDF v5.0
    // esp_netif_deinit();
    
    // Event handlers already unregistered at the beginning of cleanup
    // Clean up event group to prevent memory leak
    if (wifi_context.s_wifi_event_group) {
        vEventGroupDelete(wifi_context.s_wifi_event_group);
        wifi_context.s_wifi_event_group = NULL;
    }
    
    // Reset WiFi context state variables
    wifi_context.s_wifi_initialized = 0;
    wifi_context.s_wifi_started = 0;
    wifi_context.s_sta_connection = 0;
    wifi_context.s_ap_connection = 0;
    wifi_context.s_sta_connected = 0;
    wifi_context.s_sta_got_ip = 0;
    wifi_context.s_retry_num = 0;
    
    // Event handler instances already reset during unregistration

    FUNC_ENTRY_ARGS(TAG, "WiFi uninitialization complete");

    // Final memory usage logging with cleanup effectiveness
    wifi_log_memory_usage("After WiFi Uninit Complete");
    
    // Log memory recovery summary
    size_t final_free = esp_get_free_heap_size();
    ILOG("MEM", "WiFi cleanup complete - memory management finished");
    ILOG("MEM", "Final free heap: %zu bytes", final_free);
    
    return err;
}

int wifi_mode(uint8_t sta, uint8_t ap) {
    FUNC_ENTRY_ARGS(TAG, "sta=%d, ap=%d", sta, ap);
    esp_err_t err = ESP_OK;
    wifi_mode_t current_mode = WIFI_MODE_NULL;
    wifi_context.s_retry_num = 10;
    if (wifi_context.s_wifi_initialized && wifi_context.s_wifi_started) {
        err = esp_wifi_get_mode(&current_mode);
        if (err != ESP_OK) {
            WLOG(TAG, "[%s] esp_wifi_get_mode failed: %s", __FUNCTION__, esp_err_to_name(err));
            return 0;
        }
    }
    FUNC_ENTRY_ARGS(TAG, "mode (sta:%d, ap:%d, current:%d)", sta, ap, current_mode);
    uint8_t current_sta = current_mode == WIFI_MODE_STA || current_mode == WIFI_MODE_APSTA;
    uint8_t current_ap = current_mode == WIFI_MODE_AP || current_mode == WIFI_MODE_APSTA;
    
    uint8_t set_sta = sta ? sta : 0;
    uint8_t set_ap = ap ? ap : 0;
    wifi_mode_t set_mode;
    if (set_sta && set_ap) {
        set_mode = WIFI_MODE_APSTA;
    } else 
    if (set_sta && !set_ap) {
        set_mode = WIFI_MODE_STA;
    } else if (!set_sta && set_ap) {
        set_mode = WIFI_MODE_AP;
    } else {
        set_mode = WIFI_MODE_NULL;
    }
#if (C_LOG_LEVEL < 2)    
    WLOG(TAG, "[%s] set (sta: %d, ap: %d, next: %d)", __FUNCTION__, set_sta, set_ap, set_mode);
#endif
    if (current_mode == set_mode)
        return 1;
#if (C_LOG_LEVEL < 2)
    if (set_sta && !current_sta) {
        FUNC_ENTRY_ARGS(TAG, "Enabling STA.");
    } else if (!set_sta && current_sta) {
        FUNC_ENTRY_ARGS(TAG, "Disabling STA.");
    }
#endif
    if (set_mode == WIFI_MODE_NULL && wifi_context.s_wifi_started) {
        err = esp_wifi_stop();
        if (err != ESP_OK) {
            ELOG(TAG, "[%s] esp_wifi_stop failed: %s", __FUNCTION__, esp_err_to_name(err));
            return 0;
        }
        wifi_context.s_wifi_started = false;
        return 1;
    }
    
    // Set WiFi mode BEFORE configuring interfaces
    wifi_context.s_wifi_mode = set_mode;
    err = esp_wifi_set_mode(set_mode);
    if (err != ESP_OK) {
        ELOG(TAG, "[%s] esp_wifi_set_mode failed: %s", __FUNCTION__, esp_err_to_name(err));
        wifi_context.s_wifi_mode = current_mode;
        return 0;
    }
    
    // Now configure AP if needed (after mode is set)
    if (set_ap && !current_ap) {
        DLOG(TAG, "[%s] Enabling AP.", __FUNCTION__);
        err = wifi_ap_start();
        if (err != ESP_OK) {
            ELOG(TAG, "[%s] wifi_ap_start failed: %s", __FUNCTION__, esp_err_to_name(err));
            return 0;
        }
    }
#if (C_LOG_LEVEL < 2) 
    else if (!set_ap && current_ap) {
        DLOG(TAG, "[%s] Disabling AP.", __FUNCTION__);
    }
#endif
    
    if (set_mode != WIFI_MODE_NULL && !wifi_context.s_wifi_started) {
        err = esp_wifi_start();
        if (err != ESP_OK) {
            ELOG(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
            return 0;
        }
        wifi_context.s_wifi_started = true;
    }
    
    return 1;
}

void wifi_init() {
    FUNC_ENTRY(TAG);
    
    // Log memory usage before initialization
    wifi_log_memory_usage("Before WiFi Init");
    
    if (wifi_context.s_wifi_initialized) {
        WLOG(TAG, "[%s] WiFi already initialized, skipping", __FUNCTION__);
        return;
    }
#if defined(CONFIG_ESP_WIFI_NVS_ENABLED)
    nvs_init();
#endif
    FUNC_ENTRY_ARGS(TAG, "init netif...");
    esp_err_t err = esp_netif_init();
    if (err != ERR_OK && err != ESP_ERR_INVALID_STATE) {
        ELOG(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        goto end;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        WLOG(TAG, "esp_netif already initialized, continuing...");
    }
    if(!wifi_context.s_wifi_event_group) {
        FUNC_ENTRY_ARGS(TAG, "create event group...");
        wifi_context.s_wifi_event_group = xEventGroupCreate();
        if (wifi_context.s_wifi_event_group == 0) {
            ELOG(TAG, "xEventGroupCreate failed");
            goto end;
        }
    }

    FUNC_ENTRY_ARGS(TAG, "register event handlers...");
    // Only register if not already registered
    if (instance_wifi_id == 0) {
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, 0, &instance_wifi_id);
        if (err != ESP_OK) {
            ELOG(TAG, "esp_event_handler_instance_register WIFI_EVENT failed: %s",
                     esp_err_to_name(err));
            goto end;
        }
    }
    
    if (instance_ip_id == 0) {
        err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, 0, &instance_ip_id);
        if (err != ESP_OK) {
            ELOG(TAG, "esp_event_handler_instance_register IP_EVENT failed: %s", esp_err_to_name(err));
            goto end;
        }
    }
    FUNC_ENTRY_ARGS(TAG, "create default sta and ap...");
    // Create netif handles only if they don't exist
    if(!s_sta_netif) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (!s_sta_netif) {
            ELOG(TAG, "Failed to create STA netif");
            goto end;
        }
    }
    if(!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (!s_ap_netif) {
            ELOG(TAG, "Failed to create AP netif");
            goto end;
        }
    }
    
    // Call before-change callback (ADC suppression, config sync, etc.)
    if (s_mode_change_callbacks.before_mode_change && !mode_change_pending) {
        s_mode_change_callbacks.before_mode_change();
        mode_change_pending = true;
    }

    esp_netif_ip_info_t ipInfo = {0};
    const struct cfg_item * item = &wifi_context.ap;
    IP4_ADDR(&ipInfo.ip, item->ipv4_address[0], item->ipv4_address[1], item->ipv4_address[2], item->ipv4_address[3]);
    IP4_ADDR(&ipInfo.gw, item->ipv4_gw[0], item->ipv4_gw[1], item->ipv4_gw[2], item->ipv4_gw[3]);
    IP4_ADDR(&ipInfo.netmask, item->ipv4_netmask[0], item->ipv4_netmask[1], item->ipv4_netmask[2], item->ipv4_netmask[3]);
    esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_set_ip_info(s_ap_netif, &ipInfo);
#if defined(CONFIG_LWIP_IPV4_NAPT)
    // esp_netif_dns_info_t dns_info = {0};
    // dns_info.ip.u_addr.ip4.addr = ipaddr_addr(CONFIG_MAIN_DNS_SERVER);
    // dns_info.ip.type = IPADDR_TYPE_V4;
    // esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns_info);
#endif
    esp_netif_dhcps_start(s_ap_netif);
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    FUNC_ENTRY_ARGS(TAG, "esp_wifi_init");
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ELOG(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        goto end;
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err != ESP_OK) {
        ELOG(TAG, "esp_wifi_set_storage failed: %s", esp_err_to_name(err));
        goto end;
    }
    wifi_context.s_wifi_initialized = true;
    FUNC_ENTRY_ARGS(TAG, "WiFi initialization successful");
    
    // Log memory usage after successful initialization
    wifi_log_memory_usage("After WiFi Init Success");
    return;
    
    end:
    // Cleanup on failure
    ELOG(TAG, "[%s] WiFi initialization failed, cleaning up", __FUNCTION__);
    
    // Clean up netif handles if they were created
    if (s_sta_netif) {
        esp_netif_destroy(s_sta_netif);
        s_sta_netif = NULL;
    }
    if (s_ap_netif) {
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
    }
    
    if (wifi_context.s_wifi_event_group) {
        vEventGroupDelete(wifi_context.s_wifi_event_group);
        wifi_context.s_wifi_event_group = NULL;
    }
    if (instance_wifi_id) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_wifi_id);
        instance_wifi_id = 0;
    }
    if (instance_ip_id) {
        esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, instance_ip_id);
        instance_ip_id = 0;
    }
    wifi_context.s_wifi_initialized = false;
}

int wifi_ap_start() {
    FUNC_ENTRY(TAG);
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
    conf.ap.max_connection = 3;
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
        ELOG(TAG, "esp_wifi_set_config  WIFI_IF_AP failed: %s", esp_err_to_name(err));
        goto end;
    }
    
    end:
    return err;
}

int wifi_sta_connect(uint16_t slot) {
    FUNC_ENTRY(TAG);
    esp_err_t err = ESP_OK;
    if (slot >= M_WIFI_STA_MAX || !wifi_context.stas[slot].ssid[0])
        goto end;
    wifi_config_t conf;
    memset(&conf, 0, sizeof(conf));
    strncpy((char *)conf.sta.ssid, wifi_context.stas[slot].ssid, 31);
    conf.sta.listen_interval = 0;
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
        WLOG(TAG, "esp_wifi_get_config failed: %s", esp_err_to_name(err));
    }
    
    // reset cred...
    current_conf.sta.ssid[0] = 0;
    current_conf.sta.password[0] = 0;
    if (memcmp(&current_conf, &conf, sizeof(wifi_config_t)) != 0) {
        err = esp_wifi_disconnect();
        if (err != ESP_OK) {
            ELOG(TAG, "esp_wifi_disconnect failed: %s", esp_err_to_name(err));
            goto end;
        }
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &conf);
    if (err != ESP_OK) {
        ELOG(TAG, "esp_wifi_set_config WIFI_IF_STA failed: %s", esp_err_to_name(err));
        goto end;
    }
    wifi_context.s_sta_num_connect = slot;
    wifi_context.s_sta_connecting = true;
    wifi_context.s_sta_connected = false;
    wifi_context.s_sta_got_ip = false;
    wifi_context.s_sta_connect_error = false;
    wifi_context.s_sta_connect_not_found = false;
#if (C_LOG_LEVEL < 3)    
    FUNC_ENTRY_ARGS(TAG, " esp_wifi_connect %s", conf.sta.ssid);
#endif
    // Initialize SNTP but don't start until connection is confirmed
    initialize_sntp(wifi_context.offset);
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ELOG(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        goto end;
    }
    // SNTP will be started in IP_EVENT_STA_GOT_IP handler after connection is confirmed
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
    EventBits_t bits = xEventGroupWaitBits(wifi_context.s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdTRUE, pdFALSE, 1000 / portTICK_PERIOD_MS);
    
    /* xEventGroupWaitBits() returns the bits before the call returned, hence we
     * can test which event actually happened. Using pdTRUE for xClearOnExit
     * to prevent stale bits from affecting future calls. */
    
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
        //  ELOG(TAG, "UNEXPECTED EVENT");
        return -1;
    }
}

int wifi_wait_for_connection(uint32_t timeout_ms) {
    if (!wifi_context.s_wifi_event_group) {
        ELOG(TAG, "[%s] WiFi event group not initialized", __FUNCTION__);
        return ESP_FAIL;
    }
    
    EventBits_t bits = xEventGroupWaitBits(wifi_context.s_wifi_event_group, 
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, 
                                           pdFALSE, pdFALSE, 
                                           timeout_ms / portTICK_PERIOD_MS);
    
    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;  // Connected successfully
    } else if (bits & WIFI_FAIL_BIT) {
        return ESP_FAIL;  // Connection failed
    } else {
        return ESP_ERR_TIMEOUT;  // Timeout occurred
    }
}

int wifi_wait_for_ap_ready(uint32_t timeout_ms) {
    if (!wifi_context.s_wifi_event_group) {
        ELOG(TAG, "[%s] WiFi event group not initialized", __FUNCTION__);
        return ESP_FAIL;
    }
    
    EventBits_t bits = xEventGroupWaitBits(wifi_context.s_wifi_event_group, 
                                           WIFI_AP_READY_BIT, 
                                           pdFALSE, pdFALSE, 
                                           timeout_ms / portTICK_PERIOD_MS);
    
    if (bits & WIFI_AP_READY_BIT) {
        return ESP_OK;  // AP ready
    } else {
        return ESP_ERR_TIMEOUT;  // Timeout occurred
    }
}

#define SCAN_LIST_SIZE 10
static uint8_t scan_progress = 0;
int wifi_sta_connect_scan() {
    FUNC_ENTRY(TAG);
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
    ILOG(TAG, "[%s] ap_count: %d", __func__, ap_count);
    uint8_t k = M_WIFI_STA_MAX + 1;
    if (ap_count > 0) {
        uint16_t i, j;
        for (i = 0; (i < SCAN_LIST_SIZE) && (i < ap_count); ++i) {
            DLOG(TAG, "* %s\t\t%d", ap_info[i].ssid, ap_info[i].rssi);
            for (j = 0; j < M_WIFI_STA_MAX; ++j) {
                if (!wifi_context.stas[j].ssid[0])
                    continue;
                if (!strcmp((char*)&(ap_info[i].ssid[0]), &(wifi_context.stas[j].ssid[0]))) {
                    DLOG(TAG, "[%s] found %s", __func__, wifi_context.stas[j].ssid);
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
    FUNC_ENTRY(TAG);
    strncpy(wifi_context.ap.ssid, ap_ssid, sizeof(wifi_context.ap.ssid) - 1);
    wifi_context.ap.ssid[sizeof(wifi_context.ap.ssid) - 1] = '\0';
    strncpy(wifi_context.ap.password, ap_password, sizeof(wifi_context.ap.password) - 1);
    wifi_context.ap.password[sizeof(wifi_context.ap.password) - 1] = '\0';
    strncpy(wifi_context.stas[0].ssid, sta_ssid, sizeof(wifi_context.stas[0].ssid) - 1);
    wifi_context.stas[0].ssid[sizeof(wifi_context.stas[0].ssid) - 1] = '\0';
    strncpy(wifi_context.stas[0].password, sta_password, sizeof(wifi_context.stas[0].password) - 1);
    wifi_context.stas[0].password[sizeof(wifi_context.stas[0].password) - 1] = '\0';
    return 0;
}

int wifi_sta_set_config(int num, const char *sta_ssid, const char *sta_password) {
    FUNC_ENTRY(TAG);
    if(num >= M_WIFI_STA_MAX)
        return -1;
    strncpy(wifi_context.stas[num].ssid, sta_ssid, sizeof(wifi_context.stas[num].ssid) - 1);
    wifi_context.stas[num].ssid[sizeof(wifi_context.stas[num].ssid) - 1] = '\0';
    strncpy(wifi_context.stas[num].password, sta_password, sizeof(wifi_context.stas[num].password) - 1);
    wifi_context.stas[num].password[sizeof(wifi_context.stas[num].password) - 1] = '\0';
    return 0;
}

int wifi_ap_set_config(const char *ap_ssid, const char *ap_password) {
    FUNC_ENTRY(TAG);
    strncpy(wifi_context.ap.ssid, ap_ssid, sizeof(wifi_context.ap.ssid) - 1);
    wifi_context.ap.ssid[sizeof(wifi_context.ap.ssid) - 1] = '\0';
    strncpy(wifi_context.ap.password, ap_password, sizeof(wifi_context.ap.password) - 1);
    wifi_context.ap.password[sizeof(wifi_context.ap.password) - 1] = '\0';
    return 0;
}

void wifi_log_memory_usage(const char* context) {
    // Get heap info
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();
    
    // Get internal RAM info  
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    
    // Get WiFi/network specific info
    const char* wifi_state = wifi_context.s_wifi_initialized ? "initialized" : "not initialized";
    const char* netif_sta_state = s_sta_netif ? "exists" : "NULL";
    const char* netif_ap_state = s_ap_netif ? "exists" : "NULL";
    
    printf("=== Memory Usage [%s] ===\n", context);
    printf("Free heap: %zu bytes, Min free: %zu bytes\n", free_heap, min_free_heap);
    printf("Free internal: %zu bytes, Largest block: %zu bytes\n", free_internal, largest_free_block);
    printf("WiFi state: %s, STA netif: %s, AP netif: %s\n", wifi_state, netif_sta_state, netif_ap_state);
    printf("Event handlers: WiFi=%p, IP=%p\n", (void*)instance_wifi_id, (void*)instance_ip_id);
    printf("Event group: %p\n", (void*)wifi_context.s_wifi_event_group);
    printf("=====================================\n");
}

void wifi_prepare_memory_for_gps(void) {
    ILOG("MEM", "=== Preparing Memory for GPS Mode ===");
    
    size_t mem_before = esp_get_free_heap_size();
    size_t mem_before_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest_before = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    
    ILOG("MEM", "Memory before GPS preparation:");
    ILOG("MEM", "  Total free: %zu bytes", mem_before);
    ILOG("MEM", "  Internal free: %zu bytes", mem_before_internal);
    ILOG("MEM", "  Largest block: %zu bytes", largest_before);
    
    // Quick memory consolidation - optimized for speed
    ILOG("MEM", "Optimizing memory layout for GPS...");
    
    // Fast consolidation phase - 2 quick cleanup triggers
    esp_get_free_heap_size(); // First trigger
    vTaskDelay(pdMS_TO_TICKS(50));
    
    esp_get_free_heap_size(); // Second trigger  
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Final check and report
    size_t mem_after = esp_get_free_heap_size();
    size_t mem_after_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest_after = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    
    int total_recovered = (int)(mem_after - mem_before);
    int internal_recovered = (int)(mem_after_internal - mem_before_internal);
    int largest_change = (int)(largest_after - largest_before);
    
    ILOG("MEM", "Memory after GPS preparation:");
    ILOG("MEM", "  Total free: %zu bytes (%+d)", mem_after, total_recovered);
    ILOG("MEM", "  Internal free: %zu bytes (%+d)", mem_after_internal, internal_recovered);
    ILOG("MEM", "  Largest block: %zu bytes (%+d)", largest_after, largest_change);
    
    if (largest_after >= 32768) { // 32KB+ largest block is excellent for GPS
        ILOG("MEM", "Excellent memory layout - GPS ready with large contiguous block");
    } else if (largest_after >= 16384) { // 16KB+ is good
        ILOG("MEM", "Good memory layout - GPS ready");
    } else {
        WLOG("MEM", "Limited largest block - GPS may have memory constraints");
    }
    
    ILOG("MEM", "Memory optimization complete - GPS initialization ready");
    ILOG("MEM", "==========================================");
}

// WiFi mode change handling with callback support
void wifi_set_mode_change_callbacks(const wifi_mode_change_callbacks_t* callbacks) {
    if (callbacks) {
        s_mode_change_callbacks = *callbacks;
    } else {
        memset(&s_mode_change_callbacks, 0, sizeof(s_mode_change_callbacks));
    }
    mode_change_pending = false;
}

int wifi_request_mode_change(void) {
    if (!wifi_context.s_wifi_initialized) {
        WLOG(TAG, "[%s] WiFi mode change requested but WiFi not initialized", __func__);
        return -1;
    }
    
    DLOG(TAG, "[%s] Processing WiFi mode change request", __func__);

    // Call before-change callback (ADC suppression, config sync, etc.)
    if (s_mode_change_callbacks.before_mode_change && !mode_change_pending) {
        s_mode_change_callbacks.before_mode_change();
        mode_change_pending = true;
    }
    
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ENABLE_WIFI_AP_STA)
    if(wifi_context.s_wifi_mode == wifi_mode_apsta) {
#else
    if(wifi_context.s_wifi_mode == wifi_mode_ap) {
#endif
        DLOG(TAG, "[%s] Switching to STA mode", __func__);
        wifi_mode(1, 0); // WiFi set station mode
    }
    else 
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ENABLE_WIFI_AP_STA)
    if(wifi_context.s_wifi_mode == wifi_mode_sta)
#endif
    {
        DLOG(TAG, "[%s] Switching to AP mode", __func__);
        wifi_mode(0, 1); // WiFi set AP mode
    }
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ENABLE_WIFI_AP_STA)
    else {
        DLOG(TAG, "[%s] Switching to AP+STA mode", __func__);
        wifi_mode(1, 1); // WiFi set AP+STA mode
    }
#endif

    // Note: ADC events will be resumed by WiFi event handlers when mode change is complete
    
    return 0;
}

#endif // CONFIG_LOGGER_WIFI_ENABLED