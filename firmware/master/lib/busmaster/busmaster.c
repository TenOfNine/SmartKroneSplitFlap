/* Siehe busmaster.h und docs/spezifikation.md 4.5, 5. */
#include "busmaster.h"

#include <string.h>

/* Flags der Warteschlangeneintraege. */
#define BMQ_SHOW      0x01u   /* SET_ALL + GO (Anzeige), wird koalesziert   */
#define BMQ_RECONCILE 0x02u   /* Soll/Ist-Korrektur (SET bzw. GO)           */

/* Fehlercode 0x06 im Statusbyte 3 (Adresskollision, Spez. 6.4). */
#define BM_ERR_COLLISION 0x06u

/* --- Hilfen --------------------------------------------------------- */

static uint32_t bm_now(const busmaster_t *bm, uint32_t fallback)
{
    return bm->now_fn != NULL ? bm->now_fn(bm->now_ctx) : fallback;
}

static bool elapsed_ge(uint32_t now, uint32_t since, uint32_t ms)
{
    return (uint32_t)(now - since) >= ms;
}

static bool valid_unicast(uint8_t addr)
{
    return addr >= PROTO_ADDR_MIN && addr <= PROTO_ADDR_MAX;
}

static bm_module_t *mod_of(busmaster_t *bm, uint8_t addr)
{
    if (addr < PROTO_ADDR_MIN || addr > BUSMASTER_MAX_MODULES) {
        return NULL;
    }
    return &bm->mod[addr - 1u];
}

/* Steht der Parser mitten in einem Rahmen? protocol.h bietet dafuer keine
 * Abfrage; gelesen wird nur der Fortschritt (pos/sync), nichts veraendert. */
static bool parser_mid_frame(const proto_parser_t *p)
{
    return p->pos != 0u || p->sync != 0u;
}

static void bm_log(busmaster_t *bm, const char *event, uint8_t cmd, uint8_t addr,
                   uint32_t now)
{
    if (bm->log_fn != NULL) {
        bm->log_fn(bm->log_ctx, event, cmd, addr, now, bm->sent_ms, bm->retries);
    }
}

static void raise_warn(busmaster_t *bm, uint8_t bit)
{
    bm->warn |= bit;
    bm->warn_seq++;
}

/* --- Busbelegung ---------------------------------------------------- */

/* Busruhe: seit dem letzten Byte mindestens BUSMASTER_QUIET_MS und kein
 * angefangener Rahmen (ausser einem abgerissenen, siehe FRAME_STALE). */
static bool line_quiet(const busmaster_t *bm, uint32_t now)
{
    if (!bm->rx_seen) {
        return true;
    }
    const uint32_t since = (uint32_t)(now - bm->last_rx_ms);
    if (since < BUSMASTER_QUIET_MS) {
        return false;
    }
    if (parser_mid_frame(&bm->parser) && since < BUSMASTER_FRAME_STALE_MS) {
        return false;
    }
    return true;
}

static bool hold_over(const busmaster_t *bm, uint32_t now)
{
    return !bm->hold_active || (int32_t)(now - bm->hold_until_ms) >= 0;
}

static bool bus_free(const busmaster_t *bm, uint32_t now)
{
    return !bm->awaiting && hold_over(bm, now) && line_quiet(bm, now);
}

static void housekeeping(busmaster_t *bm, uint32_t now)
{
    if (bm->rx_seen && parser_mid_frame(&bm->parser) &&
        elapsed_ge(now, bm->last_rx_ms, BUSMASTER_FRAME_STALE_MS)) {
        proto_parser_reset(&bm->parser);   /* abgerissener Rahmen */
    }
    if (bm->hold_active && hold_over(bm, now)) {
        bm->hold_active = false;
    }
}

/* --- Senden --------------------------------------------------------- */

/* Einen Rahmen senden; danach gilt die Uhrzeit (bzw. now) als Sendeende. */
static void tx_frame(busmaster_t *bm, uint8_t cmd, uint8_t addr,
                     const uint8_t *payload, uint8_t len, uint32_t now)
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
    bm->last_tx_end_ms = bm_now(bm, now);
}

/* Anfrage senden und Antwort erwarten. frame_addr ist die Zieladresse des
 * Rahmens, reply_addr die erwartete Absenderadresse der Antwort. */
static void request(busmaster_t *bm, bm_tx_kind_t kind, uint8_t cmd,
                    uint8_t frame_addr, uint8_t reply_addr,
                    const uint8_t *payload, uint8_t len, uint8_t max_retries,
                    uint32_t now)
{
    proto_parser_reset(&bm->parser);
    tx_frame(bm, cmd, frame_addr, payload, len, now);

    bm->awaiting = true;
    bm->pending_kind = (uint8_t)kind;
    bm->pending_cmd = cmd;
    bm->pending_addr = reply_addr;
    bm->pending_len = len > sizeof(bm->pending_payload)
                          ? (uint8_t)sizeof(bm->pending_payload) : len;
    for (uint8_t i = 0; i < bm->pending_len; ++i) {
        bm->pending_payload[i] = payload[i];
    }
    bm->pending_max_retries = max_retries;
    bm->retries = 0;
    bm->sent_ms = bm->last_tx_end_ms;
    bm->defer_active = false;
    bm->txn_crc_snapshot = bm->crc_errors;
    bm_log(bm, "send", cmd, reply_addr, now);
}

