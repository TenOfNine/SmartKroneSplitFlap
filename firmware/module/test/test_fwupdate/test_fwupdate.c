/* Tests fuer den empfangsseitigen Seiten-Sammler und die Entscheidungslogik
 * des Bootloaders, siehe lib/fwupdate (fwupdate, fwboot). */
#include <string.h>

#include <unity.h>

#include "fwboot.h"
#include "fwupdate.h"
#include "protocol.h"

void setUp(void) {}
void tearDown(void) {}

/* Mitschreiben, was der Callback bekommt. */
#define MAXPAGES 32
static uint8_t  g_written[MAXPAGES][FWUPDATE_PAGE];
static uint32_t g_addr[MAXPAGES];
static int      g_npages;
static bool     g_fail_at_page = false;
static int      g_fail_page = -1;

static bool sink(void *ctx, uint32_t addr, const uint8_t *page)
{
    (void)ctx;
    if (g_fail_at_page && g_npages == g_fail_page) return false;
    if (g_npages < MAXPAGES) {
        g_addr[g_npages] = addr;
        memcpy(g_written[g_npages], page, FWUPDATE_PAGE);
    }
    g_npages++;
    return true;
}

static void reset_sink(void)
{
    memset(g_written, 0, sizeof(g_written));
    memset(g_addr, 0, sizeof(g_addr));
    g_npages = 0;
    g_fail_at_page = false;
    g_fail_page = -1;
}

static uint16_t crc16(const uint8_t *d, size_t n)
{
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++) c = (c & 1) ? (uint16_t)((c >> 1) ^ 0xA001) : (uint16_t)(c >> 1);
    }
    return c;
}

/* Bild in Bruchstuecken von `chunk` Byte einspeisen. */
static fwupdate_result_t feed(fwupdate_t *fu, const uint8_t *img, uint32_t len, uint8_t chunk)
{
    for (uint32_t off = 0; off < len; off += chunk) {
        uint8_t n = (uint8_t)((len - off < chunk) ? (len - off) : chunk);
        fwupdate_result_t r = fwupdate_chunk(fu, off, img + off, n);
        if (r != FWUPDATE_OK) return r;
    }
    return fwupdate_finish(fu);
}

static void test_exact_pages(void)
{
    reset_sink();
    uint8_t img[128];
    for (int i = 0; i < 128; i++) img[i] = (uint8_t)(i ^ 0x5A);
    fwupdate_t fu;
    fwupdate_begin(&fu, sink, NULL, sizeof(img), crc16(img, sizeof(img)));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, feed(&fu, img, sizeof(img), 30));
    TEST_ASSERT_EQUAL_INT(2, g_npages);
    TEST_ASSERT_EQUAL_UINT32(0, g_addr[0]);
    TEST_ASSERT_EQUAL_UINT32(64, g_addr[1]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(img, g_written[0], 64);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(img + 64, g_written[1], 64);
}

static void test_partial_last_page_padded(void)
{
    reset_sink();
    uint8_t img[100];
    for (int i = 0; i < 100; i++) img[i] = (uint8_t)(i + 1);
    fwupdate_t fu;
    fwupdate_begin(&fu, sink, NULL, sizeof(img), crc16(img, sizeof(img)));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, feed(&fu, img, sizeof(img), 16));
    TEST_ASSERT_EQUAL_INT(2, g_npages);
    /* zweite Seite: 36 Nutzbytes + 28x 0xFF */
    for (int i = 36; i < 64; i++) TEST_ASSERT_EQUAL_HEX8(0xFF, g_written[1][i]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(img + 64, g_written[1], 36);
}

static void test_realistic_5433(void)
{
    reset_sink();
    static uint8_t img[5433];
    for (size_t i = 0; i < sizeof(img); i++) img[i] = (uint8_t)((i * 31u + 7u) & 0xFF);
    fwupdate_t fu;
    fwupdate_begin(&fu, sink, NULL, sizeof(img), crc16(img, sizeof(img)));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, feed(&fu, img, sizeof(img), 30));
    TEST_ASSERT_EQUAL_INT(85, g_npages);   /* ceil(5433/64) */
}

