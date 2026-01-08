# Logger WiFi Component

A comprehensive WiFi connectivity component for ESP32-based GPS logger applications, providing station (STA), access point (AP), and concurrent modes with SNTP time synchronization, network configuration management, and event-driven architecture.

## Features

### WiFi Connectivity Modes
- **Station Mode (STA)**: Connect to existing WiFi networks
- **Access Point Mode (AP)**: Create WiFi hotspot for configuration
- **Concurrent Mode (AP+STA)**: Simultaneous AP and STA operation
- **Multi-STA Support**: Up to 4 pre-configured STA networks with automatic failover

### Network Management
- **Automatic Connection**: Smart scanning and connection to configured networks
- **Connection Retry**: Configurable retry logic with exponential backoff
- **Network Persistence**: Save/restore WiFi configurations in NVS
- **DHCP Support**: Dynamic IP address assignment
- **Static IP Support**: Manual IP configuration for AP and STA modes

### Time Synchronization
- **SNTP Client**: Network Time Protocol synchronization
- **Timezone Support**: Configurable timezone offsets
- **DST Handling**: Daylight Saving Time support for European timezones
- **GPS Time Integration**: Coordinate with GPS-derived time

### Event-Driven Architecture
- **WiFi Events**: Connection, disconnection, IP assignment notifications
- **Time Sync Events**: SNTP synchronization status updates
- **Mode Change Events**: AP/STA mode transition notifications
- **Integration Events**: Coordinate with other system components

### Memory Management
- **Memory Monitoring**: Built-in heap usage tracking
- **GPS Mode Optimization**: Aggressive memory cleanup for GPS operations
- **Resource Management**: Efficient memory usage in constrained environments

### Configuration Management
- **Runtime Configuration**: Dynamic WiFi settings via API
- **Kconfig Integration**: Build-time configuration options
- **Validation**: Input validation and error handling
- **Hostname Support**: Custom device hostname configuration

## Installation

### ESP-IDF Integration
Add this component to your ESP-IDF project:

```cmake
# In your project's CMakeLists.txt
set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/components logger_wifi)
```

### PlatformIO Integration
Add to your `platformio.ini`:

```ini
[env]
lib_deps =
    ; Add other dependencies
    file://./components/logger_wifi
```

## Configuration

### Kconfig Options

#### Core Configuration
```kconfig
CONFIG_LOGGER_WIFI_ENABLED=y                    # Enable WiFi component
CONFIG_WIFI_TASK_STACK_SIZE=2048               # WiFi task stack size
```

#### Network Configuration
```kconfig
CONFIG_MAIN_DNS_SERVER="8.8.8.8"               # Primary DNS server
CONFIG_SNTP_TIME_SERVER="ee.pool.ntp.org"      # SNTP server
CONFIG_SNTP_TIME_ZONE=2                        # Timezone offset (hours)
```

#### Logging Configuration
```kconfig
CONFIG_LOGGER_WIFI_LOG_LEVEL_INFO=y            # Log level (TRACE, DEBUG, INFO, WARN, ERROR, USER, NONE)
```

### Runtime Configuration

#### WiFi Mode Configuration
```c
#include "logger_wifi.h"

// Set WiFi mode (STA, AP, or AP+STA)
esp_err_t ret = wifi_mode(WIFI_MODE_STA, WIFI_MODE_AP);
// WIFI_MODE_NULL = 0, WIFI_MODE_STA = 1, WIFI_MODE_AP = 2, WIFI_MODE_APSTA = 3
```

#### Network Credentials
```c
#include "logger_wifi.h"

// Configure AP mode
wifi_ap_set_config("MyESP32AP", "password123");

// Configure STA networks (up to 4)
wifi_sta_set_config(0, "HomeWiFi", "homepass123");
wifi_sta_set_config(1, "OfficeWiFi", "officepass456");

// Set both AP and STA
wifi_set_config("MyESP32AP", "password123", "HomeWiFi", "homepass123");
```

#### Hostname Configuration
```c
#include "logger_wifi.h"

// Set device hostname
strncpy(wifi_context.hostname, "gps-logger-001", sizeof(wifi_context.hostname));
```