/* --- Warteschlange ---------------------------------------------------- */

static void q_remove_at(busmaster_t *bm, uint8_t i)
{
    if (i >= bm->queue_count) {
        return;
    }
    for (uint8_t k = i; k + 1u < bm->queue_count; ++k) {
        bm->queue[k] = bm->queue[k + 1u];
    }
    bm->queue_count--;
}

/* Entfernt alle Eintraege mit einem der Flags; addr 0 = jede Adresse. */
static void q_remove(busmaster_t *bm, uint8_t flags, uint8_t cmd, uint8_t addr)
{
    for (uint8_t i = bm->queue_count; i > 0u; --i) {
        const bm_queue_entry_t *e = &bm->queue[i - 1u];
        if ((e->flags & flags) == 0u) {
            continue;
        }
        if (cmd != 0u && e->cmd != cmd) {
            continue;
        }
        if (addr != 0u && e->addr != addr) {
            continue;
        }
        q_remove_at(bm, (uint8_t)(i - 1u));
    }
}

static bool q_has_flags(const busmaster_t *bm, uint8_t flags)
{
    for (uint8_t i = 0; i < bm->queue_count; ++i) {
        if ((bm->queue[i].flags & flags) != 0u) {
            return true;
        }
    }
    return false;
}

/* Einreihen; ein identischer, noch nicht gesendeter Eintrag zaehlt als
 * bereits eingereiht. false bei voller Warteschlange. */
static bool q_push(busmaster_t *bm, uint8_t cmd, uint8_t addr,
                   const uint8_t *payload, uint8_t len, uint8_t flags)
{
    if (len > PROTO_MAX_PAYLOAD) {
        len = PROTO_MAX_PAYLOAD;
    }
    for (uint8_t i = 0; i < bm->queue_count; ++i) {
        const bm_queue_entry_t *e = &bm->queue[i];
        if (e->cmd == cmd && e->addr == addr && e->len == len && e->flags == flags &&
            (len == 0u || memcmp(e->payload, payload, len) == 0)) {
            return true;
        }
    }
    if (bm->queue_count >= BUSMASTER_QUEUE_LEN) {
        bm->queue_drops++;
        return false;
    }
    bm_queue_entry_t *e = &bm->queue[bm->queue_count++];
    e->cmd = cmd;
    e->addr = addr;
    e->len = len;
    e->flags = flags;
    for (uint8_t i = 0; i < len; ++i) {
        e->payload[i] = payload[i];
    }
    return true;
}

/* Antwortart eines eingereihten Kommandos (Kommandotabelle 5.4). */
static bm_tx_kind_t kind_for(uint8_t cmd, uint8_t addr)
{
    if (addr == PROTO_ADDR_BROADCAST) {
        return BM_TX_NONE;
    }
    switch (cmd) {
    case CMD_GET_STATUS:  return BM_TX_STATUS;
    case CMD_GET_VERSION: return BM_TX_VERSION;
    case CMD_GET_CONFIG:  return BM_TX_CONFIG;
    case CMD_GET_UID:     return BM_TX_UID;
    case CMD_SET:
    case CMD_STOP:
    case CMD_HOME:
    case CMD_IDENTIFY:
    case CMD_SET_CONFIG:  return BM_TX_ACK;
    default:              return BM_TX_NONE;
    }
}

/* Eintraege senden, solange der Bus frei ist; bei einem Kommando mit
 * Antwort wird danach auf diese gewartet. */
static void run_queue(busmaster_t *bm, uint32_t now)
{
    while (bm->queue_count > 0u && bus_free(bm, now)) {
        const bm_queue_entry_t e = bm->queue[0];
        q_remove_at(bm, 0);

        if ((e.flags & BMQ_SHOW) != 0u) {
            tx_frame(bm, CMD_SET_ALL, PROTO_ADDR_BROADCAST, e.payload, e.len, now);
            tx_frame(bm, CMD_GO, PROTO_ADDR_BROADCAST, NULL, 0, now);
            continue;
        }
        const bm_tx_kind_t k = kind_for(e.cmd, e.addr);
        if (k == BM_TX_NONE) {
            tx_frame(bm, e.cmd, e.addr, e.payload, e.len, now);
            if (e.cmd == CMD_LED_SYNC) {
                bm->led_sync_ms = bm->last_tx_end_ms;
            }
            continue;
        }
        request(bm, k, e.cmd, e.addr, e.addr, e.payload, e.len, BUSMASTER_RETRIES, now);
        return;
    }
}

/* --- Soll/Ist-Abgleich (#26) ------------------------------------------ */

