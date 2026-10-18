/* Siehe hadiscovery.h und docs/spezifikation.md 7.6. */
#include "hadiscovery.h"

#include <stdio.h>
#include <string.h>

static bool id_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

bool hadiscovery_valid_base_topic(const char *s)
{
    if (s == NULL) {
        return false;
    }
    const size_t n = strlen(s);
    if (n == 0u || n > HADISC_TOPIC_MAX || s[0] == '/' || s[n - 1u] == '/') {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        if (s[i] == '/') {
            if (s[i + 1u] == '/') {
                return false;
            }
        } else if (!id_char(s[i]) && s[i] != '.') {
            return false;
        }
    }
    return true;
}

bool hadiscovery_valid_node_id(const char *s)
{
    if (s == NULL) {
        return false;
    }
    const size_t n = strlen(s);
    if (n == 0u || n > HADISC_NODE_MAX) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        if (!id_char(s[i])) {
            return false;
        }
    }
    return true;
}

/* snprintf vollstaendig? Ein abgeschnittener Block (z. B. availability bei
 * langem base_topic) ergaebe sonst ungueltiges JSON (#40). */
static int fits(int n, size_t size)
{
    return n >= 0 && (size_t)n < size;
}

typedef struct {
    const char *component;  /* text / select / button / binary_sensor / sensor */
    const char *object;     /* Objekt-ID-Stamm */
    const char *name;
} ent_meta_t;

static int meta(ha_entity_t which, ent_meta_t *m)
{
    switch (which) {
    case HA_ENT_TEXT:
        *m = (ent_meta_t){ "text", "text", "Anzeigetext" };
        return 0;
    case HA_ENT_MODE:
        *m = (ent_meta_t){ "select", "mode", "Betriebsart" };
        return 0;
    case HA_ENT_HOME:
        *m = (ent_meta_t){ "button", "home", "Homing" };
        return 0;
    case HA_ENT_SELFTEST:
        *m = (ent_meta_t){ "button", "selftest", "Selbsttest" };
        return 0;
    case HA_ENT_ERROR:
        *m = (ent_meta_t){ "binary_sensor", "error", "Sammelfehler" };
        return 0;
    case HA_ENT_MODULE_CHAR:
        *m = (ent_meta_t){ "sensor", "module_char", "Zeichen" };
        return 0;
    case HA_ENT_MODULE_ONLINE:
        *m = (ent_meta_t){ "binary_sensor", "module_online", "Modul online" };
        return 0;
    default:
        return -1;
    }
}

