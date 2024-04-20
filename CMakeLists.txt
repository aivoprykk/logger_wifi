### logger_context ###
#set(REQ gps_log)
set(PRIVREQ esp_wifi nvs_flash logger_common logger_config)
set(SRCS wifi.c sntp.c)
SET(INCS include)

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS ${INCS}
    REQUIRES ${REQ}
    PRIV_REQUIRES ${PRIVREQ}
)