static void reconcile_check(busmaster_t *bm, uint8_t addr, bm_module_t *m, uint32_t now)
{
    if (!m->soll_valid || m->rc_stopped) {
        return;
    }
    if (m->zustand != 0u || m->fehler != 0u || m->ist_blatt == 0u) {
        return;   /* nur im fehlerfreien Stillstand mit bekannter Lage */
    }
    if (m->ist_blatt == m->soll && m->ziel_blatt == m->soll) {
        m->rc_tries = 0;   /* am Soll: Versuche wieder frei */
        return;
    }
    if (q_has_flags(bm, BMQ_SHOW)) {
        return;   /* neuer Inhalt noch nicht gesendet: Status ist veraltet */
    }
    if (m->rc_tries >= BUSMASTER_RECONCILE_MAX_TRIES) {
        return;
    }
    if (m->rc_tries > 0u && !elapsed_ge(now, m->rc_last_ms, BUSMASTER_RECONCILE_MIN_MS)) {
        return;
    }
    const uint8_t pl[1] = { m->soll };
    if (!q_push(bm, CMD_SET, addr, pl, 1, BMQ_RECONCILE)) {
        return;
    }
    if (m->ist_blatt != m->soll) {
        /* ein GO hinter alle eingereihten Korrekturen */
        q_remove(bm, BMQ_RECONCILE, CMD_GO, 0);
        (void)q_push(bm, CMD_GO, PROTO_ADDR_BROADCAST, NULL, 0, BMQ_RECONCILE);
    }
    m->rc_tries++;
    m->rc_last_ms = now;
    bm->reconciles++;
}

/* --- Enumeration -------------------------------------------------------- */

/* Die Adresse enum_next_addr ist vergeben (ACK, spaetes ACK oder PING). */
static void enum_confirmed(busmaster_t *bm)
{
    if (bm->enum_next_addr > BUSMASTER_MAX_MODULES) {
        /* Karte jenseits der Auslegung: nicht verwalten, warnen. */
        raise_warn(bm, BM_WARN_ENUM_LIMIT);
        bm->enum_phase = BM_ENUM_FINISHING;
        return;
    }
    bm->enum_next_addr++;
    bm->enum_assign_repeat = 0;
    bm->enum_phase = BM_ENUM_ASSIGNING;
}

static void enum_begin(busmaster_t *bm, uint32_t now)
{
    tx_frame(bm, CMD_ENUM_RESET, PROTO_ADDR_BROADCAST, NULL, 0, now);
    bm->chain_active = true;
    bm->enum_phase = BM_ENUM_RESET_SENT;
    bm->enum_resets_sent = 1;
    bm->enum_step_ms = bm->last_tx_end_ms;
    bm->enum_next_addr = PROTO_ADDR_MIN;
    bm->enum_assign_repeat = 0;
    bm->warn &= (uint8_t)~(BM_WARN_ENUM_LIMIT | BM_WARN_ENUM_GAP);
}

/* Modulzahl und Online-Stand festschreiben, Verifikationslauf beginnen. */
static void enum_commit(busmaster_t *bm)
{
    const uint8_t count = bm->enum_found;
    bm->module_count = count;
    for (uint8_t i = 0; i < BUSMASTER_MAX_MODULES; ++i) {
        bm_module_t *m = &bm->mod[i];
        m->online = (i < bm->enum_chain_count) ||
                    (i < count && (bm->enum_probe_hits & (1uL << i)) != 0u);
        if (m->online) {
            m->miss_count = 0;
        }
    }
    if (count > bm->enum_chain_count) {
        raise_warn(bm, BM_WARN_ENUM_GAP);
    }
    bm->poll_cursor = 1;
    bm->round_end_pending = false;
    bm->enum_phase = BM_ENUM_VERIFYING;
    bm->enum_uid_addr = PROTO_ADDR_MIN;
    bm->enum_svc_done = false;
}

static void enum_finish(busmaster_t *bm, uint32_t now)
{
    tx_frame(bm, CMD_ENUM_DONE, PROTO_ADDR_BROADCAST, NULL, 0, now);
    bm->chain_active = false;

    uint8_t count = (uint8_t)(bm->enum_next_addr - 1u);
    if (count > BUSMASTER_MAX_MODULES) {
        count = BUSMASTER_MAX_MODULES;
    }
    bm->enum_chain_count = count;
    bm->enum_found = count;
    bm->enum_probe_hits = 0;
    for (uint8_t i = 0; i < BUSMASTER_MAX_MODULES; ++i) {
        bm_module_t *m = &bm->mod[i];
        m->fw_version = 0;
        m->app_ver = 0;
        m->ver_flags = 0;
        m->ver_known = false;
        m->ver_fails = 0;
        m->cfg_known = false;
        m->uid_fresh = false;
        m->uid_dup = false;
        m->uid_changed = false;
    }

    /* Kette kuerzer als erwartet: Karten hinter einer toten Karte sind nach
     * ENUM_DONE unter ihrer EEPROM-Adresse erreichbar (4.5.2). */
    uint8_t limit = bm->module_count > bm->enum_hint ? bm->module_count : bm->enum_hint;
    if (limit > BUSMASTER_MAX_MODULES) {
        limit = BUSMASTER_MAX_MODULES;
    }
    if (count < limit) {
        bm->enum_probe_addr = (uint8_t)(count + 1u);
        bm->enum_probe_limit = limit;
        bm->enum_phase = BM_ENUM_PROBING;
        return;
    }
    enum_commit(bm);
}

