/* Firmware entry point (nRF52840). The host simulator has its own main in sim/. */
#include "watch.h"

int main(void)
{
    watch_init();
    for (;;) {
        watch_step();
    }
}
