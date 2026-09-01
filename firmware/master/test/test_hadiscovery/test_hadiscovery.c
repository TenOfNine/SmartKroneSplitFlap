/* Tests fuer die HA-Auto-Discovery, siehe hadiscovery.c / Spezifikation 7.6. */
#include <string.h>

#include <unity.h>

#include "hadiscovery.h"

void setUp(void) {}
void tearDown(void) {}

static char topic[128];
static char payload[640];

static int build(ha_entity_t which, uint8_t n)
{
    return hadiscovery_entity(topic, sizeof(topic), payload, sizeof(payload),
                              "homeassistant", "krone/anzeige", "krone_anzeige",
                              which, n);
}

static void test_text_entity(void)
{
    TEST_ASSERT_EQUAL_INT(0, build(HA_ENT_TEXT, 0));
    TEST_ASSERT_EQUAL_STRING("homeassistant/text/krone_anzeige/text/config", topic);
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"command_topic\":\"krone/anzeige/text/set\""));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"state_topic\":\"krone/anzeige/text/state\""));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"unique_id\":\"krone_anzeige_text\""));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"device\""));
}

static void test_mode_has_options(void)
{
    TEST_ASSERT_EQUAL_INT(0, build(HA_ENT_MODE, 0));
    TEST_ASSERT_EQUAL_STRING("homeassistant/select/krone_anzeige/mode/config", topic);
    TEST_ASSERT_NOT_NULL(strstr(payload, "clock_hms"));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"command_topic\":\"krone/anzeige/mode/set\""));
}

static void test_button_home(void)
{
    TEST_ASSERT_EQUAL_INT(0, build(HA_ENT_HOME, 0));
    TEST_ASSERT_EQUAL_STRING("homeassistant/button/krone_anzeige/home/config", topic);
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"command_topic\":\"krone/anzeige/home/press\""));
}

static void test_module_char_is_per_module(void)
{
    TEST_ASSERT_EQUAL_INT(0, build(HA_ENT_MODULE_CHAR, 3));
    TEST_ASSERT_EQUAL_STRING("homeassistant/sensor/krone_anzeige/module_char_3/config", topic);
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"state_topic\":\"krone/anzeige/module/3/char\""));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"unique_id\":\"krone_anzeige_module_char_3\""));
    TEST_ASSERT_NOT_NULL(strstr(payload, "Modul 3"));
}

static void test_online_binary_sensor(void)
{
    TEST_ASSERT_EQUAL_INT(0, build(HA_ENT_MODULE_ONLINE, 7));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"device_class\":\"connectivity\""));
    TEST_ASSERT_NOT_NULL(strstr(payload, "krone/anzeige/module/7/online"));
}

static void test_availability_topic_present(void)
{
    /* Jede Entity traegt das gemeinsame LWT-Topic. */
    TEST_ASSERT_EQUAL_INT(0, build(HA_ENT_TEXT, 0));
    TEST_ASSERT_NOT_NULL(strstr(payload,
        "\"availability_topic\":\"krone/anzeige/status\""));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"payload_not_available\":\"offline\""));

    TEST_ASSERT_EQUAL_INT(0, build(HA_ENT_MODULE_ONLINE, 5));
    TEST_ASSERT_NOT_NULL(strstr(payload,
        "\"availability_topic\":\"krone/anzeige/status\""));
}

static void test_buffer_too_small(void)
{
    char small[16];
    TEST_ASSERT_EQUAL_INT(-1,
        hadiscovery_entity(topic, sizeof(topic), small, sizeof(small),
                           "homeassistant", "krone/anzeige", "krone_anzeige",
                           HA_ENT_TEXT, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_text_entity);
    RUN_TEST(test_mode_has_options);
    RUN_TEST(test_button_home);
    RUN_TEST(test_module_char_is_per_module);
    RUN_TEST(test_online_binary_sensor);
    RUN_TEST(test_availability_topic_present);
    RUN_TEST(test_buffer_too_small);
    return UNITY_END();
}