/* Ende des Verifikationslaufs: Dubletten und Wechsel bewerten (4.5.4). */
static void enum_evaluate_uids(busmaster_t *bm)
{
    bool dup = false;
    bool changed = false;
    for (uint8_t i = 0; i < bm->module_count; ++i) {
        bm_module_t *a = &bm->mod[i];
        dup = dup || a->uid_dup;   /* gestoerte GET_UID-Antworten */
        if (!a->uid_fresh) {
            continue;
        }
        changed = changed || a->uid_changed;
        for (uint8_t j = 0; j < i; ++j) {
            bm_module_t *b = &bm->mod[j];
            if (b->uid_fresh && memcmp(a->uid, b->uid, BUSMASTER_UID_LEN) == 0) {
                a->uid_dup = true;
                b->uid_dup = true;
                dup = true;
            }
        }
    }
    bm->warn &= (uint8_t)~(BM_WARN_UID_DUP | BM_WARN_UID_CHANGED);
    if (dup) {
        raise_warn(bm, BM_WARN_UID_DUP);
    }
    if (changed) {
        raise_warn(bm, BM_WARN_UID_CHANGED);
    }
}

static void start_service_probe(busmaster_t *bm, uint32_t now)
{
    bm->last_service_probe_ms = now;
    request(bm, BM_TX_SERVICE, CMD_PING, PROTO_ADDR_SERVICE, PROTO_ADDR_SERVICE,
            NULL, 0, 0, now);
}

/* Ein Enumerationsschritt bei freiem Bus. */
static void enum_step(busmaster_t *bm, uint32_t now)
{
    switch (bm->enum_phase) {
    case BM_ENUM_REQUESTED:
        enum_begin(bm, now);
        break;

    case BM_ENUM_RESET_SENT:
        if (!elapsed_ge(now, bm->enum_step_ms, BUSMASTER_ENUM_RESET_GAP_MS)) {
            break;
        }
        if (bm->enum_resets_sent < BUSMASTER_ENUM_RESET_COUNT) {
            tx_frame(bm, CMD_ENUM_RESET, PROTO_ADDR_BROADCAST, NULL, 0, now);
            bm->enum_resets_sent++;
            bm->enum_step_ms = bm->last_tx_end_ms;
            break;
        }
        bm->enum_phase = BM_ENUM_ASSIGNING;
        /* fall through */
    case BM_ENUM_ASSIGNING: {
        const uint8_t pl[1] = { bm->enum_next_addr };
        request(bm, BM_TX_ENUM_ASSIGN, CMD_ENUM_ASSIGN, PROTO_ADDR_BROADCAST,
                bm->enum_next_addr, pl, 1, 0, now);
        break;
    }

    case BM_ENUM_CHECKING:
        /* Hat eine Karte die Adresse uebernommen und nur das ACK ging
         * verloren? Dann darf ENUM_ASSIGN nicht wiederholt werden, sonst
         * uebernaehme die naechste Karte dieselbe Adresse (#22). */
        request(bm, BM_TX_ENUM_PING, CMD_PING, bm->enum_next_addr, bm->enum_next_addr,
                NULL, 0, BUSMASTER_RETRIES, now);
        break;

    case BM_ENUM_FINISHING:
        enum_finish(bm, now);
        break;

    case BM_ENUM_PROBING:
        if (bm->enum_probe_addr <= bm->enum_probe_limit) {
            request(bm, BM_TX_ENUM_PROBE, CMD_PING, bm->enum_probe_addr,
                    bm->enum_probe_addr, NULL, 0, 1, now);
        } else {
            enum_commit(bm);
        }
        break;

    case BM_ENUM_VERIFYING:
        if (bm->enum_uid_addr <= bm->module_count) {
            request(bm, BM_TX_UID, CMD_GET_UID, bm->enum_uid_addr, bm->enum_uid_addr,
                    NULL, 0, BUSMASTER_RETRIES, now);
        } else if (!bm->enum_svc_done) {
            start_service_probe(bm, now);
        } else {
            enum_evaluate_uids(bm);
            bm->enum_phase = BM_ENUM_DONE;
        }
        break;

    case BM_ENUM_IDLE:
    case BM_ENUM_DONE:
    default:
        break;
    }
}

/* --- Empfang und Abschluss einer Anfrage ------------------------------ */

static void apply_status(busmaster_t *bm, uint8_t addr, const uint8_t *p, uint32_t now)
{
    bm_module_t *m = mod_of(bm, addr);
    if (m == NULL) {
        return;
    }
    m->online = true;
    m->miss_count = 0;
    m->ist_blatt = p[0];
    m->ziel_blatt = p[1];
    m->zustand = p[2];
    m->fehler = p[3];
    m->blattzahl = p[4];
    m->korrektur = (uint16_t)p[5] | ((uint16_t)p[6] << 8);
    m->fw_version = p[7];
    if (p[3] == BM_ERR_COLLISION) {
        if (m->collisions < 0xFFFFu) {
            m->collisions++;
        }
        raise_warn(bm, BM_WARN_COLLISION);
    }
    reconcile_check(bm, addr, m, now);
}

