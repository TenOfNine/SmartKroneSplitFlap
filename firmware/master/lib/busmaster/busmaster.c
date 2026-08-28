/* Siehe busmaster.h und docs/spezifikation.md 4.5, 5. */
#include "busmaster.h"

#include <string.h>

static void send(busmaster_t *bm, uint8_t cmd, uint8_t addr,
                 const uint8_t *payload, uint8_t len)
{
    proto_frame_t f;
    f.cmd = cmd;
    f.addr = addr;
    f.payload_len = len;
    for (uint8_t i = 0; i < len; ++i) {
        f.payload[i] = payload[i];
    }
    uint8_t buf[PROTO_MAX_FRAME];
    const size_t n = proto_encode(&f, buf, sizeof(buf));
    if (n > 0 && bm->tx != NULL) {
        bm->tx(bm->tx_ctx, buf, n);
    }
}

static void expect(busmaster_t *bm, uint8_t cmd, uint8_t addr, uint32_t now)
{
    bm->pending_cmd = cmd;
    bm->pending_addr = addr;
    bm->awaiting = true;
    bm->sent_ms = now;
    bm->retries = 0;
    proto_parser_reset(&bm->parser);
}

void busmaster_init(busmaster_t *bm,
                    void (*tx)(void *, const uint8_t *, size_t), void *tx_ctx)
{
    memset(bm, 0, sizeof(*bm));
    bm->tx = tx;
    bm->tx_ctx = tx_ctx;
    proto_parser_reset(&bm->parser);
    bm->enum_phase = BM_ENUM_IDLE;
}

/* --- Kommandos ---------------------------------------------------- */

void busmaster_show(busmaster_t *bm, const uint8_t *blaetter, uint8_t count)
{
    if (count > PROTO_MAX_PAYLOAD) {
        count = PROTO_MAX_PAYLOAD;
    }
    send(bm, CMD_SET_ALL, PROTO_ADDR_BROADCAST, blaetter, count);
    for (uint8_t i = 0; i < count && i < BUSMASTER_MAX_MODULES; ++i) {
        bm->mod[i].ziel_blatt = blaetter[i];
    }
    send(bm, CMD_GO, PROTO_ADDR_BROADCAST, NULL, 0);
}

void busmaster_poll_status(busmaster_t *bm, uint8_t addr, uint32_t now_ms)
{
    if (addr < PROTO_ADDR_MIN || addr > PROTO_ADDR_MAX || bm->awaiting) {
        return;
    }
    send(bm, CMD_GET_STATUS, addr, NULL, 0);
    expect(bm, CMD_GET_STATUS, addr, now_ms);
}

void busmaster_home(busmaster_t *bm, uint8_t addr)
{
    send(bm, CMD_HOME, addr, NULL, 0);
}

void busmaster_stop(busmaster_t *bm, uint8_t addr)
{
    send(bm, CMD_STOP, addr, NULL, 0);
}

void busmaster_set_config(busmaster_t *bm, uint8_t addr, uint8_t blattzahl,
                          uint8_t offset, uint8_t vorhalt, uint8_t flags)
{
    const uint8_t pl[4] = { blattzahl, offset, vorhalt, flags };
    send(bm, CMD_SET_CONFIG, addr, pl, sizeof(pl));
}

/* --- Enumeration ------------------------------------------------- */

void busmaster_start_enumeration(busmaster_t *bm, uint32_t now_ms)
{
    for (uint8_t i = 0; i < BUSMASTER_MAX_MODULES; ++i) {
        bm->mod[i].online = false;
    }
    bm->chain_active = true;
    send(bm, CMD_ENUM_RESET, PROTO_ADDR_BROADCAST, NULL, 0);
    bm->enum_phase = BM_ENUM_RESET_SENT;
    bm->enum_next_addr = PROTO_ADDR_MIN;
    bm->enum_step_ms = now_ms;
    bm->awaiting = false;
}

bool busmaster_enum_busy(const busmaster_t *bm)
{
    return bm->enum_phase != BM_ENUM_IDLE && bm->enum_phase != BM_ENUM_DONE;
}