static void test_crc_mismatch(void)
{
    reset_sink();
    uint8_t img[64];
    memset(img, 0xAB, sizeof(img));
    fwupdate_t fu;
    fwupdate_begin(&fu, sink, NULL, sizeof(img), 0x1234 /* falsch */);
    TEST_ASSERT_EQUAL_INT(FWUPDATE_ERR_CRC, feed(&fu, img, sizeof(img), 30));
}

static void test_length_mismatch(void)
{
    reset_sink();
    uint8_t img[64];
    memset(img, 0x11, sizeof(img));
    fwupdate_t fu;
    fwupdate_begin(&fu, sink, NULL, 80 /* mehr angekuendigt */, crc16(img, 80));
    for (uint32_t o = 0; o < 64; o += 30) {
        uint8_t n = (o + 30 <= 64) ? 30 : (uint8_t)(64 - o);
        fwupdate_chunk(&fu, o, img + o, n);
    }
    TEST_ASSERT_EQUAL_INT(FWUPDATE_ERR_LENGTH, fwupdate_finish(&fu));
}

static void test_out_of_order_rejected(void)
{
    reset_sink();
    uint8_t data[30] = {0};
    fwupdate_t fu;
    fwupdate_begin(&fu, sink, NULL, 128, 0);
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, fwupdate_chunk(&fu, 0, data, 30));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_ERR_ORDER, fwupdate_chunk(&fu, 60, data, 30)); /* Luecke */
}

static void test_range_exceeded(void)
{
    reset_sink();
    uint8_t data[30] = {0};
    fwupdate_t fu;
    fwupdate_begin(&fu, sink, NULL, 40, 0);
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, fwupdate_chunk(&fu, 0, data, 30));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_ERR_RANGE, fwupdate_chunk(&fu, 30, data, 30)); /* 60 > 40 */
}

static void test_write_failure_propagates(void)
{
    reset_sink();
    g_fail_at_page = true;
    g_fail_page = 1;
    uint8_t img[200];
    memset(img, 7, sizeof(img));
    fwupdate_t fu;
    fwupdate_begin(&fu, sink, NULL, sizeof(img), crc16(img, sizeof(img)));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_ERR_WRITE, feed(&fu, img, sizeof(img), 30));
}

/* --- Wiederaufsetzbarkeit (lib/fwupdate) ------------------------------ */

static void test_duplicate_last_chunk_is_idempotent(void)
{
    reset_sink();
    uint8_t img[128];
    for (int i = 0; i < 128; i++) img[i] = (uint8_t)(i * 3 + 1);
    fwupdate_t fu;
    fwupdate_begin(&fu, sink, NULL, sizeof(img), crc16(img, sizeof(img)));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, fwupdate_chunk(&fu, 0, img, 30));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, fwupdate_chunk(&fu, 30, img + 30, 30));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, fwupdate_chunk(&fu, 60, img + 60, 30)); /* Seite 0 voll */
    TEST_ASSERT_EQUAL_INT(1, g_npages);
    /* ACK verloren -> Master wiederholt denselben Rahmen */
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, fwupdate_chunk(&fu, 60, img + 60, 30));
    TEST_ASSERT_EQUAL_INT(1, g_npages);              /* nicht erneut geschrieben */
    TEST_ASSERT_EQUAL_UINT32(90, fu.received);
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, fwupdate_chunk(&fu, 90, img + 90, 30));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, fwupdate_chunk(&fu, 120, img + 120, 8));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, fwupdate_chunk(&fu, 120, img + 120, 8)); /* letzter doppelt */
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, fwupdate_finish(&fu));                   /* CRC unberuehrt */
    TEST_ASSERT_EQUAL_INT(2, g_npages);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(img, g_written[0], 64);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(img + 64, g_written[1], 64);
}