static bool frame_matches(const busmaster_t *bm, const proto_frame_t *f)
{
    const uint8_t a = bm->pending_addr;
    /* Antworten unterscheiden sich vom (bei /RE auf GND moeglichen) eigenen
     * Echo an der Payload-Laenge. Ausnahme: STOP/HOME (beide leer); im
     * RS485-Halbduplexbetrieb des ESP32-C3 gibt es kein Echo. */
    switch ((bm_tx_kind_t)bm->pending_kind) {
    case BM_TX_STATUS:
        return f->cmd == CMD_GET_STATUS && f->addr == a && f->payload_len >= 8u;
    case BM_TX_VERSION:
        return f->cmd == CMD_GET_VERSION && f->addr == a && f->payload_len >= 5u;
    case BM_TX_CONFIG:
        return f->cmd == CMD_GET_CONFIG && f->addr == a && f->payload_len >= 4u;
    case BM_TX_ACK:
        return f->cmd == bm->pending_cmd && f->addr == a && f->payload_len == 0u;
    case BM_TX_ENUM_ASSIGN:
        return f->cmd == CMD_ENUM_ASSIGN && f->addr == a && f->payload_len == 0u;
    case BM_TX_ENUM_PING:
        return (f->cmd == CMD_PING && f->addr == a && f->payload_len >= 1u) ||
               (f->cmd == CMD_ENUM_ASSIGN && f->addr == a && f->payload_len == 0u);
    case BM_TX_UID:
        return f->cmd == CMD_GET_UID && f->addr == a &&
               f->payload_len >= BUSMASTER_UID_LEN;
    case BM_TX_SERVICE:
        return f->cmd == CMD_PING && f->addr == PROTO_ADDR_SERVICE &&
               f->payload_len >= 1u;
    case BM_TX_ENUM_PROBE:
        return f->cmd == CMD_PING && f->addr == a && f->payload_len >= 1u;
    case BM_TX_NONE:
    default:
        return false;
    }
}

/* Anfrage abschliessen: f = Antwort, NULL = endgueltig ohne Antwort. */
static void complete(busmaster_t *bm, const proto_frame_t *f, uint32_t now)
{
    const bm_tx_kind_t kind = (bm_tx_kind_t)bm->pending_kind;
    const uint8_t addr = bm->pending_addr;
    bm_module_t *m = mod_of(bm, addr);

    bm_log(bm, f != NULL ? "match" : "give_up", bm->pending_cmd, addr, now);
    if (f != NULL && bm->retries > 0u) {
        /* Antwort auf die fruehere Anfrage? Dann kommt die auf die
         * Wiederholung womoeglich noch. */
        bm->hold_active = true;
        bm->hold_until_ms = bm->last_tx_end_ms + BUSMASTER_LATE_GUARD_MS;
    }
    bm->awaiting = false;
    bm->pending_kind = BM_TX_NONE;
    bm->defer_active = false;

    switch (kind) {
    case BM_TX_STATUS:
        if (f != NULL) {
            apply_status(bm, addr, f->payload, now);
        } else {
            bm->timeouts++;
            if (m != NULL) {
                if (m->miss_count < 255u) {
                    m->miss_count++;
                }
                if (m->miss_count >= 3u) {
                    m->online = false;
                }
            }
        }
        if (bm->round_end_pending) {
            bm->round_end_pending = false;
            bm->round_done = true;
        }
        break;

    case BM_TX_VERSION:
        if (m == NULL) {
            break;
        }
        if (f != NULL) {
            m->app_ver = (uint16_t)((f->payload[1] << 8) | f->payload[2]);
            m->ver_flags = f->payload[3];
            m->ver_known = true;
            m->ver_fails = 0;
        } else {
            bm->timeouts++;
            if (m->ver_fails < 255u) {
                m->ver_fails++;
            }
        }
        break;

    case BM_TX_CONFIG:
        if (f == NULL) {
            bm->timeouts++;
        } else if (m != NULL) {
            m->cfg_blattzahl = f->payload[0];
            m->cfg_offset    = f->payload[1];
            m->cfg_vorhalt   = f->payload[2];
            m->cfg_flags     = f->payload[3];
            m->cfg_known     = true;
        }
        break;

    case BM_TX_ACK:
        if (f == NULL) {
            bm->timeouts++;   /* ohne Offline-Zaehlung */
        }
        break;

    case BM_TX_ENUM_ASSIGN:
        if (f != NULL) {
            enum_confirmed(bm);
        } else {
            bm->enum_phase = BM_ENUM_CHECKING;
        }
        break;

    case BM_TX_ENUM_PING:
        if (f != NULL) {
            enum_confirmed(bm);
        } else if (bm->enum_assign_repeat == 0u) {
            bm->enum_assign_repeat = 1;          /* ENUM_ASSIGN einmal wiederholen */
            bm->enum_phase = BM_ENUM_ASSIGNING;
        } else {
            bm->enum_phase = BM_ENUM_FINISHING;  /* Kettenende */
        }
        break;

    case BM_TX_UID:
        if (f == NULL) {
            bm->timeouts++;
            if (m != NULL && bm->crc_errors != bm->txn_crc_snapshot) {
                m->uid_dup = true;   /* gestoerte Antworten: mehrere Karten? */
            }
        } else if (m != NULL) {
            if (m->uid_known && memcmp(m->uid, f->payload, BUSMASTER_UID_LEN) != 0) {
                m->uid_changed = true;
            }
            memcpy(m->uid, f->payload, BUSMASTER_UID_LEN);
            m->uid_known = true;
            m->uid_fresh = true;
        }
        if (bm->enum_phase == BM_ENUM_VERIFYING) {
            bm->enum_uid_addr++;
        }
        break;

    case BM_TX_SERVICE: {
        /* Antwort oder gestoerter Empfang (mehrere Karten auf 250) */
        const bool hit = (f != NULL) || (bm->crc_errors != bm->txn_crc_snapshot);
        if (hit) {
            if ((bm->warn & BM_WARN_SERVICE_ADDR) == 0u) {
                raise_warn(bm, BM_WARN_SERVICE_ADDR);
            }
        } else {
            bm->warn &= (uint8_t)~BM_WARN_SERVICE_ADDR;
        }
        if (bm->enum_phase == BM_ENUM_VERIFYING) {
            bm->enum_svc_done = true;
        }
        break;
    }

    case BM_TX_ENUM_PROBE:
        if (f != NULL && addr >= PROTO_ADDR_MIN && addr <= BUSMASTER_MAX_MODULES) {
            bm->enum_probe_hits |= (1uL << (addr - 1u));
            bm->enum_found = addr;   /* aufsteigend: hoechste antwortende */
        }
        bm->enum_probe_addr++;
        break;

    case BM_TX_NONE:
    default:
        break;
    }
}

