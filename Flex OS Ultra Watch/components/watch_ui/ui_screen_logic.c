/*
 * ui_screen_logic — ver include/ui_screen_logic.h
 */
#include "ui_screen_logic.h"

uint32_t scr_dim_ms(uint32_t timeout_ms)
{
    const uint32_t third = timeout_ms / 3;
    return third < SCR_DIM_MAX_MS ? third : SCR_DIM_MAX_MS;
}

scr_state_t scr_next(scr_state_t cur, uint32_t inactive_ms, uint32_t timeout_ms, bool hold)
{
    if (hold) {
        return SCR_ON;
    }
    if (cur == SCR_OFF) {
        return SCR_OFF;
    }
    if (inactive_ms >= timeout_ms) {
        return SCR_OFF;
    }
    if (inactive_ms >= timeout_ms - scr_dim_ms(timeout_ms)) {
        return SCR_DIM;
    }
    return SCR_ON;
}

int scr_dim_level(int user_percent)
{
    int d = user_percent / 3;
    if (d < 5) {
        d = 5;
    }
    return d < user_percent ? d : user_percent;
}

void btn_init(btn_t *b)
{
    b->idle = -1;
    b->raw = -1;
    b->raw_since = 0;
    b->down = false;
    b->down_ms = 0;
}

bool btn_update(btn_t *b, int level, uint32_t now_ms)
{
    if (level != b->raw) {
        b->raw = level;
        b->raw_since = now_ms;
    }
    const uint32_t stable = now_ms - b->raw_since;
    if (b->idle < 0) {
        /* Aprender el reposo: nivel estable un rato, pasado el arranque. */
        if (now_ms >= BTN_LEARN_AFTER_MS && stable >= BTN_STABLE_MS) {
            b->idle = level;
        }
        return false;
    }
    if (stable < BTN_MIN_MS) {
        return false; /* rebote */
    }
    const bool pressed = level != b->idle;
    if (pressed && !b->down) {
        b->down = true;
        b->down_ms = b->raw_since;
        return false;
    }
    if (!pressed && b->down) {
        b->down = false;
        const uint32_t held = b->raw_since - b->down_ms;
        return held <= BTN_SHORT_MAX_MS;
    }
    return false;
}