static void test_duplicate_requires_same_offset_and_length(void)
{
    reset_sink();
    uint8_t data[30] = {0};
    fwupdate_t fu;
    fwupdate_begin(&fu, sink, NULL, 128, 0);
    TEST_ASSERT_EQUAL_INT(FWUPDATE_ERR_ORDER, fwupdate_chunk(&fu, 30, data, 30)); /* noch nichts da */
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, fwupdate_chunk(&fu, 0, data, 30));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, fwupdate_chunk(&fu, 30, data, 30));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_ERR_ORDER, fwupdate_chunk(&fu, 0, data, 30));  /* vorletztes */
    TEST_ASSERT_EQUAL_INT(FWUPDATE_ERR_ORDER, fwupdate_chunk(&fu, 40, data, 20)); /* andere Laenge */
    TEST_ASSERT_EQUAL_UINT32(60, fu.received);
}

static void test_write_failure_is_sticky(void)
{
    reset_sink();
    g_fail_at_page = true;
    g_fail_page = 0;
    uint8_t img[64];
    memset(img, 3, sizeof(img));
    fwupdate_t fu;
    fwupdate_begin(&fu, sink, NULL, sizeof(img), crc16(img, sizeof(img)));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, fwupdate_chunk(&fu, 0, img, 30));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, fwupdate_chunk(&fu, 30, img + 30, 30));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_ERR_WRITE, fwupdate_chunk(&fu, 60, img + 60, 4)); /* Seite 0 */
    /* Wiederholung darf den Fehler nicht als Duplikat bestaetigen */
    TEST_ASSERT_EQUAL_INT(FWUPDATE_ERR_WRITE, fwupdate_chunk(&fu, 60, img + 60, 4));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_ERR_WRITE, fwupdate_finish(&fu));
    /* Neubeginn hebt den Fehler auf */
    g_fail_at_page = false;
    fwupdate_begin(&fu, sink, NULL, sizeof(img), crc16(img, sizeof(img)));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, feed(&fu, img, sizeof(img), 30));
}

/* --- Entscheidungslogik des Bootloaders (fwboot) ----------------------- */

#define BT_APP_MAX 512u
#define BT_BL_VER  2u
#define OWN        7u

static uint8_t  g_flash[BT_APP_MAX];  /* App-Bereich */
static uint8_t  g_marker;             /* EEPROM-Byte "App gueltig" */
static int      g_marker_writes;
static int      g_flash_writes;
static bool     g_flash_fail;

static bool flash_sink(void *ctx, uint32_t addr, const uint8_t *page)
{
    (void)ctx;
    /* Der Marker muss vor jeder Seite ungueltig sein (Spez. 5.7). */
    TEST_ASSERT_EQUAL_HEX8(FWBOOT_MARKER_UPDATE, g_marker);
    if (g_flash_fail) return false;
    TEST_ASSERT_TRUE(addr + FWUPDATE_PAGE <= BT_APP_MAX);
    memcpy(&g_flash[addr], page, FWUPDATE_PAGE);
    g_flash_writes++;
    return true;
}

static void marker_sink(void *ctx, uint8_t m)
{
    (void)ctx;
    g_marker = m;
    g_marker_writes++;
}

static uint16_t first_word(void) { return (uint16_t)(g_flash[0] | (g_flash[1] << 8)); }

/* Kaltstart des Bootloaders mit dem aktuellen Flash-/EEPROM-Stand. */
static void boot(fwboot_t *bt, uint8_t eeprom_addr, bool forced)
{
    fwboot_init(bt, eeprom_addr, forced, fwboot_app_valid(first_word(), g_marker),
                BT_APP_MAX, BT_BL_VER, flash_sink, marker_sink, NULL);
}

/* Karte mit gueltiger alter App (Werksflash: EEPROM geloescht). */
static void card_with_app(void)
{
    memset(g_flash, 0x11, sizeof(g_flash));
    g_marker = FWBOOT_MARKER_BLANK;
    g_marker_writes = 0;
    g_flash_writes = 0;
    g_flash_fail = false;
}

