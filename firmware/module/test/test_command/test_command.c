/* Tests fuer die Kommandotabelle, siehe protocol.c / Spezifikation 5.4. */
#include <unity.h>

#include "protocol.h"

void setUp(void) {}
void tearDown(void) {}

static void test_lookup_known(void)
{
    const proto_cmd_info_t *i = proto_cmd_lookup(CMD_SET_ALL);
    TEST_ASSERT_NOT_NULL(i);
    TEST_ASSERT_EQUAL_STRING("SET_ALL", i->name);
    TEST_ASSERT_EQUAL(ADDRESSING_BROADCAST, i->addressing);
    TEST_ASSERT_EQUAL_INT16(-1, i->payload_len);  /* variabel */
    TEST_ASSERT_FALSE(i->has_response);
}

static void test_lookup_unknown(void)
{
    TEST_ASSERT_NULL(proto_cmd_lookup(0x00));
    TEST_ASSERT_NULL(proto_cmd_lookup(0x99));
    TEST_ASSERT_NULL(proto_cmd_lookup(0xFF));
}

static void test_set_requires_unicast_and_one_byte(void)
{
    TEST_ASSERT_TRUE(proto_cmd_is_valid(CMD_SET, 5, 1));
    TEST_ASSERT_FALSE(proto_cmd_is_valid(CMD_SET, PROTO_ADDR_BROADCAST, 1)); /* kein Broadcast */
    TEST_ASSERT_FALSE(proto_cmd_is_valid(CMD_SET, 5, 0));  /* falsche Payload-Laenge */
    TEST_ASSERT_FALSE(proto_cmd_is_valid(CMD_SET, 5, 2));
}

static void test_broadcast_only_commands(void)
{
    TEST_ASSERT_TRUE(proto_cmd_is_valid(CMD_GO, PROTO_ADDR_BROADCAST, 0));
    TEST_ASSERT_FALSE(proto_cmd_is_valid(CMD_GO, 3, 0));   /* nur Broadcast */
    TEST_ASSERT_FALSE(proto_cmd_is_valid(CMD_GO, PROTO_ADDR_BROADCAST, 1)); /* kein Payload */

    TEST_ASSERT_TRUE(proto_cmd_is_valid(CMD_SET_ALL, PROTO_ADDR_BROADCAST, 10));
    TEST_ASSERT_TRUE(proto_cmd_is_valid(CMD_SET_ALL, PROTO_ADDR_BROADCAST, 1));
    TEST_ASSERT_FALSE(proto_cmd_is_valid(CMD_SET_ALL, 4, 10));
}

static void test_both_addressing(void)
{
    TEST_ASSERT_TRUE(proto_cmd_is_valid(CMD_STOP, PROTO_ADDR_BROADCAST, 0));
    TEST_ASSERT_TRUE(proto_cmd_is_valid(CMD_STOP, 250, 0));
    TEST_ASSERT_TRUE(proto_cmd_is_valid(CMD_HOME, 1, 0));
}

static void test_enum_assign_shape(void)
{
    TEST_ASSERT_TRUE(proto_cmd_is_valid(CMD_ENUM_ASSIGN, PROTO_ADDR_BROADCAST, 1));
    TEST_ASSERT_FALSE(proto_cmd_is_valid(CMD_ENUM_ASSIGN, PROTO_ADDR_BROADCAST, 0));
    TEST_ASSERT_FALSE(proto_cmd_is_valid(CMD_ENUM_ASSIGN, 5, 1)); /* nur Broadcast */
    const proto_cmd_info_t *i = proto_cmd_lookup(CMD_ENUM_ASSIGN);
    TEST_ASSERT_TRUE(i->has_response);  /* ACK nur von der Karte mit CHAIN_IN */
}

static void test_address_range(void)
{
    TEST_ASSERT_TRUE(proto_cmd_is_valid(CMD_GET_STATUS, PROTO_ADDR_MAX, 0));
    TEST_ASSERT_FALSE(proto_cmd_is_valid(CMD_GET_STATUS, PROTO_ADDR_MAX + 1, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_lookup_known);
    RUN_TEST(test_lookup_unknown);
    RUN_TEST(test_set_requires_unicast_and_one_byte);
    RUN_TEST(test_broadcast_only_commands);
    RUN_TEST(test_both_addressing);
    RUN_TEST(test_enum_assign_shape);
    RUN_TEST(test_address_range);
    return UNITY_END();
}