## Usage

### Basic Initialization

```c
#include "logger_wifi.h"

// Initialize WiFi system
wifi_init();

// Set WiFi mode (example: STA + AP concurrent mode)
wifi_mode(WIFI_MODE_STA, WIFI_MODE_AP);

// Wait for connection (optional, with timeout)
if (wifi_wait_for_connection(10000) == ESP_OK) {
    ESP_LOGI(TAG, "WiFi connected successfully");
}

// Use WiFi services...
// When done, uninitialize
wifi_uninit();
```

### Station Mode Connection

```c
#include "logger_wifi.h"

// Configure STA network
wifi_sta_set_config(0, "MyWiFi", "password123");

// Connect to configured network
esp_err_t ret = wifi_sta_connect(0);  // Connect to slot 0
if (ret == ESP_OK) {
    // Wait for connection
    if (wifi_wait_for_connection(15000) == ESP_OK) {
        ESP_LOGI(TAG, "Connected to WiFi network");
    }
}

// Alternative: Scan and connect to best available network
wifi_sta_connect_scan();
```

### Access Point Mode

```c
#include "logger_wifi.h"

// Configure AP
wifi_ap_set_config("ESP32-Config", "config123");

// Start AP mode
esp_err_t ret = wifi_ap_start();
if (ret == ESP_OK) {
    // Wait for AP to be ready
    if (wifi_wait_for_ap_ready(5000) == ESP_OK) {
        ESP_LOGI(TAG, "AP mode ready, SSID: ESP32-Config");
    }
}
```

### Time Synchronization

```c
#include "logger_wifi.h"

// Set timezone (hours offset from UTC)
set_time_zone(2.0);  // Central European Time

// Initialize SNTP client
initialize_sntp(2.0);

// Wait for time synchronization
if (wifi_wait_for_time_sync(10000) == ESP_OK) {
    ESP_LOGI(TAG, "Time synchronized via SNTP");

    // Print current local time
    print_local_time();
}

// Cleanup when done
uninitialize_sntp();
```

### Event Handling

```c
#include "logger_wifi.h"
#include "logger_events.h"

// Register event handler
esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, ip_event_handler, NULL);

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA mode started");
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Connected to AP");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "Disconnected from AP");
                wifi_sta_connect_scan();  // Auto-reconnect
                break;
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "AP mode started");
                break;
        }
    }
}

// IP event handler
static void ip_event_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}
```

### Mode Change Callbacks

```c
#include "logger_wifi.h"

// Define callbacks for mode changes
static wifi_mode_change_callbacks_t mode_callbacks = {
    .before_mode_change = on_before_mode_change,
    .after_mode_change_complete = on_after_mode_change_complete
};

// Set callbacks
wifi_set_mode_change_callbacks(&mode_callbacks);

// Request mode change (will call callbacks)
wifi_request_mode_change();

static void on_before_mode_change(void) {
    // Called before mode change
    // e.g., pause ADC monitoring, save configuration
    ESP_LOGI(TAG, "Preparing for WiFi mode change");
}

static void on_after_mode_change_complete(void) {
    // Called after mode change complete
    // e.g., resume ADC monitoring
    ESP_LOGI(TAG, "WiFi mode change complete");
}
```

### Memory Management

```c
#include "logger_wifi.h"

// Monitor memory usage
wifi_log_memory_usage("Before GPS operation");

// Prepare memory for GPS operations (aggressive cleanup)
wifi_prepare_memory_for_gps();

// Your GPS operations here...

// Memory will be automatically restored when WiFi reconnects
```

## API Reference

### Core Functions

#### Initialization & Deinitialization
- `wifi_init()` - Initialize WiFi system
- `wifi_uninit()` - Deinitialize WiFi system

#### Mode Management
- `wifi_mode(uint8_t sta, uint8_t ap)` - Set WiFi mode (STA/AP/APSTA)
- `wifi_request_mode_change()` - Request mode change with callbacks
- `wifi_set_mode_change_callbacks(const wifi_mode_change_callbacks_t* callbacks)` - Set mode change callbacks