static fwboot_action_t frame(fwboot_t *bt, uint8_t cmd, uint8_t addr,
                             const uint8_t *pl, uint8_t len)
{
    return fwboot_on_frame(bt, cmd, addr, pl, len);
}

static fwboot_action_t begin(fwboot_t *bt, uint8_t addr, uint32_t len, uint16_t crc)
{
    const uint8_t pl[4] = { (uint8_t)len, (uint8_t)(len >> 8), (uint8_t)crc, (uint8_t)(crc >> 8) };
    return frame(bt, CMD_FW_BEGIN, addr, pl, 4);
}

static fwboot_action_t data(fwboot_t *bt, uint32_t off, const uint8_t *d, uint8_t n)
{
    uint8_t pl[2 + PROTO_FW_CHUNK];
    pl[0] = (uint8_t)off;
    pl[1] = (uint8_t)(off >> 8);
    memcpy(&pl[2], d, n);
    return frame(bt, CMD_FW_DATA, bt->addr, pl, (uint8_t)(2u + n));
}

static void expect_reply(const fwboot_t *bt, fwboot_action_t act, uint8_t cmd, uint8_t code)
{
    TEST_ASSERT_EQUAL_INT(FWBOOT_REPLY, act);
    TEST_ASSERT_EQUAL_HEX8(cmd, bt->reply_cmd);
    TEST_ASSERT_EQUAL_UINT8(1, bt->reply_len);
    TEST_ASSERT_EQUAL_HEX8(code, bt->reply[0]);
}

/* Liefert Byte 3 (Flags) der GET_VERSION-Antwort. */
static uint8_t version_flags(fwboot_t *bt)
{
    fwboot_action_t a = frame(bt, CMD_GET_VERSION, bt->addr, NULL, 0);
    TEST_ASSERT_EQUAL_INT(FWBOOT_REPLY, a);
    TEST_ASSERT_EQUAL_HEX8(CMD_GET_VERSION, bt->reply_cmd);
    TEST_ASSERT_EQUAL_UINT8(5, bt->reply_len);
    TEST_ASSERT_EQUAL_UINT8(BT_BL_VER, bt->reply[4]);
    return bt->reply[3];
}

static void test_boot_app_valid_rules(void)
{
    TEST_ASSERT_FALSE(fwboot_app_valid(0xFFFF, FWBOOT_MARKER_BLANK));  /* leerer App-Bereich */
    TEST_ASSERT_FALSE(fwboot_app_valid(0xFFFF, FWBOOT_MARKER_VALID));
    TEST_ASSERT_TRUE(fwboot_app_valid(0x940C, FWBOOT_MARKER_BLANK));   /* Werksflash */
    TEST_ASSERT_TRUE(fwboot_app_valid(0x940C, FWBOOT_MARKER_VALID));   /* Bus-Update fertig */
    TEST_ASSERT_FALSE(fwboot_app_valid(0x940C, FWBOOT_MARKER_UPDATE)); /* abgebrochen */
    TEST_ASSERT_FALSE(fwboot_app_valid(0x940C, 0x5A));                 /* unbekannt: nicht starten */
}

