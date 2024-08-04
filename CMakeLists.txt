### logger_context ###
set(REQ esp_wifi)
set(PRIVREQ nvs_flash logger_common)
set(SRCS wifi.c sntp.c)
SET(INCS include)

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS ${INCS}
    REQUIRES ${REQ}
    PRIV_REQUIRES ${PRIVREQ}
)
