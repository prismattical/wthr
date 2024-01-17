#include <check.h>
#include <stdlib.h>

#define UNIT_TEST

#include "requests.h"

#define HOURS 24

START_TEST(test_parse_ip_info)
{
    const char *jsonString1 = "{ \"ip\": \"127.0.0.1\",\n"
                              "\"bogon\": true\n"
                              "}";
    const char *jsonString2 = "{ \"ip\": \"123.12.0.42\",\n"
                              "\"city\": \"Zhengzhou\",\n"
                              "\"region\": \"Henan\",\n"
                              "\"country\": \"CN\",\n"
                              "\"loc\": \"34.7578,113.6486\",\n"
                              "\"org\": \"AS4837 CHINA UNICOM China169 Backbone\",\n"
                              "\"timezone\": \"Asia/Shanghai\",\n"
                              "\"readme\": \"https://ipinfo.io/missingauth\"\n"
                              "}";
    double latitude = 0.;
    double longitude = 0.;
    int rc;

    rc = parse_ip_info(jsonString1, &latitude, &longitude);
    ck_assert_int_eq(rc, -1);
    ck_assert_double_eq_tol(latitude, 0., 0.0001);
    ck_assert_double_eq_tol(longitude, 0., 0.0001);

    rc = parse_ip_info(jsonString2, &latitude, &longitude);
    ck_assert_int_eq(rc, 0);
    ck_assert_double_eq_tol(latitude, 34.7578, 0.0001);
    ck_assert_double_eq_tol(longitude, 113.6486, 0.0001);
}
END_TEST

START_TEST(test_get_geolocation)
{
    CURL *curl = curl_easy_init();
    ck_assert_ptr_nonnull(curl);

    const char *ip1 = "127.0.0.1";
    const char *ip2 = "123.12.0.42";

    double latitude = 0.;
    double longitude = 0.;
    int rc;

    rc = get_geolocation(curl, ip1, &latitude, &longitude);
    ck_assert_int_eq(rc, -1);
    ck_assert_double_eq_tol(latitude, 0., 0.0001);
    ck_assert_double_eq_tol(longitude, 0., 0.0001);

    rc = get_geolocation(curl, ip2, &latitude, &longitude);
    ck_assert_int_eq(rc, 0);
    ck_assert_double_eq_tol(latitude, 34.7578, 0.0001);
    ck_assert_double_eq_tol(longitude, 113.6486, 0.0001);

    curl_easy_cleanup(curl);
}
END_TEST

START_TEST(test_get_forecast)
{
    CURL *curl = curl_easy_init();
    ck_assert_ptr_nonnull(curl);

    double temperature[HOURS];
    int humidity[HOURS];
    double wind_speed[HOURS];
    int precipitation[HOURS];
    int cloud_cover[HOURS];
    int rc =
        get_forecast(curl, 34.7578, 113.6486, temperature, humidity, wind_speed, precipitation, cloud_cover, HOURS);
    ck_assert_int_eq(rc, 0);
    curl_easy_cleanup(curl);
}
END_TEST

Suite *add_suite()
{
    Suite *s = suite_create("RequestsTests");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_parse_ip_info);
    tcase_add_test(tc_core, test_get_geolocation);
    tcase_add_test(tc_core, test_get_forecast);
    suite_add_tcase(s, tc_core);

    return s;
}

int main()
{
    int number_failed;
    Suite *s = add_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_VERBOSE);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