static void enum_send_assign(busmaster_t *bm, uint32_t now)
{
    const uint8_t pl[1] = { bm->enum_next_addr };
    send(bm, CMD_ENUM_ASSIGN, PROTO_ADDR_BROADCAST, pl, 1);
    bm->enum_phase = BM_ENUM_ASSIGNING;
    bm->enum_step_ms = now;
    expect(bm, CMD_ENUM_ASSIGN, bm->enum_next_addr, now);
}

static void enum_finish(busmaster_t *bm)
{
    send(bm, CMD_ENUM_DONE, PROTO_ADDR_BROADCAST, NULL, 0);
    bm->chain_active = false;
    bm->module_count = (uint8_t)(bm->enum_next_addr - 1u);
    bm->enum_phase = BM_ENUM_DONE;
    bm->awaiting = false;
}

/* --- Empfang ---------------------------------------------------- */

static void apply_status(busmaster_t *bm, uint8_t addr, const uint8_t *p, uint8_t len)
{
    if (addr < PROTO_ADDR_MIN || addr > BUSMASTER_MAX_MODULES || len < 8) {
        return;
    }
    bm_module_t *m = &bm->mod[addr - 1u];
    m->online = true;
    m->miss_count = 0;
    m->ist_blatt = p[0];
    m->ziel_blatt = p[1];
    m->zustand = p[2];
    m->fehler = p[3];
    m->blattzahl = p[4];
    m->korrektur = (uint16_t)p[5] | ((uint16_t)p[6] << 8);
    m->fw_version = p[7];
}

void busmaster_on_rx_byte(busmaster_t *bm, uint8_t byte, uint32_t now_ms)
{
    if (proto_parser_feed(&bm->parser, byte) != PARSE_FRAME_OK) {
        return;
    }
    const proto_frame_t *f = &bm->parser.frame;

    if (!bm->awaiting) {
        return;
    }

    if (bm->pending_cmd == CMD_ENUM_ASSIGN && f->cmd == CMD_ENUM_ASSIGN) {
        /* ACK der Karte, die die Adresse uebernommen hat. */
        if (bm->enum_next_addr - 1u < BUSMASTER_MAX_MODULES) {
            bm->mod[bm->enum_next_addr - 1u].online = true;
        }
        bm->enum_next_addr++;
        bm->awaiting = false;
        if (bm->enum_next_addr > PROTO_ADDR_MAX) {
            enum_finish(bm);
        } else {
            enum_send_assign(bm, now_ms);
        }
        return;
    }

    if (f->cmd == bm->pending_cmd && f->addr == bm->pending_addr) {
        if (f->cmd == CMD_GET_STATUS) {
            apply_status(bm, f->addr, f->payload, f->payload_len);
        }
        bm->awaiting = false;
    }
}

/* --- Zeitfortschritt ------------------------------------------- */

void busmaster_tick(busmaster_t *bm, uint32_t now_ms)
{
    if (bm->enum_phase == BM_ENUM_RESET_SENT) {
        /* kurze Pause nach ENUM_RESET, dann die erste Adresse vergeben */
        if ((uint32_t)(now_ms - bm->enum_step_ms) >= 2u) {
            enum_send_assign(bm, now_ms);
        }
        return;
    }

    if (!bm->awaiting) {
        return;
    }

    if ((uint32_t)(now_ms - bm->sent_ms) < BUSMASTER_TIMEOUT_MS) {
        return;
    }

    /* Timeout. */
    if (bm->pending_cmd == CMD_ENUM_ASSIGN) {
        /* keine Karte mehr in der Kette -> Enumeration abschliessen */
        enum_finish(bm);
        return;
    }

    if (bm->retries < BUSMASTER_RETRIES) {
        bm->retries++;
        bm->sent_ms = now_ms;
        proto_parser_reset(&bm->parser);
        send(bm, bm->pending_cmd, bm->pending_addr, NULL, 0);
        return;
    }

    /* endgueltig kein Empfang: Modul offline markieren */
    if (bm->pending_addr >= PROTO_ADDR_MIN &&
        bm->pending_addr <= BUSMASTER_MAX_MODULES) {
        bm_module_t *m = &bm->mod[bm->pending_addr - 1u];
        if (m->miss_count < 255) {
            m->miss_count++;
        }
        if (m->miss_count >= 3) {
            m->online = false;
        }
    }
    bm->awaiting = false;
}
