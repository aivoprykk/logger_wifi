#ifndef SNTP_H
#define SNTP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t set_time_zone(float offset);
void initialize_sntp(float offset);
int uninitialize_sntp(void);
//void obtain_sntp_time(void);
void print_local_time();

#ifdef __cplusplus
}
#endif
#endif