static void test_boot_window_starts_app(void)
{
    card_with_app();
    fwboot_t bt;
    boot(&bt, OWN, false);
    TEST_ASSERT_EQUAL_UINT16(FWBOOT_WINDOW_MS, fwboot_timeout_ms(&bt));
    /* Broadcast und fremde Adressen beenden das Fenster nicht */
    TEST_ASSERT_EQUAL_INT(FWBOOT_IGNORE, frame(&bt, CMD_GET_STATUS, OWN + 1, NULL, 0));
    TEST_ASSERT_EQUAL_INT(FWBOOT_IGNORE, begin(&bt, PROTO_ADDR_BROADCAST, 64, 0));
    TEST_ASSERT_EQUAL_INT(FWBOOT_IGNORE, begin(&bt, PROTO_ADDR_SERVICE, 64, 0));
    TEST_ASSERT_EQUAL_INT(0, g_marker_writes);
    /* Fensterende -> App */
    TEST_ASSERT_EQUAL_INT(FWBOOT_START_APP, fwboot_on_timeout(&bt));

    /* Rahmen an die eigene Adresse, der kein FW_BEGIN ist -> sofort App */
    boot(&bt, OWN, false);
    TEST_ASSERT_EQUAL_INT(FWBOOT_START_APP, frame(&bt, CMD_GET_STATUS, OWN, NULL, 0));
    boot(&bt, OWN, false);
    TEST_ASSERT_EQUAL_INT(FWBOOT_START_APP, frame(&bt, CMD_GET_VERSION, OWN, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, g_marker_writes);
}

static void test_boot_forced_waits_then_starts_valid_app(void)
{
    card_with_app();
    fwboot_t bt;
    boot(&bt, OWN, true);
    TEST_ASSERT_EQUAL_UINT16(FWBOOT_IDLE_MS, fwboot_timeout_ms(&bt));
    TEST_ASSERT_EQUAL_HEX8(PROTO_VER_FLAG_BOOTLOADER | PROTO_VER_FLAG_APP_VALID,
                           version_flags(&bt));
    TEST_ASSERT_EQUAL_HEX8(0xFF, bt.reply[1]);
    TEST_ASSERT_EQUAL_INT(FWBOOT_NONE, frame(&bt, CMD_ENTER_BOOTLOADER, OWN, NULL, 0));
    /* Statusabfragen halten den Bootloader nicht fest */
    TEST_ASSERT_EQUAL_INT(FWBOOT_IGNORE, frame(&bt, CMD_GET_STATUS, OWN, NULL, 0));
    TEST_ASSERT_EQUAL_INT(FWBOOT_START_APP, fwboot_on_timeout(&bt));
}

/* #29: nach abgebrochenem Update nie in die App, Flag aktuell */
static void test_boot_aborted_update_never_starts_app(void)
{
    card_with_app();
    uint8_t img[200];
    for (int i = 0; i < 200; i++) img[i] = (uint8_t)(i + 9);
    fwboot_t bt;
    boot(&bt, OWN, false);                         /* Fensterpfad (GPIOR0 verloren) */
    expect_reply(&bt, begin(&bt, OWN, sizeof(img), crc16(img, sizeof(img))), CMD_FW_BEGIN, 0x01);
    TEST_ASSERT_EQUAL_HEX8(FWBOOT_MARKER_UPDATE, g_marker);
    for (uint32_t off = 0; off < 120; off += 30) {
        expect_reply(&bt, data(&bt, off, img + off, 30), CMD_FW_DATA, 0x01);
    }
    TEST_ASSERT_TRUE(g_flash_writes > 0);          /* Abbild jetzt gemischt */
    TEST_ASSERT_EQUAL_HEX8(PROTO_VER_FLAG_BOOTLOADER, version_flags(&bt));
    /* Master verstummt: beliebig viele Zeitablaeufe, keiner startet die App */
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_INT(FWBOOT_NONE, fwboot_on_timeout(&bt));
    }
    /* Neustart (WDT, Stromausfall): auch dann kein Fenster, keine App */
    boot(&bt, OWN, false);
    TEST_ASSERT_FALSE(bt.window);
    TEST_ASSERT_EQUAL_INT(FWBOOT_NONE, fwboot_on_timeout(&bt));
    TEST_ASSERT_EQUAL_HEX8(PROTO_VER_FLAG_BOOTLOADER, version_flags(&bt));
    boot(&bt, OWN, true);
    TEST_ASSERT_EQUAL_INT(FWBOOT_NONE, fwboot_on_timeout(&bt));
}

