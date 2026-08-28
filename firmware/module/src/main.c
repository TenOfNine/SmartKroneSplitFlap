/*
 * Einstiegspunkt der Modul-Firmware (ATtiny1616).
 *
 * Platzhalter. Der Zustandsautomat nach docs/spezifikation.md Kapitel 6
 * (INIT / HOMING / IDLE / MOVING / ERROR), die USART-Anbindung im RS-485-Modus
 * ueber XDIR, die Impulsauswertung und der Watchdog werden in Backlog T7
 * implementiert. Die hardwareunabhaengigen Bausteine liegen bereits in
 * lib/protocol/ und lib/enumeration/ und sind mit `pio test -e native` getestet.
 */
#include <avr/io.h>

int main(void)
{
    for (;;) {
        /* T7 */
    }
}
