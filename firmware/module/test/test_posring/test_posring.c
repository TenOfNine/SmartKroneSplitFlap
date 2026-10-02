/* Tests fuer den Positions-Ringpuffer, siehe posring.c / Spezifikation 6.2. */
#include <string.h>

#include <unity.h>

#include "posring.h"

#define BASE   16u
#define SLOTS  16u
#define EE_LEN 64u

/* Simuliertes EEPROM mit Schreibprotokoll und Stromausfall-Modell. */
static uint8_t  ee[EE_LEN];
static uint32_t writes_per_addr[EE_LEN];
static uint32_t write_count;
static long     power_fail_after;   /* < 0: kein Ausfall; sonst Anzahl erlaubter Writes */
static uint8_t  torn_mask;          /* ODER-Maske fuer den ersten verlorenen Write */
static uint16_t last_write_addr[4];

static uint8_t sim_read(void *ctx, uint16_t addr)
{
    (void)ctx;
    TEST_ASSERT_TRUE(addr < EE_LEN);
    return ee[addr];
}

static void sim_write(void *ctx, uint16_t addr, uint8_t value)
{
    (void)ctx;
    TEST_ASSERT_TRUE(addr >= BASE && addr < BASE + 2u * SLOTS);
    if (power_fail_after >= 0 && (long)write_count >= power_fail_after) {
        if ((long)write_count == power_fail_after && torn_mask != 0u) {
            /* abgebrochener Write: geloescht (0xFF) und nur teilweise programmiert */
            ee[addr] = (uint8_t)(value | torn_mask);
        }
        write_count++;
        return;
    }
    if ((addr - BASE) % 2u == 0u) {
        TEST_ASSERT_NOT_EQUAL(0xFF, value);   /* seq 0xFF wird nie geschrieben */
    }
    last_write_addr[write_count % 4u] = addr;
    ee[addr] = value;
    writes_per_addr[addr]++;
    write_count++;
}

static posring_t ring;

static void boot(void)
{
    posring_init(&ring, BASE, SLOTS, sim_read, sim_write, NULL);
}

void setUp(void)
{
    memset(ee, 0xFF, sizeof(ee));
    memset(writes_per_addr, 0, sizeof(writes_per_addr));
    write_count = 0;
    power_fail_after = -1;
    torn_mask = 0;
    boot();
}
void tearDown(void) {}

static void test_empty_ring_has_no_position(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, posring_position(&ring));
    TEST_ASSERT_FALSE(posring_invalidate(&ring));   /* nichts zu tun */
    TEST_ASSERT_EQUAL_UINT32(0, write_count);
}

static void test_store_and_reload(void)
{
    posring_store(&ring, 17);
    TEST_ASSERT_EQUAL_UINT8(17, posring_position(&ring));
    boot();
    TEST_ASSERT_EQUAL_UINT8(17, posring_position(&ring));
}

static void test_pos_written_before_seq(void)
{
    posring_store(&ring, 5);
    TEST_ASSERT_EQUAL_UINT32(2, write_count);
    TEST_ASSERT_EQUAL_UINT16(BASE + 1u, last_write_addr[0]);   /* pos */
    TEST_ASSERT_EQUAL_UINT16(BASE, last_write_addr[1]);        /* seq */
}

/* > 600 Speicherungen: jeder Neustart liefert die letzte Position, seq laeuft
 * ueber 255 hinweg, und alle Slots werden gleich oft beschrieben (Wrap-Fehler
 * aus #24: ab #255 landete alles im selben Slot). */
static void test_wrap_many_stores(void)
{
    const unsigned n = 1000u;
    for (unsigned i = 0; i < n; ++i) {
        const uint8_t pos = (uint8_t)((i % 40u) + 1u);
        posring_store(&ring, pos);
        boot();
        TEST_ASSERT_EQUAL_UINT8(pos, posring_position(&ring));
    }
    uint32_t lo = 0xFFFFFFFFu, hi = 0;
    for (unsigned s = 0; s < SLOTS; ++s) {
        const uint32_t w = writes_per_addr[BASE + 2u * s];
        TEST_ASSERT_EQUAL_UINT32(w, writes_per_addr[BASE + 2u * s + 1u]);
        if (w < lo) lo = w;
        if (w > hi) hi = w;
    }
    TEST_ASSERT_TRUE(hi - lo <= 1u);
    TEST_ASSERT_TRUE(lo >= n / SLOTS - 1u);
}

/* Fahrtablauf wie in der Firmware: Start invalidiert, Stillstand speichert. */
static void test_invalidate_and_store_cycle(void)
{
    posring_store(&ring, 3);
    for (unsigned i = 0; i < 700u; ++i) {
        TEST_ASSERT_TRUE(posring_invalidate(&ring));
        TEST_ASSERT_FALSE(posring_invalidate(&ring));   /* nur einmal */
        boot();
        TEST_ASSERT_EQUAL_UINT8(0, posring_position(&ring));   /* Ausfall waehrend Fahrt */
        const uint8_t pos = (uint8_t)((i * 7u) % 40u + 1u);
        posring_store(&ring, pos);
        boot();
        TEST_ASSERT_EQUAL_UINT8(pos, posring_position(&ring));
    }
}

static void test_store_same_position_skips(void)
{
    posring_store(&ring, 9);
    const uint32_t w = write_count;
    posring_store(&ring, 9);
    TEST_ASSERT_EQUAL_UINT32(w, write_count);
    posring_store(&ring, 0);   /* 0 = ungueltig -> Invalidierung */
    TEST_ASSERT_EQUAL_UINT8(0, posring_position(&ring));
}

