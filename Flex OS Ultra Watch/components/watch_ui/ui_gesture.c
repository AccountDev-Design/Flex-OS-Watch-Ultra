/*
 * ui_gesture — ver include/ui_gesture.h
 */
#include "ui_gesture.h"

#include <string.h>

static int32_t along(const ui_drag_t *d, int32_t x, int32_t y)
{
    return (d->axis == UI_AXIS_X ? x - d->x0 : y - d->y0) * d->dir;
}

static int32_t across(const ui_drag_t *d, int32_t x, int32_t y)
{
    const int32_t v = d->axis == UI_AXIS_X ? y - d->y0 : x - d->x0;
    return v < 0 ? -v : v;
}

static int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void sample(ui_drag_t *d, int32_t pos, uint32_t t)
{
    d->vhead = (uint8_t)((d->vhead + 1) % UI_VEL_SAMPLES);
    d->vp[d->vhead] = pos;
    d->vt[d->vhead] = t;
    if (d->vn < UI_VEL_SAMPLES) {
        d->vn++;
    }
}

void ui_drag_init(ui_drag_t *d, ui_axis_t axis, int8_t dir, int32_t span)
{
    memset(d, 0, sizeof(*d));
    d->axis = axis;
    d->dir = dir < 0 ? -1 : 1;
    d->span = span > 0 ? span : 1;
}

void ui_drag_press(ui_drag_t *d, int32_t x, int32_t y, uint32_t t_ms)
{
    d->state = UI_DRAG_PENDING;
    d->x0 = x;
    d->y0 = y;
    d->travel = 0;
    d->vn = 0;
    d->vhead = 0;
    sample(d, 0, t_ms);
}

ui_drag_state_t ui_drag_move(ui_drag_t *d, int32_t x, int32_t y, uint32_t t_ms)
{
    const int32_t a = along(d, x, y);
    if (d->state == UI_DRAG_PENDING) {
        const int32_t c = across(d, x, y);
        const int32_t aa = a < 0 ? -a : a;
        if (aa < UI_DRAG_SLOP_PX && c < UI_DRAG_SLOP_PX) {
            return d->state; /* todavía puede ser un toque */
        }
        /* Primer movimiento claro: ¿es este gesto? */
        d->state = (a > 0 && a >= c) ? UI_DRAG_ACTIVE : UI_DRAG_REJECTED;
    }
    if (d->state == UI_DRAG_ACTIVE) {
        d->travel = clamp(a, 0, d->span);
        sample(d, a, t_ms);
    }
    return d->state;
}

int32_t ui_drag_velocity(const ui_drag_t *d)
{
    if (d->vn < 2) {
        return 0;
    }
    const uint32_t t_new = d->vt[d->vhead];
    /* La muestra más reciente con al menos UI_VEL_WINDOW_MS de antigüedad;
     * si no la hay, la más antigua guardada. */
    int idx = d->vhead;
    for (int k = 1; k < d->vn; k++) {
        idx = (d->vhead + UI_VEL_SAMPLES - k) % UI_VEL_SAMPLES;
        if (t_new - d->vt[idx] >= UI_VEL_WINDOW_MS) {
            break;
        }
    }
    const uint32_t dt = t_new - d->vt[idx];
    if (dt == 0) {
        return 0;
    }
    return (int32_t)((int64_t)(d->vp[d->vhead] - d->vp[idx]) * 1000 / (int64_t)dt);
}

bool ui_drag_should_commit(int32_t travel, int32_t span, int32_t v)
{
    if (span <= 0) {
        return false;
    }
    if (v <= -UI_DRAG_BACKFLING_PX_S) {
        return false; /* el dedo volvía: el usuario se arrepintió */
    }
    if ((int64_t)travel * 100 >= (int64_t)span * UI_DRAG_COMMIT_PCT) {
        return true;
    }
    return v >= UI_DRAG_FLING_PX_S && (int64_t)travel * 100 >= (int64_t)span * UI_DRAG_FLING_MIN_PCT;
}

ui_drag_result_t ui_drag_release(ui_drag_t *d, int32_t x, int32_t y, uint32_t t_ms)
{
    const ui_drag_state_t st = ui_drag_move(d, x, y, t_ms);
    d->state = UI_DRAG_IDLE;
    if (st != UI_DRAG_ACTIVE) {
        return UI_DRAG_NONE;
    }
    return ui_drag_should_commit(d->travel, d->span, ui_drag_velocity(d)) ? UI_DRAG_COMMIT : UI_DRAG_CANCEL;
}

uint32_t ui_drag_remaining_ms(int32_t travel, int32_t span, uint32_t full_ms, bool commit)
{
    if (span <= 0) {
        return UI_ANIM_MIN_MS;
    }
    travel = clamp(travel, 0, span);
    const int32_t left = commit ? span - travel : travel;
    const uint32_t ms = (uint32_t)((uint64_t)full_ms * (uint32_t)left / (uint32_t)span);
    return ms < UI_ANIM_MIN_MS ? UI_ANIM_MIN_MS : ms;
}

int32_t ui_ease_out_cubic_permille(int32_t u)
{
    u = clamp(u, 0, 1000);
    const int64_t q = 1000 - u;
    return (int32_t)(1000 - q * q * q / 1000000);
}