void busmaster_on_rx_byte(busmaster_t *bm, uint8_t byte, uint32_t now_ms)
{
    const uint32_t now = bm_now(bm, now_ms);
    bm->last_rx_ms = now;
    bm->rx_seen = true;

    const proto_parse_result_t pr = proto_parser_feed(&bm->parser, byte);
    if (pr == PARSE_ERR_CRC) {
        bm->crc_errors++;
        return;
    }
    if (pr != PARSE_FRAME_OK) {
        return;
    }
    const proto_frame_t *f = &bm->parser.frame;

    if (!bm->awaiting) {
        /* Spaetes ENUM_ASSIGN-ACK, bevor der PING hinaus ist. */
        if (bm->enum_phase == BM_ENUM_CHECKING && f->cmd == CMD_ENUM_ASSIGN &&
            f->addr == bm->enum_next_addr && f->payload_len == 0u) {
            enum_confirmed(bm);
        }
        return;
    }
    if (frame_matches(bm, f)) {
        complete(bm, f, now);
    }
}

void busmaster_note_activity(busmaster_t *bm, uint32_t now_ms)
{
    bm->last_rx_ms = bm_now(bm, now_ms);
    bm->rx_seen = true;
}

/* --- Zeitfortschritt -------------------------------------------------- */

static void handle_timeout(busmaster_t *bm, uint32_t now)
{
    if ((uint32_t)(now - bm->sent_ms) <= BUSMASTER_TIMEOUT_MS) {
        return;
    }
    if (!line_quiet(bm, now)) {
        /* Empfang laeuft: nicht hineinsenden, Wiederholung verschieben. */
        if (!bm->defer_active) {
            bm->defer_active = true;
            bm->defer_since_ms = now;
            bm_log(bm, "defer", bm->pending_cmd, bm->pending_addr, now);
        }
        if (!elapsed_ge(now, bm->defer_since_ms, BUSMASTER_RETRY_DEFER_MAX_MS)) {
            return;
        }
        complete(bm, NULL, now);   /* Obergrenze: aufgeben statt senden */
        return;
    }
    bm->defer_active = false;

    if (bm->retries < bm->pending_max_retries) {
        bm->retries++;
        bm_log(bm, "retry", bm->pending_cmd, bm->pending_addr, now);
        proto_parser_reset(&bm->parser);
        tx_frame(bm, bm->pending_cmd, bm->pending_addr, bm->pending_payload,
                 bm->pending_len, now);
        bm->sent_ms = bm->last_tx_end_ms;
        return;
    }
    complete(bm, NULL, now);
}

void busmaster_tick(busmaster_t *bm, uint32_t now_ms)
{
    const uint32_t now = bm_now(bm, now_ms);
    housekeeping(bm, now);

    if (bm->awaiting) {
        handle_timeout(bm, now);
        if (bm->awaiting) {
            return;
        }
    }
    if (!bus_free(bm, now)) {
        return;
    }
    if (busmaster_enum_busy(bm)) {
        enum_step(bm, now);
        return;
    }
    run_queue(bm, now);
}

/* --- Abfrageplan ------------------------------------------------------ */