/* Stromausfall nach jedem einzelnen Write einer Operation: nach dem Neustart
 * gilt entweder der alte oder der neue Stand, nie etwas Drittes. */
static void check_power_loss(uint8_t torn)
{
    for (unsigned hist = 0; hist < 40u; ++hist) {
        for (unsigned cut = 0; cut < 2u; ++cut) {
            for (int op = 0; op < 2; ++op) {
                setUp();
                for (unsigned i = 0; i < hist; ++i) {
                    posring_store(&ring, (uint8_t)(i % 40u + 1u));
                    if (i % 3u == 0u) {
                        (void)posring_invalidate(&ring);
                    }
                }
                if (op == 1 && posring_position(&ring) == 0u) {
                    posring_store(&ring, 33);
                }
                const uint8_t before = posring_position(&ring);
                const uint8_t after = (op == 0) ? 21u : 0u;
                power_fail_after = (long)(write_count + cut);
                torn_mask = torn;
                if (op == 0) {
                    posring_store(&ring, 21);
                } else {
                    (void)posring_invalidate(&ring);
                }
                power_fail_after = -1;
                torn_mask = 0;
                boot();
                const uint8_t got = posring_position(&ring);
                TEST_ASSERT_TRUE_MESSAGE(got == before || got == after,
                                         "weder alter noch neuer Stand");
                /* Ring bleibt danach voll funktionsfaehig */
                posring_store(&ring, 11);
                boot();
                TEST_ASSERT_EQUAL_UINT8(11, posring_position(&ring));
            }
        }
    }
}

static void test_power_loss_between_writes(void)
{
    check_power_loss(0x00);
}

static void test_power_loss_torn_write(void)
{
    check_power_loss(0x01);
    check_power_loss(0x80);
    check_power_loss(0xFF);
}

/* Abgerissenes seq-Byte mit jeder moeglichen Bitmaske. Die Historien 129..140
 * und 257..269 sind die Lagen, in denen ein reiner Modulo-255-Vergleich
 * einen alten Eintrag als neuesten waehlen wuerde (zirkulaere Ordnung). */
static void test_power_loss_torn_seq_all_masks(void)
{
    static const uint16_t hists[] = { 5, 16, 17, 129, 130, 131, 132, 133, 134,
                                      136, 137, 138, 140, 254, 255, 256, 257,
                                      258, 260, 263, 266, 269 };
    for (unsigned h = 0; h < sizeof(hists) / sizeof(hists[0]); ++h) {
        for (unsigned mask = 1; mask < 256u; ++mask) {
            setUp();
            for (unsigned i = 0; i < hists[h]; ++i) {
                posring_store(&ring, (uint8_t)(i % 40u + 1u));
            }
            const uint8_t before = posring_position(&ring);
            power_fail_after = (long)(write_count + 1u);   /* pos ok, seq reisst ab */
            torn_mask = (uint8_t)mask;
            posring_store(&ring, 77);
            power_fail_after = -1;
            torn_mask = 0;
            boot();
            const uint8_t got = posring_position(&ring);
            TEST_ASSERT_TRUE_MESSAGE(got == before || got == 77u,
                                     "abgerissene seq waehlt alten Eintrag");
        }
    }
}

/* Abbild der Firmware bis v1.15 nach dem Wrap-Fehler: 15 Slots mit seq
 * 239..253, dahinter der Slot, in den sie seither immer seq 0xFF schrieb. */
static void test_legacy_ring_after_wrap_bug(void)
{
    memset(ee, 0xFF, sizeof(ee));
    uint8_t seq = 239;
    for (unsigned s = 0; s < SLOTS; ++s) {
        const unsigned slot = (s + 5u) % SLOTS;   /* beliebige Startlage */
        ee[BASE + 2u * slot] = (s == SLOTS - 1u) ? 0xFFu : seq;
        ee[BASE + 2u * slot + 1u] = (uint8_t)(s + 1u);
        seq++;
    }
    boot();
    TEST_ASSERT_EQUAL_UINT8(15, posring_position(&ring));   /* Slot mit seq 253 */
    posring_store(&ring, 30);
    boot();
    TEST_ASSERT_EQUAL_UINT8(30, posring_position(&ring));
    for (unsigned i = 0; i < 300u; ++i) {
        posring_store(&ring, (uint8_t)(i % 40u + 1u));
    }
    boot();
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(299u % 40u + 1u), posring_position(&ring));
}

static void test_pos_ff_is_invalid(void)
{
    ee[BASE] = 0;
    ee[BASE + 1u] = 0xFF;
    boot();
    TEST_ASSERT_EQUAL_UINT8(0, posring_position(&ring));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_ring_has_no_position);
    RUN_TEST(test_store_and_reload);
    RUN_TEST(test_pos_written_before_seq);
    RUN_TEST(test_wrap_many_stores);
    RUN_TEST(test_invalidate_and_store_cycle);
    RUN_TEST(test_store_same_position_skips);
    RUN_TEST(test_power_loss_between_writes);
    RUN_TEST(test_power_loss_torn_write);
    RUN_TEST(test_power_loss_torn_seq_all_masks);
    RUN_TEST(test_legacy_ring_after_wrap_bug);
    RUN_TEST(test_pos_ff_is_invalid);
    return UNITY_END();
}
