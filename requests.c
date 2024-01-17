#include "requests.h"

#include <curl/curl.h>
#include <json-c/json.h>
#include <stdio.h>
#include <string.h>

#define IPINFO_URL_LENGTH 100
#define IPINFO_RESPONCE_LENGTH 1000
#define OPEN_METEO_URL_LENGTH 250
#define OPEN_METEO_RESPONCE_LENGTH 2000

int parse_ip_info(const char *json_string, double *latitude, double *longitude)
{
    struct json_object *jsonObj = json_tokener_parse(json_string);

    if (!jsonObj)
    {
        (void)fprintf(stderr, "Error parsing JSON string\n");
        return -1;
    }

    // Check for "bogon" field
    struct json_object *bogonObj;
    if (json_object_object_get_ex(jsonObj, "bogon", &bogonObj))
    {
        if (json_object_get_boolean(bogonObj))
        {
            (void)fprintf(stderr, "Error: IP is bogon\n");
            json_object_put(jsonObj);
            return -1;
        }
    }

    // Get values of "loc" field
    struct json_object *locObj;
    if (json_object_object_get_ex(jsonObj, "loc", &locObj))
    {
        const char *locString = json_object_get_string(locObj);
        // Split locString into latitude and longitude
        (void)sscanf(locString, "%lf,%lf", latitude, longitude);
    }
    else
    {
        (void)fprintf(stderr, "Error: Missing 'loc' field\n");
        json_object_put(jsonObj);
        return -1;
    }

    // Clean up and return success
    json_object_put(jsonObj);
    return 0;
}

static size_t write_geolocation_callback(char *ptr, size_t size, size_t nmeb, void *userbuffer)
{
    char *converted = userbuffer;
    memcpy(converted, ptr, nmeb);
    converted[nmeb] = '\0';
    return size * nmeb;
}

int get_geolocation(CURL *curl, const char *ip_address, double *latitude, double *longitude)
{
    curl_easy_reset(curl);

    CURLcode res;
    static char responce_buffer[IPINFO_RESPONCE_LENGTH];

    static char url[IPINFO_URL_LENGTH];
    (void)snprintf(url, sizeof(url), "https://ipinfo.io/%s", ip_address);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_geolocation_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, responce_buffer);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        (void)fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

        return -1;
    }

    // Parse the response
    int rc = parse_ip_info(responce_buffer, latitude, longitude);
    if (rc != 0)
    {
        (void)fprintf(stderr, "Failed to parse geolocation data: %s\n", responce_buffer);
        return -1;
    }

    return 0;
}

static size_t write_forecast_callback(char *ptr, size_t size, size_t nmeb, void *userbuffer)
{
    char *converted = userbuffer;
    memcpy(converted, ptr, nmeb);
    converted[nmeb] = '\0';
    return size * nmeb;
}

int get_forecast(CURL *curl, double latitude, double longitude, double *temperature, int *humidity, double *wind_speed,
                 int *precipitation, int *cloud_cover, int len)
{
    curl_easy_reset(curl);

    CURLcode res;
    static char responce_buffer[OPEN_METEO_RESPONCE_LENGTH];

    static char url[OPEN_METEO_URL_LENGTH];
    (void)snprintf(url, sizeof(url),
                   "https://api.open-meteo.com/v1/"
                   "forecast?latitude=%lf&longitude=%lf&hourly=temperature_2m,relative_humidity_2m,precipitation_"
                   "probability,cloud_cover,wind_speed_10m&timezone=auto&forecast_days=1",
                   latitude, longitude);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_forecast_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, responce_buffer);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        (void)fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

        return -1;
    }

    struct json_object *root;
    struct json_object *hourly;
    struct json_object *time_array;
    struct json_object *temperature_array;
    struct json_object *relative_humidity_array;
    struct json_object *wind_speed_array;
    struct json_object *precipitation_probability_array;
    struct json_object *cloud_cover_array;

    root = json_tokener_parse(responce_buffer);

    if (json_object_get_type(root) == json_type_object)
    {
        json_object_object_get_ex(root, "hourly", &hourly);
        json_object_object_get_ex(hourly, "time", &time_array);
        int forecast_len = (int)json_object_array_length(time_array);
        if (len != forecast_len)
        {
            (void)fprintf(stderr, "get_forecast(): array length doesn't correspond with forecast length\n");
            return -1;
        }

        json_object_object_get_ex(hourly, "temperature_2m", &temperature_array);
        json_object_object_get_ex(hourly, "relative_humidity_2m", &relative_humidity_array);
        json_object_object_get_ex(hourly, "wind_speed_10m", &wind_speed_array);
        json_object_object_get_ex(hourly, "precipitation_probability", &precipitation_probability_array);
        json_object_object_get_ex(hourly, "cloud_cover", &cloud_cover_array);

        for (int i = 0; i < len; ++i)
        {

            temperature[i] = json_object_get_double(json_object_array_get_idx(temperature_array, i));
            humidity[i] = json_object_get_int(json_object_array_get_idx(relative_humidity_array, i));
            wind_speed[i] = json_object_get_double(json_object_array_get_idx(wind_speed_array, i));
            precipitation[i] = json_object_get_int(json_object_array_get_idx(precipitation_probability_array, i));
            cloud_cover[i] = json_object_get_int(json_object_array_get_idx(cloud_cover_array, i));
        }

        json_object_put(root);
    }
    else
    {
        (void)fprintf(stderr, "get_forecast(): error parsing JSON\n");
        return -1;
    }

    return 0;
}
