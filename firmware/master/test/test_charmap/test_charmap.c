/* Tests fuer die Zeichenabbildung, siehe charmap.c / Spezifikation 7.4, Anhang A. */
#include <unity.h>

#include "charmap.h"

void setUp(void) {}
void tearDown(void) {}

static void test_blatt_mapping(void)
{
    TEST_ASSERT_EQUAL_UINT8(1, charmap_blatt(' '));
    TEST_ASSERT_EQUAL_UINT8(3, charmap_blatt('0'));
    TEST_ASSERT_EQUAL_UINT8(12, charmap_blatt('9'));
    TEST_ASSERT_EQUAL_UINT8(13, charmap_blatt('A'));
    TEST_ASSERT_EQUAL_UINT8(38, charmap_blatt('Z'));
    TEST_ASSERT_EQUAL_UINT8(13, charmap_blatt('a'));   /* Kleinbuchstabe */
    TEST_ASSERT_EQUAL_UINT8(39, charmap_blatt('-'));
    TEST_ASSERT_EQUAL_UINT8(40, charmap_blatt('.'));
    TEST_ASSERT_EQUAL_UINT8(1, charmap_blatt(':'));    /* nicht darstellbar */
    TEST_ASSERT_EQUAL_UINT8(1, charmap_blatt('#'));
}

static void test_render_left(void)
{
    uint8_t b[10];
    charmap_render("HALLO", b, 10, CHARMAP_ALIGN_LEFT);
    const uint8_t exp[10] = { 20, 13, 24, 24, 27, 1, 1, 1, 1, 1 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, b, 10);
}

static void test_render_right(void)
{
    uint8_t b[6];
    charmap_render("42", b, 6, CHARMAP_ALIGN_RIGHT);
    const uint8_t exp[6] = { 1, 1, 1, 1, charmap_blatt('4'), charmap_blatt('2') };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, b, 6);
}

static void test_render_center(void)
{
    uint8_t b[7];
    charmap_render("ABC", b, 7, CHARMAP_ALIGN_CENTER);
    /* pad_left = (7-3)/2 = 2 */
    const uint8_t exp[7] = { 1, 1, 13, 14, 15, 1, 1 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, b, 7);
}

static void test_lowercase_folds(void)
{
    uint8_t b[5];
    charmap_render("abcde", b, 5, CHARMAP_ALIGN_LEFT);
    const uint8_t exp[5] = { 13, 14, 15, 16, 17 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, b, 5);
}

static void test_umlaut_expands_when_space(void)
{
    /* "GRÜN" -> "GRUEN", passt in 6 */
    uint8_t b[6];
    charmap_render("GR\xC3\x9C" "N", b, 6, CHARMAP_ALIGN_LEFT);
    const uint8_t exp[6] = { charmap_blatt('G'), charmap_blatt('R'),
                             charmap_blatt('U'), charmap_blatt('E'),
                             charmap_blatt('N'), 1 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, b, 6);
}

static void test_umlaut_reduces_when_tight(void)
{
    /* "ÜBEL" -> "UEBEL" (5), passt nicht in 4 -> "UBEL" */
    uint8_t b[4];
    charmap_render("\xC3\x9C" "BEL", b, 4, CHARMAP_ALIGN_LEFT);
    const uint8_t exp[4] = { charmap_blatt('U'), charmap_blatt('B'),
                             charmap_blatt('E'), charmap_blatt('L') };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, b, 4);
}

static void test_sharp_s(void)
{
    uint8_t b[6];
    charmap_render("STRA\xC3\x9F" "E", b, 6, CHARMAP_ALIGN_LEFT);
    /* "STRASSE" (7) passt nicht in 6; ss auf s reduziert -> "STRASE" (6) */
    const uint8_t exp[6] = { charmap_blatt('S'), charmap_blatt('T'),
                             charmap_blatt('R'), charmap_blatt('A'),
                             charmap_blatt('S'), charmap_blatt('E') };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, b, 6);
}

static void test_overlong_truncates(void)
{
    uint8_t b[3];
    charmap_render("ABCDEFG", b, 3, CHARMAP_ALIGN_LEFT);
    const uint8_t exp[3] = { 13, 14, 15 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, b, 3);
}

static void test_unknown_to_leerbild(void)
{
    uint8_t b[5];
    charmap_render("A*B", b, 5, CHARMAP_ALIGN_LEFT);
    const uint8_t exp[5] = { 13, 1, 14, 1, 1 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, b, 5);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_blatt_mapping);
    RUN_TEST(test_render_left);
    RUN_TEST(test_render_right);
    RUN_TEST(test_render_center);
    RUN_TEST(test_lowercase_folds);
    RUN_TEST(test_umlaut_expands_when_space);
    RUN_TEST(test_umlaut_reduces_when_tight);
    RUN_TEST(test_sharp_s);
    RUN_TEST(test_overlong_truncates);
    RUN_TEST(test_unknown_to_leerbild);
    return UNITY_END();
}