bool busmaster_service_poll(busmaster_t *bm, uint8_t count, uint32_t now_ms)
{
    const uint32_t now = bm_now(bm, now_ms);
    const bool done = bm->round_done;
    bm->round_done = false;

    housekeeping(bm, now);
    if (count > BUSMASTER_MAX_MODULES) {
        count = BUSMASTER_MAX_MODULES;
    }
    if (busmaster_enum_busy(bm) || bm->queue_count > 0u || !bus_free(bm, now)) {
        return done;
    }

    /* 1. Statusabfrage, rundlaufend, hat Vorrang */
    if (count > 0u && elapsed_ge(now, bm->last_status_poll_ms, BUSMASTER_POLL_INTERVAL_MS)) {
        if (bm->poll_cursor < PROTO_ADDR_MIN || bm->poll_cursor > count) {
            bm->poll_cursor = PROTO_ADDR_MIN;
        }
        const uint8_t addr = bm->poll_cursor;
        bm->poll_cursor = (uint8_t)((addr % count) + 1u);
        bm->last_status_poll_ms = now;
        bm->round_end_pending = (addr == count);
        request(bm, BM_TX_STATUS, CMD_GET_STATUS, addr, addr, NULL, 0,
                BUSMASTER_RETRIES, now);
        return done;
    }

    /* Zusatzabfragen nur in der ersten Haelfte des Statusintervalls, damit
     * sie die naechste Statusabfrage nicht verzoegern. */
    if (count > 0u &&
        elapsed_ge(now, bm->last_status_poll_ms, BUSMASTER_POLL_INTERVAL_MS / 2u)) {
        return done;
    }

    /* 2. Serviceadresse 250 (Spez. 4.5.2) */
    if (elapsed_ge(now, bm->last_service_probe_ms, BUSMASTER_SERVICE_PROBE_MS)) {
        start_service_probe(bm, now);
        return done;
    }

    /* 3. Versionsabfrage mit eigenem Zeiger, ohne Statusadresse zu verdraengen */
    if (count > 0u && elapsed_ge(now, bm->last_version_poll_ms, BUSMASTER_VERSION_POLL_MS)) {
        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t addr = (uint8_t)((bm->ver_cursor % count) + 1u);
            bm->ver_cursor = addr;
            const bm_module_t *m = &bm->mod[addr - 1u];
            if (m->online && !m->ver_known && m->ver_fails < BUSMASTER_VERSION_MAX_FAILS) {
                bm->last_version_poll_ms = now;
                request(bm, BM_TX_VERSION, CMD_GET_VERSION, addr, addr, NULL, 0,
                        BUSMASTER_RETRIES, now);
                break;
            }
        }
    }
    return done;
}

bool busmaster_idle(const busmaster_t *bm, uint32_t now_ms)
{
    const uint32_t now = bm_now(bm, now_ms);
    return !busmaster_enum_busy(bm) && bm->queue_count == 0u && bus_free(bm, now);
}

/* --- Oeffentliche Kommandos ------------------------------------------- */

void busmaster_init(busmaster_t *bm,
                    void (*tx)(void *, const uint8_t *, size_t), void *tx_ctx)
{
    memset(bm, 0, sizeof(*bm));
    bm->tx = tx;
    bm->tx_ctx = tx_ctx;
    proto_parser_reset(&bm->parser);
    bm->enum_phase = BM_ENUM_IDLE;
    bm->poll_cursor = PROTO_ADDR_MIN;
}

void busmaster_set_clock(busmaster_t *bm, busmaster_clock_fn now_fn, void *ctx)
{
    bm->now_fn = now_fn;
    bm->now_ctx = ctx;
}

void busmaster_set_log(busmaster_t *bm, busmaster_log_fn fn, void *log_ctx)
{
    bm->log_fn = fn;
    bm->log_ctx = log_ctx;
}

void busmaster_show(busmaster_t *bm, const uint8_t *blaetter, uint8_t count)
{
    if (count > PROTO_MAX_PAYLOAD) {
        count = PROTO_MAX_PAYLOAD;
    }
    if (count > BUSMASTER_MAX_MODULES) {
        count = BUSMASTER_MAX_MODULES;
    }
    for (uint8_t i = 0; i < BUSMASTER_MAX_MODULES; ++i) {
        bm_module_t *m = &bm->mod[i];
        if (i < count) {
            if (!m->soll_valid || m->soll != blaetter[i] || m->rc_stopped) {
                m->rc_tries = 0;   /* Soll-Aenderung: neue Versuche */
            }
            m->soll = blaetter[i];
            m->soll_valid = true;
            m->rc_stopped = false;
        } else {
            m->soll_valid = false;
        }
    }
    /* Der neueste Inhalt ersetzt einen noch nicht gesendeten; ausstehende
     * Korrekturen sind damit hinfaellig. */
    q_remove(bm, BMQ_SHOW | BMQ_RECONCILE, 0, 0);
    if (count == 0u) {
        return;
    }
    if (!q_push(bm, CMD_SET_ALL, PROTO_ADDR_BROADCAST, blaetter, count, BMQ_SHOW)) {
        q_remove_at(bm, 0);   /* voll: aeltesten Eintrag opfern, Anzeige hat Vorrang */
        (void)q_push(bm, CMD_SET_ALL, PROTO_ADDR_BROADCAST, blaetter, count, BMQ_SHOW);
    }
}