static void test_boot_crc_error_never_starts_app(void)
{
    card_with_app();
    uint8_t img[100];
    memset(img, 0x42, sizeof(img));
    fwboot_t bt;
    boot(&bt, OWN, true);
    expect_reply(&bt, begin(&bt, OWN, sizeof(img), 0x1234 /* falsch */), CMD_FW_BEGIN, 0x01);
    for (uint32_t off = 0; off < 100; off += 30) {
        uint8_t n = (uint8_t)((100 - off) < 30 ? (100 - off) : 30);
        expect_reply(&bt, data(&bt, off, img + off, n), CMD_FW_DATA, 0x01);
    }
    expect_reply(&bt, frame(&bt, CMD_FW_END, OWN, NULL, 0), CMD_FW_END, FWUPDATE_ERR_CRC);
    TEST_ASSERT_EQUAL_HEX8(FWBOOT_MARKER_UPDATE, g_marker);
    TEST_ASSERT_EQUAL_INT(FWBOOT_NONE, fwboot_on_timeout(&bt));
    TEST_ASSERT_EQUAL_HEX8(PROTO_VER_FLAG_BOOTLOADER, version_flags(&bt));
    /* weiteres FW_END ohne offene Uebertragung: NAK */
    expect_reply(&bt, frame(&bt, CMD_FW_END, OWN, NULL, 0), CMD_FW_END, 0x00);
}

static void test_boot_full_update_with_lost_acks(void)
{
    card_with_app();
    uint8_t img[150];
    for (int i = 0; i < 150; i++) img[i] = (uint8_t)(i ^ 0xA7);
    const uint16_t crc = crc16(img, sizeof(img));
    fwboot_t bt;
    boot(&bt, OWN, true);
    expect_reply(&bt, begin(&bt, OWN, sizeof(img), crc), CMD_FW_BEGIN, 0x01);
    expect_reply(&bt, begin(&bt, OWN, sizeof(img), crc), CMD_FW_BEGIN, 0x01); /* BEGIN-ACK verloren */
    for (uint32_t off = 0; off < 150; off += 30) {
        expect_reply(&bt, data(&bt, off, img + off, 30), CMD_FW_DATA, 0x01);
        expect_reply(&bt, data(&bt, off, img + off, 30), CMD_FW_DATA, 0x01); /* DATA-ACK verloren */
    }
    expect_reply(&bt, data(&bt, 60, img + 60, 30), CMD_FW_DATA, 0x00);         /* echte Luecke */
    TEST_ASSERT_TRUE(bt.active);                                                /* bleibt offen */
    /* Leerlauf (Master haengt) beendet die Uebertragung nicht */
    TEST_ASSERT_EQUAL_INT(FWBOOT_NONE, fwboot_on_timeout(&bt));
    TEST_ASSERT_TRUE(bt.active);
    fwboot_action_t a = frame(&bt, CMD_FW_END, OWN, NULL, 0);
    TEST_ASSERT_EQUAL_INT(FWBOOT_REPLY_RESET, a);
    TEST_ASSERT_EQUAL_HEX8(CMD_FW_END, bt.reply_cmd);
    TEST_ASSERT_EQUAL_HEX8(0x01, bt.reply[0]);
    TEST_ASSERT_EQUAL_HEX8(FWBOOT_MARKER_VALID, g_marker);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(img, g_flash, sizeof(img));
    TEST_ASSERT_EQUAL_INT(3, g_flash_writes);   /* ceil(150/64), keine Doppelschreibungen */
    /* nach dem Neustart: Fenster, dann neue App */
    boot(&bt, OWN, false);
    TEST_ASSERT_TRUE(bt.window);
    TEST_ASSERT_EQUAL_INT(FWBOOT_START_APP, fwboot_on_timeout(&bt));
}

