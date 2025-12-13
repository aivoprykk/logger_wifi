
set(SRCS wifi.c sntp.c)
SET(INCS include)

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS ${INCS}
    PRIV_REQUIRES logger_common logger_config
)

if(NOT ${IDF_TARGET} STREQUAL "linux" AND CONFIG_LOGGER_WIFI_ENABLED)
    target_link_libraries(${COMPONENT_LIB} PUBLIC idf::esp_wifi)
endif()
if(CONFIG_GPS_LOG_ENABLED)
target_link_libraries(${COMPONENT_LIB} PRIVATE idf::gps_log)
endif()