int hadiscovery_entity(char *config_topic, size_t topic_size,
                       char *payload, size_t payload_size,
                       const char *disc_prefix, const char *base_topic,
                       const char *node_id, ha_entity_t which, uint8_t module_n)
{
    ent_meta_t m;
    if (meta(which, &m) != 0) {
        return -1;
    }

    const int per_module = (which == HA_ENT_MODULE_CHAR ||
                            which == HA_ENT_MODULE_ONLINE);

    char object[48];
    if (per_module) {
        snprintf(object, sizeof(object), "%s_%u", m.object, (unsigned)module_n);
    } else {
        snprintf(object, sizeof(object), "%s", m.object);
    }
    /* Laengen wie die Puffer der Zentralsteuerung (base_topic 47, node_id 31) */
    if (base_topic == NULL || node_id == NULL ||
        strlen(base_topic) > HADISC_TOPIC_MAX || strlen(node_id) > HADISC_NODE_MAX) {
        return -1;
    }

    int n = snprintf(config_topic, topic_size, "%s/%s/%s/%s/config",
                     disc_prefix, m.component, node_id, object);
    if (n < 0 || (size_t)n >= topic_size) {
        return -1;
    }

    /* gemeinsamer Verfuegbarkeits-Block (LWT-Topic, Spezifikation 7.6) */
    char avail[96 + HADISC_TOPIC_MAX];
    if (!fits(snprintf(avail, sizeof(avail),
                       "\"availability_topic\":\"%s/status\","
                       "\"payload_available\":\"online\","
                       "\"payload_not_available\":\"offline\"",
                       base_topic), sizeof(avail))) {
        return -1;
    }

    /* gemeinsamer device-Block */
    char dev[128 + HADISC_NODE_MAX];
    if (!fits(snprintf(dev, sizeof(dev),
                       "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"KRONE Fallblattanzeige\","
                       "\"manufacturer\":\"Eigenbau\",\"model\":\"REW Palettenreihe A\"}",
                       node_id), sizeof(dev))) {
        return -1;
    }

    char uid[24 + HADISC_NODE_MAX];
    if (per_module) {
        snprintf(uid, sizeof(uid), "%s_%s_%u", node_id, m.object, (unsigned)module_n);
    } else {
        snprintf(uid, sizeof(uid), "%s_%s", node_id, m.object);
    }

    char name[48];
    if (per_module) {
        snprintf(name, sizeof(name), "%s Modul %u", m.name, (unsigned)module_n);
    } else {
        snprintf(name, sizeof(name), "%s", m.name);
    }

    int r;
    switch (which) {
    case HA_ENT_TEXT:
        r = snprintf(payload, payload_size,
                     "{\"name\":\"%s\",\"unique_id\":\"%s\","
                     "\"command_topic\":\"%s/text/set\",\"state_topic\":\"%s/text/state\","
                     "%s,%s}",
                     name, uid, base_topic, base_topic, avail, dev);
        break;
    case HA_ENT_MODE:
        r = snprintf(payload, payload_size,
                     "{\"name\":\"%s\",\"unique_id\":\"%s\","
                     "\"command_topic\":\"%s/mode/set\",\"state_topic\":\"%s/mode/state\","
                     "\"options\":[\"text\",\"clock_hm\",\"clock_hms\",\"blank\",\"off\"],"
                     "%s,%s}",
                     name, uid, base_topic, base_topic, avail, dev);
        break;
    case HA_ENT_HOME:
        r = snprintf(payload, payload_size,
                     "{\"name\":\"%s\",\"unique_id\":\"%s\","
                     "\"command_topic\":\"%s/home/press\",%s,%s}",
                     name, uid, base_topic, avail, dev);
        break;
    case HA_ENT_SELFTEST:
        r = snprintf(payload, payload_size,
                     "{\"name\":\"%s\",\"unique_id\":\"%s\","
                     "\"command_topic\":\"%s/selftest/press\",%s,%s}",
                     name, uid, base_topic, avail, dev);
        break;
    case HA_ENT_ERROR:
        r = snprintf(payload, payload_size,
                     "{\"name\":\"%s\",\"unique_id\":\"%s\","
                     "\"state_topic\":\"%s/error/state\",\"device_class\":\"problem\","
                     "\"payload_on\":\"1\",\"payload_off\":\"0\",%s,%s}",
                     name, uid, base_topic, avail, dev);
        break;
    case HA_ENT_MODULE_CHAR:
        r = snprintf(payload, payload_size,
                     "{\"name\":\"%s\",\"unique_id\":\"%s\","
                     "\"state_topic\":\"%s/module/%u/char\",%s,%s}",
                     name, uid, base_topic, (unsigned)module_n, avail, dev);
        break;
    case HA_ENT_MODULE_ONLINE:
        r = snprintf(payload, payload_size,
                     "{\"name\":\"%s\",\"unique_id\":\"%s\","
                     "\"state_topic\":\"%s/module/%u/online\",\"device_class\":\"connectivity\","
                     "\"payload_on\":\"1\",\"payload_off\":\"0\",%s,%s}",
                     name, uid, base_topic, (unsigned)module_n, avail, dev);
        break;
    default:
        return -1;
    }

    if (r < 0 || (size_t)r >= payload_size) {
        return -1;
    }
    return 0;
}