static void test_boot_begin_restarts_transfer(void)
{
    card_with_app();
    uint8_t img[128];
    for (int i = 0; i < 128; i++) img[i] = (uint8_t)(200 - i);
    const uint16_t crc = crc16(img, sizeof(img));
    fwboot_t bt;
    boot(&bt, OWN, true);
    expect_reply(&bt, begin(&bt, OWN, sizeof(img), crc), CMD_FW_BEGIN, 0x01);
    uint8_t junk[30];
    memset(junk, 0xEE, sizeof(junk));
    for (uint32_t off = 0; off < 90; off += 30) {
        expect_reply(&bt, data(&bt, off, junk, 30), CMD_FW_DATA, 0x01);
    }
    /* Master startet neu (z. B. nach eigenem Neustart): FW_BEGIN mittendrin */
    expect_reply(&bt, begin(&bt, OWN, sizeof(img), crc), CMD_FW_BEGIN, 0x01);
    TEST_ASSERT_EQUAL_UINT32(0, bt.fu.received);
    expect_reply(&bt, data(&bt, 90, img + 90, 30), CMD_FW_DATA, 0x00);  /* alter Stand weg */
    for (uint32_t off = 0; off < 128; off += 30) {
        uint8_t n = (uint8_t)((128 - off) < 30 ? (128 - off) : 30);
        expect_reply(&bt, data(&bt, off, img + off, n), CMD_FW_DATA, 0x01);
    }
    TEST_ASSERT_EQUAL_INT(FWBOOT_REPLY_RESET, frame(&bt, CMD_FW_END, OWN, NULL, 0));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(img, g_flash, sizeof(img));
}

