#include "haptics.h"

#include "core/log.h"
#include "drivers/drv2605.h"

static bool s_ok;
static bool s_playing;
static uint32_t s_count;
static uint8_t s_last;

void haptics_init(bool hw_ok)
{
    s_ok = hw_ok;
    s_playing = false;
    s_count = 0;
    s_last = 0;
}

void haptics_play(uint8_t effect, uint64_t now)
{
    (void)now;
    if (!s_ok) {
        return;
    }
    if (drv2605_play(effect) == 0) {
        s_playing = true;
        s_count++;
        s_last = effect;
        log_printf("haptic: effect %u", effect);
    }
}

uint64_t haptics_poll(uint64_t now)
{
    if (!s_playing) {
        return 0;
    }
    bool playing = false;
    if (drv2605_is_playing(&playing) != 0 || !playing) {
        drv2605_standby();
        s_playing = false;
        return 0;
    }
    return now + HAPTICS_POLL_MS;
}

uint32_t haptics_count(void)
{
    return s_count;
}

uint8_t haptics_last_effect(void)
{
    return s_last;
}
