/* Siehe moduleupdate.h. */
#include "moduleupdate.h"

#include <string.h>

#include "protocol.h"

/* Zeiten (ms) */
#define T_ACK        1500u   /* Wartezeit auf ein ACK                 */
#define T_BOOT        800u   /* Modul-Reset in den Bootloader         */
#define T_APP        1200u   /* Modul-Reset in die neue App           */
#define MAX_RETRIES     3u

#define ACK_OK 0x01u
#define VER_FLAG_APP_VALID 0x02u   /* == PROTO_VER_FLAG_APP_VALID */

static void send(moduleupdate_t *mu, uint8_t cmd, uint8_t addr,
                 const uint8_t *pl, uint8_t len)
{
    proto_frame_t f;
    f.cmd = cmd;
    f.addr = addr;
    f.payload_len = len;
    for (uint8_t i = 0; i < len; ++i) {
        f.payload[i] = pl[i];
    }
    uint8_t buf[PROTO_MAX_FRAME];
    size_t n = proto_encode(&f, buf, sizeof(buf));
    if (n > 0) {
        mu->tx(mu->tx_ctx, buf, n);
    }
}

void moduleupdate_init(moduleupdate_t *mu,
                       void (*tx)(void *, const uint8_t *, size_t), void *tx_ctx,
                       const uint8_t *image, uint32_t image_len, uint16_t image_crc,
                       uint16_t target_ver)
{
    memset(mu, 0, sizeof(*mu));
    mu->tx = tx;
    mu->tx_ctx = tx_ctx;
    mu->image = image;
    mu->image_len = image_len;
    mu->image_crc = image_crc;
    mu->target_ver = target_ver;
    mu->phase = MU_P_IDLE;
}

void moduleupdate_enqueue(moduleupdate_t *mu, uint32_t addr_mask, uint32_t online_mask)
{
    mu->online = online_mask;
    for (uint8_t a = 1; a <= MU_MAX_ADDR; ++a) {
        const uint32_t bit = 1u << (a - 1u);
        if (!(addr_mask & bit)) {
            continue;
        }
        if (!(online_mask & bit)) {
            mu->result[a] = MU_RES_SKIPPED;
            continue;
        }
        mu->queue |= bit;
        mu->result[a] = MU_RES_QUEUED;
    }
}

/* Phasenwechsel mit Zeitstempel. */
static void go(moduleupdate_t *mu, mu_phase_t p, uint32_t now)
{
    mu->phase = p;
    mu->t_phase = now;
    mu->awaiting = false;
    mu->retries = 0;
}

static void pick_next(moduleupdate_t *mu, uint32_t now)
{
    for (uint8_t a = 1; a <= MU_MAX_ADDR; ++a) {
        if (mu->queue & (1u << (a - 1u))) {
            mu->queue &= ~(1u << (a - 1u));
            mu->cur = a;
            mu->off = 0;
            mu->result[a] = MU_RES_RUNNING;
            go(mu, MU_P_ENTER, now);
            return;
        }
    }
    mu->cur = 0;
    go(mu, MU_P_IDLE, now);
}

static void done_current(moduleupdate_t *mu, bool ok, uint32_t now)
{
    if (mu->cur) {
        mu->result[mu->cur] = ok ? MU_RES_OK : MU_RES_FAILED;
        if (ok) { mu->done_ok++; } else { mu->done_fail++; }
    }
    pick_next(mu, now);
}

/* Retry-Zaehler hochsetzen; true = endgueltig gescheitert. */
static bool retry_exhausted(moduleupdate_t *mu)
{
    return (++mu->retries > MAX_RETRIES);
}

static void send_begin(moduleupdate_t *mu, uint32_t now)
{
    uint8_t pl[4] = {
        (uint8_t)(mu->image_len & 0xFFu), (uint8_t)((mu->image_len >> 8) & 0xFFu),
        (uint8_t)(mu->image_crc & 0xFFu), (uint8_t)((mu->image_crc >> 8) & 0xFFu),
    };
    send(mu, CMD_FW_BEGIN, mu->cur, pl, 4);
    mu->awaiting = true;
    mu->t_phase = now;
}

static void send_data(moduleupdate_t *mu, uint32_t now)
{
    uint8_t pl[2u + MU_CHUNK];
    uint32_t remain = mu->image_len - mu->off;
    uint8_t n = (uint8_t)(remain < MU_CHUNK ? remain : MU_CHUNK);
    pl[0] = (uint8_t)(mu->off & 0xFFu);
    pl[1] = (uint8_t)((mu->off >> 8) & 0xFFu);
    memcpy(&pl[2], &mu->image[mu->off], n);
    send(mu, CMD_FW_DATA, mu->cur, pl, (uint8_t)(2u + n));
    mu->awaiting = true;
    mu->t_phase = now;
}

