#if !defined(REQUESTS_H)
#define REQUESTS_H

#include <curl/curl.h>

#ifdef UNIT_TEST
int parse_ip_info(const char *json_string, double *latitude, double *longitude);
#endif

int get_geolocation(CURL *curl, const char *ip_address, double *latitude, double *longitude);
int get_forecast(CURL *curl, double latitude, double longitude, double *temperature, int *humidity, double *wind_speed,
                 int *precipitation, int *cloud_cover, int len);
#endif // REQUESTS_H
