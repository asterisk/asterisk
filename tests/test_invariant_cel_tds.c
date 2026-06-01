#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* 
 * Since cel_tds.c is an Asterisk module with complex dependencies,
 * we test the sanitization logic by checking if SQL injection payloads
 * would survive the blacklist-based filtering approach.
 */

/* Simulates the vulnerable blacklist sanitization from cel_tds.c */
static int payload_survives_blacklist(const char *input)
{
    /* Known bad strings from typical blacklist approaches */
    const char *known_bad[] = {"DROP", "DELETE", "INSERT", "UPDATE", "--", ";", NULL};
    char *test_str = strdup(input);
    
    for (int idx = 0; known_bad[idx] != NULL; idx++) {
        char *ptr;
        while ((ptr = strstr(test_str, known_bad[idx])) != NULL) {
            memmove(ptr, ptr + strlen(known_bad[idx]), 
                    strlen(ptr + strlen(known_bad[idx])) + 1);
        }
    }
    
    /* Check if dangerous characters/patterns remain after "sanitization" */
    int dangerous = (strchr(test_str, '\'') != NULL ||
                     strcasestr(test_str, "or") != NULL ||
                     strcasestr(test_str, "union") != NULL ||
                     strcasestr(test_str, "select") != NULL);
    
    free(test_str);
    return dangerous;
}

START_TEST(test_sql_injection_blacklist_bypass)
{
    /* Invariant: User input must never appear in SQL queries without parameterization.
     * Blacklist sanitization is inherently bypassable - this test proves it. */
    const char *payloads[] = {
        "' OR 1=1 --",           /* Classic SQL injection */
        "'; DROP TABLE users; --", /* Destructive injection */
        "' UNION SELECT * FROM passwords --", /* Data exfiltration */
        "valid_account_code",    /* Valid input for comparison */
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);

    for (int i = 0; i < num_payloads - 1; i++) {
        /* Attack payloads should be flagged as dangerous even after blacklist */
        ck_assert_msg(payload_survives_blacklist(payloads[i]),
            "Blacklist failed to block injection payload: %s", payloads[i]);
    }
    
    /* Valid input should not trigger false positive */
    ck_assert_msg(!payload_survives_blacklist(payloads[num_payloads - 1]),
        "Valid input incorrectly flagged as dangerous");
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_sql_injection_blacklist_bypass);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}