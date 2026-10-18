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

/* Laengstes zulaessiges base_topic/node_id: vollstaendiges, gueltiges JSON
 * (vorher wurde availability ab 35 Zeichen abgeschnitten, #40). */
static void test_longest_topic_and_node_complete(void)
{
    char base[HADISC_TOPIC_MAX + 1u], node[HADISC_NODE_MAX + 1u];
    memset(base, 'b', HADISC_TOPIC_MAX);
    base[HADISC_TOPIC_MAX] = '\0';
    base[10] = '/';
    memset(node, 'n', HADISC_NODE_MAX);
    node[HADISC_NODE_MAX] = '\0';
    TEST_ASSERT_TRUE(hadiscovery_valid_base_topic(base));
    TEST_ASSERT_TRUE(hadiscovery_valid_node_id(node));
    char big[640];
    for (int e = HA_ENT_TEXT; e <= HA_ENT_MODULE_ONLINE; ++e) {
        TEST_ASSERT_EQUAL_INT(0, hadiscovery_entity(topic, sizeof(topic), big, sizeof(big),
                                                    "homeassistant", base, node,
                                                    (ha_entity_t)e, 32));
        TEST_ASSERT_NOT_NULL(strstr(big, "\"payload_not_available\":\"offline\""));
        TEST_ASSERT_EQUAL_CHAR('}', big[strlen(big) - 1u]);
    }
    char too_long[HADISC_TOPIC_MAX + 2u];
    memset(too_long, 'x', sizeof(too_long) - 1u);
    too_long[sizeof(too_long) - 1u] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, hadiscovery_entity(topic, sizeof(topic), big, sizeof(big),
                                                 "homeassistant", too_long, node,
                                                 HA_ENT_TEXT, 0));
}

static void test_topic_and_node_validation(void)
{
    TEST_ASSERT_TRUE(hadiscovery_valid_base_topic("krone/anzeige"));
    TEST_ASSERT_TRUE(hadiscovery_valid_base_topic("home/flur.anzeige-1"));
    TEST_ASSERT_FALSE(hadiscovery_valid_base_topic(""));
    TEST_ASSERT_FALSE(hadiscovery_valid_base_topic("/krone"));
    TEST_ASSERT_FALSE(hadiscovery_valid_base_topic("krone/"));
    TEST_ASSERT_FALSE(hadiscovery_valid_base_topic("krone//a"));
    TEST_ASSERT_FALSE(hadiscovery_valid_base_topic("krone/#"));
    TEST_ASSERT_FALSE(hadiscovery_valid_base_topic("krone/+/a"));
    TEST_ASSERT_FALSE(hadiscovery_valid_base_topic("kro\"ne"));
    TEST_ASSERT_FALSE(hadiscovery_valid_base_topic("kro ne"));
    TEST_ASSERT_TRUE(hadiscovery_valid_node_id("krone_anzeige"));
    TEST_ASSERT_TRUE(hadiscovery_valid_node_id("Anzeige-2"));
    TEST_ASSERT_FALSE(hadiscovery_valid_node_id(""));
    TEST_ASSERT_FALSE(hadiscovery_valid_node_id("a/b"));
    TEST_ASSERT_FALSE(hadiscovery_valid_node_id("a.b"));
    TEST_ASSERT_FALSE(hadiscovery_valid_node_id("0123456789012345678901234567890123"));
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
    RUN_TEST(test_longest_topic_and_node_complete);
    RUN_TEST(test_topic_and_node_validation);
    return UNITY_END();
}