static void test_boot_rejects_bad_frames(void)
{
    card_with_app();
    fwboot_t bt;
    boot(&bt, OWN, true);
    const uint8_t pl[5] = { 64, 0, 0, 0, 0 };
    /* Laenge falsch / nicht Unicast an uns */
    TEST_ASSERT_EQUAL_INT(FWBOOT_IGNORE, frame(&bt, CMD_FW_BEGIN, OWN, pl, 3));
    TEST_ASSERT_EQUAL_INT(FWBOOT_IGNORE, frame(&bt, CMD_FW_BEGIN, OWN, pl, 5));
    TEST_ASSERT_EQUAL_INT(FWBOOT_IGNORE, frame(&bt, CMD_FW_BEGIN, PROTO_ADDR_BROADCAST, pl, 4));
    TEST_ASSERT_EQUAL_INT(FWBOOT_IGNORE, frame(&bt, CMD_FW_BEGIN, PROTO_ADDR_SERVICE, pl, 4));
    TEST_ASSERT_EQUAL_INT(FWBOOT_IGNORE, frame(&bt, CMD_GET_VERSION, PROTO_ADDR_BROADCAST, NULL, 0));
    TEST_ASSERT_EQUAL_INT(FWBOOT_IGNORE, frame(&bt, CMD_FW_DATA, OWN, pl, 2));
    TEST_ASSERT_EQUAL_INT(FWBOOT_IGNORE, frame(&bt, CMD_FW_END, OWN, pl, 1));
    TEST_ASSERT_EQUAL_INT(FWBOOT_IGNORE, frame(&bt, CMD_ENTER_BOOTLOADER, PROTO_ADDR_BROADCAST, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, g_marker_writes);
    /* DATA/END ohne FW_BEGIN: NAK */
    expect_reply(&bt, data(&bt, 0, pl, 3), CMD_FW_DATA, 0x00);
    expect_reply(&bt, frame(&bt, CMD_FW_END, OWN, NULL, 0), CMD_FW_END, 0x00);
    /* zu grosses / leeres Abbild: NAK, App bleibt unberuehrt und startbar */
    expect_reply(&bt, begin(&bt, OWN, BT_APP_MAX + 1u, 0), CMD_FW_BEGIN, 0x00);
    expect_reply(&bt, begin(&bt, OWN, 0, 0), CMD_FW_BEGIN, 0x00);
    TEST_ASSERT_EQUAL_INT(0, g_marker_writes);
    TEST_ASSERT_EQUAL_INT(FWBOOT_START_APP, fwboot_on_timeout(&bt));
}

static void test_boot_unaddressed_card_uses_service_address(void)
{
    memset(g_flash, 0xFF, sizeof(g_flash));   /* nur Bootloader geflasht */
    g_marker = FWBOOT_MARKER_BLANK;
    g_marker_writes = 0;
    g_flash_fail = false;
    fwboot_t bt;
    boot(&bt, 0xFF, false);                   /* EEPROM-Adresse geloescht */
    TEST_ASSERT_EQUAL_UINT8(PROTO_ADDR_SERVICE, bt.addr);
    TEST_ASSERT_FALSE(bt.window);             /* keine App -> warten */
    TEST_ASSERT_EQUAL_INT(FWBOOT_NONE, fwboot_on_timeout(&bt));
    TEST_ASSERT_EQUAL_INT(FWBOOT_IGNORE, frame(&bt, CMD_GET_VERSION, 1, NULL, 0));
    TEST_ASSERT_EQUAL_HEX8(PROTO_VER_FLAG_BOOTLOADER, version_flags(&bt));
    expect_reply(&bt, begin(&bt, PROTO_ADDR_SERVICE, 64, 0), CMD_FW_BEGIN, 0x01);

    boot(&bt, 0, false);
    TEST_ASSERT_EQUAL_UINT8(PROTO_ADDR_SERVICE, bt.addr);
    boot(&bt, 12, false);
    TEST_ASSERT_EQUAL_UINT8(12, bt.addr);
}

static void test_boot_write_error_not_acked_on_retry(void)
{
    card_with_app();
    uint8_t img[64];
    memset(img, 0x5C, sizeof(img));
    fwboot_t bt;
    boot(&bt, OWN, true);
    expect_reply(&bt, begin(&bt, OWN, sizeof(img), crc16(img, sizeof(img))), CMD_FW_BEGIN, 0x01);
    expect_reply(&bt, data(&bt, 0, img, 30), CMD_FW_DATA, 0x01);
    expect_reply(&bt, data(&bt, 30, img + 30, 30), CMD_FW_DATA, 0x01);
    g_flash_fail = true;
    expect_reply(&bt, data(&bt, 60, img + 60, 4), CMD_FW_DATA, 0x00);
    g_flash_fail = false;
    expect_reply(&bt, data(&bt, 60, img + 60, 4), CMD_FW_DATA, 0x00);  /* kein Duplikat-OK */
    expect_reply(&bt, frame(&bt, CMD_FW_END, OWN, NULL, 0), CMD_FW_END, FWUPDATE_ERR_WRITE);
    TEST_ASSERT_EQUAL_INT(FWBOOT_NONE, fwboot_on_timeout(&bt));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_exact_pages);
    RUN_TEST(test_partial_last_page_padded);
    RUN_TEST(test_realistic_5433);
    RUN_TEST(test_crc_mismatch);
    RUN_TEST(test_length_mismatch);
    RUN_TEST(test_out_of_order_rejected);
    RUN_TEST(test_range_exceeded);
    RUN_TEST(test_write_failure_propagates);
    RUN_TEST(test_duplicate_last_chunk_is_idempotent);
    RUN_TEST(test_duplicate_requires_same_offset_and_length);
    RUN_TEST(test_write_failure_is_sticky);
    RUN_TEST(test_boot_app_valid_rules);
    RUN_TEST(test_boot_window_starts_app);
    RUN_TEST(test_boot_forced_waits_then_starts_valid_app);
    RUN_TEST(test_boot_aborted_update_never_starts_app);
    RUN_TEST(test_boot_crc_error_never_starts_app);
    RUN_TEST(test_boot_full_update_with_lost_acks);
    RUN_TEST(test_boot_begin_restarts_transfer);
    RUN_TEST(test_boot_rejects_bad_frames);
    RUN_TEST(test_boot_unaddressed_card_uses_service_address);
    RUN_TEST(test_boot_write_error_not_acked_on_retry);
    return UNITY_END();
}
