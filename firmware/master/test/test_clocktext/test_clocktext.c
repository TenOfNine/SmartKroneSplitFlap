/* Tests fuer die Uhrzeitformatierung, siehe clocktext.c / Spezifikation 7.6. */
#include <string.h>

#include <unity.h>

#include "clocktext.h"

void setUp(void) {}
void tearDown(void) {}

static void test_hm_default_sep(void)
{
    char b[16];
    const size_t n = clocktext_format(b, sizeof(b), 9, 5, 0, false, '.');
    TEST_ASSERT_EQUAL_size_t(5, n);
    TEST_ASSERT_EQUAL_STRING("09.05", b);
}

static void test_hms(void)
{
    char b[16];
    const size_t n = clocktext_format(b, sizeof(b), 23, 59, 7, true, '-');
    TEST_ASSERT_EQUAL_size_t(8, n);
    TEST_ASSERT_EQUAL_STRING("23-59-07", b);
}

static void test_invalid_sep_becomes_dot(void)
{
    char b[16];
    clocktext_format(b, sizeof(b), 12, 0, 0, false, ':');
    TEST_ASSERT_EQUAL_STRING("12.00", b);
}

static void test_out_of_range(void)
{
    char b[16];
    TEST_ASSERT_EQUAL_size_t(0, clocktext_format(b, sizeof(b), 24, 0, 0, false, '.'));
    TEST_ASSERT_EQUAL_size_t(0, clocktext_format(b, sizeof(b), 0, 60, 0, false, '.'));
}

static void test_buffer_too_small(void)
{
    char b[5];
    TEST_ASSERT_EQUAL_size_t(0, clocktext_format(b, sizeof(b), 12, 0, 0, false, '.'));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hm_default_sep);
    RUN_TEST(test_hms);
    RUN_TEST(test_invalid_sep_becomes_dot);
    RUN_TEST(test_out_of_range);
    RUN_TEST(test_buffer_too_small);
    return UNITY_END();
}