void busmaster_clear_targets(busmaster_t *bm)
{
    for (uint8_t i = 0; i < BUSMASTER_MAX_MODULES; ++i) {
        bm->mod[i].soll_valid = false;
    }
    q_remove(bm, BMQ_RECONCILE, 0, 0);
}

void busmaster_poll_status(busmaster_t *bm, uint8_t addr, uint32_t now_ms)
{
    const uint32_t now = bm_now(bm, now_ms);
    housekeeping(bm, now);
    if (!valid_unicast(addr) || busmaster_enum_busy(bm) || !bus_free(bm, now)) {
        return;
    }
    request(bm, BM_TX_STATUS, CMD_GET_STATUS, addr, addr, NULL, 0, BUSMASTER_RETRIES, now);
}

void busmaster_poll_version(busmaster_t *bm, uint8_t addr, uint32_t now_ms)
{
    const uint32_t now = bm_now(bm, now_ms);
    housekeeping(bm, now);
    if (!valid_unicast(addr) || busmaster_enum_busy(bm) || !bus_free(bm, now)) {
        return;
    }
    request(bm, BM_TX_VERSION, CMD_GET_VERSION, addr, addr, NULL, 0, BUSMASTER_RETRIES, now);
}

void busmaster_poll_config(busmaster_t *bm, uint8_t addr, uint32_t now_ms)
{
    (void)now_ms;
    if (!valid_unicast(addr)) {
        return;
    }
    (void)q_push(bm, CMD_GET_CONFIG, addr, NULL, 0, 0);
}

void busmaster_invalidate_version(busmaster_t *bm, uint8_t addr)
{
    bm_module_t *m = mod_of(bm, addr);
    if (m != NULL) {
        m->ver_known = false;
        m->ver_fails = 0;
    }
}

bool busmaster_led_sync(busmaster_t *bm, uint32_t now_ms)
{
    (void)now_ms;
    return q_push(bm, CMD_LED_SYNC, PROTO_ADDR_BROADCAST, NULL, 0, 0);
}

void busmaster_home(busmaster_t *bm, uint8_t addr)
{
    if (addr != PROTO_ADDR_BROADCAST && !valid_unicast(addr)) {
        return;
    }
    for (uint8_t i = 0; i < BUSMASTER_MAX_MODULES; ++i) {
        if (addr == PROTO_ADDR_BROADCAST || addr == i + 1u) {
            bm->mod[i].rc_tries = 0;   /* nach dem Homing das Soll wieder anfahren */
            bm->mod[i].rc_stopped = false;
        }
    }
    (void)q_push(bm, CMD_HOME, addr, NULL, 0, 0);
}

void busmaster_stop(busmaster_t *bm, uint8_t addr)
{
    if (addr != PROTO_ADDR_BROADCAST && !valid_unicast(addr)) {
        return;
    }
    for (uint8_t i = 0; i < BUSMASTER_MAX_MODULES; ++i) {
        if (addr == PROTO_ADDR_BROADCAST || addr == i + 1u) {
            bm->mod[i].rc_stopped = true;   /* STOP nicht selbsttaetig aufheben */
        }
    }
    if (addr == PROTO_ADDR_BROADCAST) {
        q_remove(bm, BMQ_RECONCILE, 0, 0);
    } else {
        q_remove(bm, BMQ_RECONCILE, CMD_SET, addr);
    }
    (void)q_push(bm, CMD_STOP, addr, NULL, 0, 0);
}

void busmaster_identify(busmaster_t *bm, uint8_t addr, uint8_t seconds)
{
    if (!valid_unicast(addr)) {
        return;
    }
    const uint8_t pl[1] = { seconds };
    (void)q_push(bm, CMD_IDENTIFY, addr, pl, sizeof(pl), 0);
}

void busmaster_set_config(busmaster_t *bm, uint8_t addr, uint8_t blattzahl,
                          uint8_t offset, uint8_t vorhalt, uint8_t flags)
{
    if (!valid_unicast(addr)) {
        return;
    }
    const uint8_t pl[4] = { blattzahl, offset, vorhalt, flags };
    (void)q_push(bm, CMD_SET_CONFIG, addr, pl, sizeof(pl), 0);
}

void busmaster_start_enumeration(busmaster_t *bm, uint32_t now_ms)
{
    (void)now_ms;
    if (busmaster_enum_busy(bm)) {
        return;
    }
    bm->enum_phase = BM_ENUM_REQUESTED;   /* Start in busmaster_tick bei freiem Bus */
}

void busmaster_set_enum_hint(busmaster_t *bm, uint8_t expected_count)
{
    bm->enum_hint = expected_count > BUSMASTER_MAX_MODULES
                        ? (uint8_t)BUSMASTER_MAX_MODULES : expected_count;
}

bool busmaster_enum_busy(const busmaster_t *bm)
{
    return bm->enum_phase != BM_ENUM_IDLE && bm->enum_phase != BM_ENUM_DONE;
}

uint8_t busmaster_warnings(const busmaster_t *bm)
{
    return bm->warn;
}

void busmaster_ack_warnings(busmaster_t *bm, uint8_t mask)
{
    bm->warn &= (uint8_t)~mask;
}