#### Station Mode
- `wifi_sta_connect(uint16_t slot)` - Connect to specific STA network slot
- `wifi_sta_connect_scan()` - Scan and connect to best available network
- `wifi_sta_set_config(int num, const char *ssid, const char *password)` - Configure STA network

#### Access Point Mode
- `wifi_ap_start()` - Start AP mode
- `wifi_ap_set_config(const char *ssid, const char *password)` - Configure AP

#### Configuration
- `wifi_set_config(const char *ap_ssid, const char *ap_password, const char *sta_ssid, const char *sta_password)` - Set both AP and STA config

#### Status & Waiting
- `wifi_status()` - Get current WiFi status
- `wifi_wait_for_connection(uint32_t timeout_ms)` - Wait for STA connection
- `wifi_wait_for_ap_ready(uint32_t timeout_ms)` - Wait for AP ready
- `wifi_wait_for_time_sync(uint32_t timeout_ms)` - Wait for SNTP sync

#### Time Synchronization
- `set_time_zone(float offset)` - Set timezone offset
- `initialize_sntp(float offset)` - Initialize SNTP client
- `uninitialize_sntp()` - Deinitialize SNTP client
- `print_local_time()` - Print current local time

#### Memory Management
- `wifi_log_memory_usage(const char* context)` - Log memory usage
- `wifi_prepare_memory_for_gps()` - Prepare memory for GPS operations

### Data Structures

#### wifi_context (Global Context)
```c
struct m_wifi_context {
    bool s_wifi_started;            // WiFi started flag
    bool s_wifi_initialized;        // WiFi initialized flag
    bool s_nvs_initialized;         // NVS initialized flag

    bool s_sta_connect_not_found;   // STA network not found flag

    bool s_sta_connect_error;       // STA connection error flag
    uint8_t s_sta_num_connect;      // Current STA connection attempt
    uint8_t s_retry_num;            // Retry counter
    uint8_t s_wifi_mode;            // Current WiFi mode

    struct cfg_item ap;             // AP configuration
    struct cfg_item stas[M_WIFI_STA_MAX]; // STA configurations
    char hostname[32];              // Device hostname
    float offset;                   // Timezone offset

    EventGroupHandle_t s_wifi_event_group; // Event group handle
};
```

#### cfg_item (Network Configuration)
```c
struct cfg_item {
    char ssid[32];                  // Network SSID
    char password[64];              // Network password
    uint8_t ipv4_address[4];        // Static IP address
    uint8_t ipv4_netmask[4];        // Network mask
    uint8_t ipv4_gw[4];             // Gateway address
};
```

#### WiFi Modes
```c
enum wifi_modes_s {
    wifi_mode_null = 0,             // No WiFi mode
    wifi_mode_sta = 1,              // Station mode only
    wifi_mode_ap = 2,               // Access point mode only
    wifi_mode_apsta = 3             // Concurrent AP+STA mode
};
```

### Events

#### WiFi Events
- `WIFI_EVENT_WIFI_READY` - WiFi ready
- `WIFI_EVENT_SCAN_DONE` - WiFi scan completed
- `WIFI_EVENT_STA_START` - STA mode started
- `WIFI_EVENT_STA_STOP` - STA mode stopped
- `WIFI_EVENT_STA_CONNECTED` - Connected to AP
- `WIFI_EVENT_STA_DISCONNECTED` - Disconnected from AP
- `WIFI_EVENT_AP_START` - AP mode started
- `WIFI_EVENT_AP_STOP` - AP mode stopped
- `WIFI_EVENT_AP_STACONNECTED` - Station connected to AP
- `WIFI_EVENT_AP_STADISCONNECTED` - Station disconnected from AP

#### IP Events
- `IP_EVENT_STA_GOT_IP` - STA got IP address
- `IP_EVENT_STA_LOST_IP` - STA lost IP address
- `IP_EVENT_AP_STAIPASSIGNED` - AP assigned IP to station

#### Logger Events
- `LOGGER_EVENT_DATETIME_SET` - Date/time set via SNTP

## Examples

### Complete WiFi Setup with Time Sync

