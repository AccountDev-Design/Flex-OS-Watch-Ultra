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
    return scr_next_ex(cur, inactive_ms, timeout_ms, hold, false, true);
}

scr_state_t scr_next_ex(scr_state_t cur, uint32_t inactive_ms, uint32_t timeout_ms, bool hold, bool aod, bool dim)
{
    if (hold) {
        return SCR_ON;
    }
    if (cur == SCR_OFF || cur == SCR_AOD) {
        return cur;
    }
    if (inactive_ms >= timeout_ms) {
        return aod ? SCR_AOD : SCR_OFF;
    }
    if (dim && inactive_ms >= timeout_ms - scr_dim_ms(timeout_ms)) {
        return SCR_DIM;
    }
    return SCR_ON;
}

int scr_dim_ramp(int user, int target, uint32_t elapsed_ms, uint32_t dim_ms)
{
    if (target >= user || dim_ms == 0 || elapsed_ms >= dim_ms) {
        return target < user ? target : user;
    }
    return user - (int)((int64_t)(user - target) * (int64_t)elapsed_ms / (int64_t)dim_ms);
}

void scr_pixel_shift(uint32_t step, int max, int *dx, int *dy)
{
    /* Recorrido en dos anillos (mitad y entero), alternando lados para que
     * dos pasos seguidos no estén juntos: cada píxel pasa el mismo tiempo en
     * cada posición a lo largo del ciclo. Unidades de max/2. */
    static const int8_t kPat[SCR_SHIFT_STEPS][2] = {
        {0, 0},   {2, 0},  {-2, 1}, {1, -2}, {-1, 2}, {2, 2},  {-2, -1}, {0, -2},
        {1, 1},   {-2, 2}, {2, -1}, {-1, -1}, {0, 2},  {2, -2}, {-2, 0},  {-1, 1},
    };
    if (max < 0) {
        max = 0;
    }
    const int i = (int)(step % SCR_SHIFT_STEPS);
    *dx = kPat[i][0] * max / 2;
    *dy = kPat[i][1] * max / 2;
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
