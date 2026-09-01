/* Tests fuer den Ereignis-Ringpuffer, siehe eventlog.c. */
#include <string.h>

#include <unity.h>

#include "eventlog.h"

static evlog_t lg;

void setUp(void)   { evlog_init(&lg); }
void tearDown(void) {}

static void test_push_and_json_newest_first(void)
{
    evlog_push(&lg, 100, EVLOG_INFO, "sys", "Start");
    evlog_push(&lg, 200, EVLOG_WARN, "wifi", "schwaches Signal %d dBm", -84);

    char json[512];
    size_t n = evlog_json(&lg, EVLOG_INFO, json, sizeof(json));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"seq\":2"));
    /* Neuester Eintrag zuerst */
    const char *a = strstr(json, "schwaches Signal -84 dBm");
    const char *b = strstr(json, "Start");
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_TRUE(a < b);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"src\":\"wifi\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"sev\":1"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"t\":200"));
}

static void test_min_sev_filter(void)
{
    evlog_push(&lg, 1, EVLOG_INFO, "sys", "info");
    evlog_push(&lg, 2, EVLOG_WARN, "bus", "warnung");
    evlog_push(&lg, 3, EVLOG_ERR,  "bus", "fehler");

    char json[512];
    evlog_json(&lg, EVLOG_WARN, json, sizeof(json));
    TEST_ASSERT_NULL(strstr(json, "\"msg\":\"info\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"msg\":\"warnung\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"msg\":\"fehler\""));

    evlog_json(&lg, EVLOG_ERR, json, sizeof(json));
    TEST_ASSERT_NULL(strstr(json, "warnung"));
    TEST_ASSERT_NOT_NULL(strstr(json, "fehler"));
}

static void test_ring_overwrites_oldest(void)
{
    for (uint32_t i = 0; i < EVLOG_CAPACITY + 5u; ++i) {
        evlog_push(&lg, i, EVLOG_INFO, "sys", "e%lu", (unsigned long)i);
    }
    TEST_ASSERT_EQUAL_UINT16(EVLOG_CAPACITY, lg.count);

    char json[2048];
    evlog_json(&lg, EVLOG_INFO, json, sizeof(json));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"seq\":37"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"msg\":\"e36\""));   /* neuester */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"msg\":\"e5\""));    /* aeltester noch da */
    TEST_ASSERT_NULL(strstr(json, "\"msg\":\"e4\""));        /* herausgefallen */
}

static void test_json_escapes_message(void)
{
    evlog_push(&lg, 1, EVLOG_ERR, "bus", "Adr 3 \"kaputt\"\n\\ende");
    char json[256];
    evlog_json(&lg, EVLOG_INFO, json, sizeof(json));
    TEST_ASSERT_NOT_NULL(strstr(json, "Adr 3 \\\"kaputt\\\"\\n\\\\ende"));
}

static void test_long_message_truncated_not_overrun(void)
{
    char big[300];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    evlog_push(&lg, 1, EVLOG_INFO, "sys", "%s", big);
    TEST_ASSERT_TRUE(strlen(lg.buf[0].msg) < EVLOG_MSG_MAX);

    char json[512];
    size_t n = evlog_json(&lg, EVLOG_INFO, json, sizeof(json));
    TEST_ASSERT_TRUE(n > 0);
}

static void test_clear_keeps_seq(void)
{
    evlog_push(&lg, 1, EVLOG_INFO, "sys", "a");
    evlog_push(&lg, 2, EVLOG_INFO, "sys", "b");
    evlog_clear(&lg);
    TEST_ASSERT_EQUAL_UINT16(0, lg.count);

    char json[128];
    evlog_json(&lg, EVLOG_INFO, json, sizeof(json));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"seq\":2"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"entries\":[]"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_push_and_json_newest_first);
    RUN_TEST(test_min_sev_filter);
    RUN_TEST(test_ring_overwrites_oldest);
    RUN_TEST(test_json_escapes_message);
    RUN_TEST(test_long_message_truncated_not_overrun);
    RUN_TEST(test_clear_keeps_seq);
    return UNITY_END();
}
