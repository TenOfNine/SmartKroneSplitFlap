/*
 * Home-Assistant-MQTT-Auto-Discovery fuer die Zentralsteuerung.
 * Baut Config-Topic und Payload je Entity nach docs/spezifikation.md 7.6.
 *
 * Hardwareunabhaengig, Payload wird per snprintf gebaut (keine JSON-Bibliothek).
 */
#ifndef KRONE_HADISCOVERY_H
#define KRONE_HADISCOVERY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HA_ENT_TEXT,          /* Anzeigetext          text        */
    HA_ENT_MODE,          /* Betriebsart          select      */
    HA_ENT_HOME,          /* Homing               button      */
    HA_ENT_SELFTEST,      /* Selbsttest           button      */
    HA_ENT_ERROR,         /* Sammelfehler         binary_sensor */
    HA_ENT_MODULE_CHAR,   /* Zeichen je Modul     sensor      */
    HA_ENT_MODULE_ONLINE, /* Modul online         binary_sensor */
} ha_entity_t;

/*
 * base_topic z. B. "krone/anzeige", node_id z. B. "krone_anzeige" (nur
 * [a-z0-9_]). module_n wird nur bei HA_ENT_MODULE_* verwendet.
 *
 * config_topic:  <disc_prefix>/<component>/<node>/<object>/config
 * payload:       JSON mit name, unique_id, den passenden Topics,
 *                availability_topic (<base_topic>/status) und device{}
 *
 * Rueckgabe: 0 bei Erfolg, -1 bei zu kleinem Puffer.
 */
int hadiscovery_entity(char *config_topic, size_t topic_size,
                       char *payload, size_t payload_size,
                       const char *disc_prefix, const char *base_topic,
                       const char *node_id, ha_entity_t which, uint8_t module_n);

#ifdef __cplusplus
}
#endif

#endif /* KRONE_HADISCOVERY_H */