```c
#include "logger_wifi.h"
#include "logger_events.h"

static const char *TAG = "wifi_example";

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    ESP_LOGI(TAG, "WiFi Event: %s", wifi_event_strings(event_id));
}

void app_main(void) {
    // Initialize WiFi
    wifi_init();

    // Register event handlers
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);

    // Configure networks
    wifi_ap_set_config("GPS-Logger-Config", "config123");
    wifi_sta_set_config(0, "HomeNetwork", "homepass456");
    wifi_sta_set_config(1, "OfficeNetwork", "officepass789");

    // Set hostname
    strncpy(wifi_context.hostname, "gps-logger-001", sizeof(wifi_context.hostname));

    // Start concurrent AP+STA mode
    wifi_mode(WIFI_MODE_STA, WIFI_MODE_AP);

    // Wait for connections
    wifi_wait_for_ap_ready(5000);
    wifi_wait_for_connection(10000);

    // Initialize time synchronization
    set_time_zone(2.0);  // CET
    initialize_sntp(2.0);

    // Wait for time sync
    if (wifi_wait_for_time_sync(15000) == ESP_OK) {
        ESP_LOGI(TAG, "System ready with network and time sync");
        print_local_time();
    }

    // Main application loop
    while (1) {
        // Your application logic here
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### Configuration Portal Example

```c
#include "logger_wifi.h"

void setup_configuration_portal(void) {
    // Start AP mode for configuration
    wifi_ap_set_config("ESP32-Setup", "setup123");
    wifi_ap_start();

    ESP_LOGI(TAG, "Configuration AP started");
    ESP_LOGI(TAG, "Connect to WiFi: ESP32-Setup");
    ESP_LOGI(TAG, "Password: setup123");
    ESP_LOGI(TAG, "Then visit: http://192.168.4.1");

    // Wait for configuration to be completed
    // (This would typically be done via HTTP server)
}
```

## Troubleshooting

### Common Issues

#### STA Connection Failures
- Verify SSID and password are correct
- Check WiFi signal strength and interference
- Ensure AP supports 2.4GHz (ESP32 limitation)
- Enable debug logging: `CONFIG_LOGGER_WIFI_LOG_LEVEL_DEBUG=y`

#### AP Mode Not Starting
- Check for IP address conflicts on subnet
- Verify AP credentials meet requirements
- Check for memory constraints
- Monitor WiFi events for detailed error information

#### Time Synchronization Issues
- Verify internet connectivity
- Check SNTP server configuration
- Validate timezone settings
- Ensure DNS resolution works

#### Memory Issues
- Monitor heap usage with `wifi_log_memory_usage()`
- Use `wifi_prepare_memory_for_gps()` before memory-intensive operations
- Check for memory leaks in application code
- Consider increasing task stack sizes

### Debug Configuration

Enable detailed debugging:

```kconfig
CONFIG_LOGGER_WIFI_LOG_LEVEL_DEBUG=y
```

### Event Monitoring

Monitor WiFi events for troubleshooting:

```c
// Enable event logging
ESP_LOGI(TAG, "WiFi Event: %s", wifi_event_strings(event_id));
ESP_LOGI(TAG, "WiFi Status: %d", wifi_status());
```

### Network Diagnostics

```c
#include "logger_wifi.h"

// Check connection status
int status = wifi_status();
ESP_LOGI(TAG, "WiFi Status: %d", status);

// Log current configuration
ESP_LOGI(TAG, "AP SSID: %s", wifi_context.ap.ssid);
ESP_LOGI(TAG, "STA SSID: %s", wifi_context.stas[0].ssid);
ESP_LOGI(TAG, "Hostname: %s", wifi_context.hostname);
```

## Dependencies

- ESP-IDF v4.4 or later
- logger_common component
- esp_wifi
- esp_netif
- esp_event
- lwip
- esp_sntp

## Contributing

1. Follow ESP-IDF coding conventions
2. Add comprehensive error handling
3. Include event notifications for state changes
4. Update documentation for new features
5. Test with multiple WiFi configurations

## License

See LICENSE file in component directory.