/* Siehe hadiscovery.h und docs/spezifikation.md 7.6. */
#include "hadiscovery.h"

#include <stdio.h>

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

    int n = snprintf(config_topic, topic_size, "%s/%s/%s/%s/config",
                     disc_prefix, m.component, node_id, object);
    if (n < 0 || (size_t)n >= topic_size) {
        return -1;
    }

    /* gemeinsamer device-Block */
    char dev[160];
    snprintf(dev, sizeof(dev),
             "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"KRONE Fallblattanzeige\","
             "\"manufacturer\":\"Eigenbau\",\"model\":\"REW Palettenreihe A\"}",
             node_id);

    char uid[64];
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
                     "%s}",
                     name, uid, base_topic, base_topic, dev);
        break;
    case HA_ENT_MODE:
        r = snprintf(payload, payload_size,
                     "{\"name\":\"%s\",\"unique_id\":\"%s\","
                     "\"command_topic\":\"%s/mode/set\",\"state_topic\":\"%s/mode/state\","
                     "\"options\":[\"text\",\"clock_hm\",\"clock_hms\",\"blank\",\"off\"],"
                     "%s}",
                     name, uid, base_topic, base_topic, dev);
        break;
    case HA_ENT_HOME:
        r = snprintf(payload, payload_size,
                     "{\"name\":\"%s\",\"unique_id\":\"%s\","
                     "\"command_topic\":\"%s/home/press\",%s}",
                     name, uid, base_topic, dev);
        break;
    case HA_ENT_SELFTEST:
        r = snprintf(payload, payload_size,
                     "{\"name\":\"%s\",\"unique_id\":\"%s\","
                     "\"command_topic\":\"%s/selftest/press\",%s}",
                     name, uid, base_topic, dev);
        break;
    case HA_ENT_ERROR:
        r = snprintf(payload, payload_size,
                     "{\"name\":\"%s\",\"unique_id\":\"%s\","
                     "\"state_topic\":\"%s/error/state\",\"device_class\":\"problem\","
                     "\"payload_on\":\"1\",\"payload_off\":\"0\",%s}",
                     name, uid, base_topic, dev);
        break;
    case HA_ENT_MODULE_CHAR:
        r = snprintf(payload, payload_size,
                     "{\"name\":\"%s\",\"unique_id\":\"%s\","
                     "\"state_topic\":\"%s/module/%u/char\",%s}",
                     name, uid, base_topic, (unsigned)module_n, dev);
        break;
    case HA_ENT_MODULE_ONLINE:
        r = snprintf(payload, payload_size,
                     "{\"name\":\"%s\",\"unique_id\":\"%s\","
                     "\"state_topic\":\"%s/module/%u/online\",\"device_class\":\"connectivity\","
                     "\"payload_on\":\"1\",\"payload_off\":\"0\",%s}",
                     name, uid, base_topic, (unsigned)module_n, dev);
        break;
    default:
        return -1;
    }

    if (r < 0 || (size_t)r >= payload_size) {
        return -1;
    }
    return 0;
}