void moduleupdate_tick(moduleupdate_t *mu, uint32_t now)
{
    if (mu->phase == MU_P_IDLE) {
        if (mu->queue) {
            pick_next(mu, now);
        }
        return;
    }
    if (mu->cur == 0) {
        go(mu, MU_P_IDLE, now);
        return;
    }

    const uint32_t age = now - mu->t_phase;

    switch (mu->phase) {
    case MU_P_ENTER:
        send(mu, CMD_ENTER_BOOTLOADER, mu->cur, NULL, 0);
        go(mu, MU_P_WAIT_BOOT, now);
        break;

    case MU_P_WAIT_BOOT:
        if (age >= T_BOOT) {
            go(mu, MU_P_BEGIN, now);
        }
        break;

    case MU_P_BEGIN:
        if (!mu->awaiting) {
            send_begin(mu, now);
        } else if (age >= T_ACK) {
            if (retry_exhausted(mu)) { done_current(mu, false, now); }
            else { send_begin(mu, now); }
        }
        break;

    case MU_P_DATA:
        if (!mu->awaiting) {
            send_data(mu, now);
        } else if (age >= T_ACK) {
            if (retry_exhausted(mu)) { done_current(mu, false, now); }
            else { send_data(mu, now); }
        }
        break;

    case MU_P_END:
        if (!mu->awaiting || age >= T_ACK) {
            if (mu->awaiting && retry_exhausted(mu)) {
                done_current(mu, false, now);
            } else {
                send(mu, CMD_FW_END, mu->cur, NULL, 0);
                mu->awaiting = true;
                mu->t_phase = now;
            }
        }
        break;

    case MU_P_WAIT_APP:
        if (age >= T_APP) {
            send(mu, CMD_GET_VERSION, mu->cur, NULL, 0);
            go(mu, MU_P_CONFIRM, now);
            mu->awaiting = true;
        }
        break;

    case MU_P_CONFIRM:
        if (age >= T_ACK) {
            if (retry_exhausted(mu)) {
                done_current(mu, false, now);
            } else {
                send(mu, CMD_GET_VERSION, mu->cur, NULL, 0);
                mu->t_phase = now;
            }
        }
        break;

    default:
        break;
    }
}

void moduleupdate_on_frame(moduleupdate_t *mu, uint8_t cmd, uint8_t addr,
                           const uint8_t *payload, uint8_t len, uint32_t now)
{
    if (mu->cur == 0 || addr != mu->cur || !mu->awaiting) {
        return;
    }
    const bool ok = (len >= 1u && payload[0] == ACK_OK);

    switch (mu->phase) {
    case MU_P_BEGIN:
        if (cmd == CMD_FW_BEGIN) {
            if (ok) { go(mu, MU_P_DATA, now); }
            else    { mu->awaiting = false; }
        }
        break;

    case MU_P_DATA:
        if (cmd == CMD_FW_DATA) {
            if (ok) {
                uint32_t remain = mu->image_len - mu->off;
                mu->off += (remain < MU_CHUNK ? remain : MU_CHUNK);
                mu->retries = 0;
                mu->awaiting = false;
                if (mu->off >= mu->image_len) {
                    go(mu, MU_P_END, now);
                }
            } else {
                mu->awaiting = false;
            }
        }
        break;

    case MU_P_END:
        if (cmd == CMD_FW_END) {
            if (ok) { go(mu, MU_P_WAIT_APP, now); }
            else    { mu->awaiting = false; }
        }
        break;

    case MU_P_CONFIRM:
        if (cmd == CMD_GET_VERSION && len >= 4u) {
            const uint16_t ver = (uint16_t)(payload[1] << 8) | payload[2];
            const bool app_ok = (payload[3] & VER_FLAG_APP_VALID) != 0u;
            done_current(mu, app_ok && (mu->target_ver == 0u || ver == mu->target_ver), now);
        }
        break;

    default:
        break;
    }
}

bool moduleupdate_busy(const moduleupdate_t *mu)
{
    return mu->phase != MU_P_IDLE || mu->queue != 0u;
}

uint8_t moduleupdate_progress(const moduleupdate_t *mu)
{
    if (mu->cur == 0 || mu->image_len == 0u) {
        return 0;
    }
    return (uint8_t)((uint32_t)(mu->off / 4u) * 100u / (mu->image_len / 4u + 1u));
}
