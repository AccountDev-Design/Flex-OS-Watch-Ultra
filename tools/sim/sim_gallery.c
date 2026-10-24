/*
 * Galería de la vista previa: la API de watch_gallery sobre el motor REAL
 * (gal_engine: fuentes, listas, miniaturas, visor y transferencias) y el
 * ecosistema en memoria de tests/host (gal_fake_eco: el Flex Web Server de
 * Flex OS Ultra, su pasarela al teléfono y Flex Cloud, con sus rutas, formas
 * JSON y reglas), con el reloj de LVGL. Las fotos son los JPEG de fixtures/
 * (gen_gal_fixtures.py) y se decodifican con el TJpgDec de LVGL. Lo único
 * simulado es la red; que los contratos son los reales lo demuestra
 * tests/host/gallery_e2e.sh contra los servidores de verdad.
 *
 * La batería "galeria" recorre la app como una persona: solo el reloj, el
 * emparejamiento con Flex OS Ultra (código malo y bueno), las pestañas, el
 * visor (2x, deslizar), la información y una descarga, una transferencia que
 * falla y se cancela, Flex Cloud con la cuenta vinculada, sin Wi-Fi y de
 * vuelta, suspender y retomar, y al cerrar: ni memoria del motor, ni bloques
 * de LVGL, ni temporizadores de más.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "flexfs.h"
#include "fxp.h"
#include "gal_fake_eco.h"
#include "sim.h"
#include "ui_internal.h"
#include "watch_gallery.h"

extern const uint8_t _binary_gal_scene_0_jpg_start[], _binary_gal_scene_0_jpg_end[];
extern const uint8_t _binary_gal_scene_1_jpg_start[], _binary_gal_scene_1_jpg_end[];
extern const uint8_t _binary_gal_scene_2_jpg_start[], _binary_gal_scene_2_jpg_end[];
extern const uint8_t _binary_gal_scene_3_jpg_start[], _binary_gal_scene_3_jpg_end[];

static const uint8_t *const kScene[4] = {_binary_gal_scene_0_jpg_start, _binary_gal_scene_1_jpg_start,
                                         _binary_gal_scene_2_jpg_start, _binary_gal_scene_3_jpg_start};
static const uint8_t *const kSceneEnd[4] = {_binary_gal_scene_0_jpg_end, _binary_gal_scene_1_jpg_end,
                                            _binary_gal_scene_2_jpg_end, _binary_gal_scene_3_jpg_end};

/* ---------- la plataforma del motor ---------- */

#define ST_MAX 16

typedef struct {
    char key[24];
    uint8_t val[200];
    size_t len;
    bool used;
} st_ent_t;

static eco_t s_eco;
static gal_io_t s_lan, s_cl;
static gal_store_t s_store;
static gal_env_t s_env;
static gal_engine_t *s_e;
static st_ent_t s_st[ST_MAX];
static lv_timer_t *s_timer;
static bool s_open, s_wifi = true, s_eco_ready;
static uint32_t s_last, s_idle_since;
static size_t s_live; /* bytes del motor vivos (alloc/free) */
static int s_live_n;
static uint32_t s_rng = 0x5EED6A11u;

static void *m_alloc(size_t n)
{
    size_t *p = malloc(n + 16);
    if (!p) {
        return NULL;
    }
    p[0] = n;
    s_live += n;
    s_live_n++;
    return (uint8_t *)p + 16;
}

static void m_free(void *q)
{
    if (q) {
        size_t *p = (size_t *)((uint8_t *)q - 16);
        s_live -= p[0];
        s_live_n--;
        free(p);
    }
}

static st_ent_t *st_find(const char *k)
{
    for (int i = 0; i < ST_MAX; i++) {
        if (s_st[i].used && !strcmp(s_st[i].key, k)) {
            return &s_st[i];
        }
    }
    return NULL;
}

static bool st_get(void *ctx, const char *key, void *buf, size_t *len)
{
    (void)ctx;
    const st_ent_t *e = st_find(key);
    if (!e || e->len > *len) {
        return false;
    }
    memcpy(buf, e->val, e->len);
    *len = e->len;
    return true;
}

static bool st_set(void *ctx, const char *key, const void *buf, size_t len)
{
    (void)ctx;
    st_ent_t *e = st_find(key);
    for (int i = 0; !e && i < ST_MAX; i++) {
        if (!s_st[i].used) {
            e = &s_st[i];
            e->used = true;
            snprintf(e->key, sizeof(e->key), "%s", key);
        }
    }
    if (!e || len > sizeof(e->val)) {
        return false;
    }
    memcpy(e->val, buf, len);
    e->len = len;
    return true;
}

static bool st_erase(void *ctx, const char *key)
{
    (void)ctx;
    st_ent_t *e = st_find(key);
    if (e) {
        memset(e, 0, sizeof(*e));
    }
    return true;
}

static void st_random(void *ctx, uint8_t *out, size_t n)
{
    (void)ctx;
    for (size_t i = 0; i < n; i++) {
        s_rng = s_rng * 1664525u + 1013904223u;
        out[i] = (uint8_t)(s_rng >> 24);
    }
}

static uint32_t env_now(void *ctx)
{
    (void)ctx;
    return s_eco.clock;
}

static uint64_t env_fs_free(void *ctx)
{
    (void)ctx;
    return 8u * 1024u * 1024u;
}

static bool io_cancelled(void *ctx)
{
    (void)ctx;
    return s_e && gal_engine_should_abort(s_e);
}

static void eco_setup(void)
{
    if (s_eco_ready) {
        eco_free(&s_eco);
    }
    eco_init(&s_eco);
    s_eco.clock = 100000;
    s_lan = gf_io(&s_eco.lan);
    s_cl = gf_io(&s_eco.cloud);
    s_lan.cancelled = io_cancelled;
    s_cl.cancelled = io_cancelled;
    s_store = (gal_store_t){.get = st_get, .set = st_set, .erase = st_erase, .random = st_random};
    s_env = (gal_env_t){.io_lan = &s_lan, .io_cloud = &s_cl, .store = &s_store, .alloc = m_alloc, .free = m_free,
                        .now = env_now, .fs_free = env_fs_free, .mac_hex = "A1B2C3D4E5F6", .version = "sim"};
    s_eco_ready = true;
}

/* El bucle de la tarea del reloj, en el reloj de LVGL: el transporte falso
 * contesta en el acto, así que cada vuelta hace su trabajo entero. */
static void step_cb(lv_timer_t *t)
{
    (void)t;
    const uint32_t now = lv_tick_get();
    s_eco.clock += now - s_last;
    s_last = now;
    if (!s_e) {
        return;
    }
    gal_set_wifi(s_e, s_wifi);
    for (int i = 0; i < 16; i++) {
        if (gal_engine_step(s_e) > 0) {
            break;
        }
    }
    if (!s_open && !gal_engine_busy(s_e)) {
        if (!s_idle_since) {
            s_idle_since = now | 1u;
        } else if (now - s_idle_since > 2000) { /* como watch_gallery: cerrada y sin trabajo, fuera el motor */
            gal_engine_free(s_e);
            s_e = NULL;
            lv_timer_delete(s_timer);
            s_timer = NULL;
        }
    } else {
        s_idle_since = 0;
    }
}

gal_engine_t *watch_gallery_open(void)
{
    if (!s_eco_ready) {
        eco_setup();
    }
    if (!s_e) {
        s_e = gal_engine_new(&s_env);
        if (!s_e) {
            return NULL;
        }
        gal_engine_set_cloud(s_e, "cloud.test", 443, true, NULL, NULL);
    }
    if (!s_timer) {
        s_last = lv_tick_get();
        s_timer = lv_timer_create(step_cb, 10, NULL);
    }
    s_open = true;
    s_idle_since = 0;
    if (!gal_engine_open(s_e)) {
        s_open = false;
        return NULL;
    }
    return s_e;
}

void watch_gallery_close(void)
{
    if (s_e && s_open) {
        s_open = false;
        gal_engine_close(s_e);
    }
}

gal_engine_t *watch_gallery_engine(void)
{
    return s_open ? s_e : NULL;
}

/* ---------- la batería ---------- */

static void click(lv_obj_t *o)
{
    if (o) {
        lv_obj_send_event(o, LV_EVENT_CLICKED, NULL);
    }
}

static bool visible(lv_obj_t *o)
{
    for (lv_obj_t *p = o; p; p = lv_obj_get_parent(p)) {
        if (lv_obj_has_flag(p, LV_OBJ_FLAG_HIDDEN)) {
            return false;
        }
    }
    return o != NULL;
}

static bool has(const char *t)
{
    lv_obj_t *l = sim_find_label(ui_nav_top(), t);
    return l && visible(l);
}

static bool has_prefix_in(lv_obj_t *o, const char *p)
{
    if (lv_obj_check_type(o, &lv_label_class) && strncmp(lv_label_get_text(o), p, strlen(p)) == 0 && visible(o)) {
        return true;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        if (has_prefix_in(lv_obj_get_child(o, (int32_t)i), p)) {
            return true;
        }
    }
    return false;
}

static bool has_prefix(const char *p)
{
    return has_prefix_in(ui_nav_top(), p);
}

static lv_obj_t *label_prefix(lv_obj_t *o, const char *p)
{
    if (lv_obj_check_type(o, &lv_label_class) && strncmp(lv_label_get_text(o), p, strlen(p)) == 0 && visible(o)) {
        return o;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *r = label_prefix(lv_obj_get_child(o, (int32_t)i), p);
        if (r) {
            return r;
        }
    }
    return NULL;
}

static lv_obj_t *tap_prefix(const char *p)
{
    lv_obj_t *l = label_prefix(ui_nav_top(), p);
    while (l && !lv_obj_has_flag(l, LV_OBJ_FLAG_CLICKABLE)) {
        l = lv_obj_get_parent(l);
    }
    return l;
}

static int count_text(lv_obj_t *o, const char *t)
{
    int n = lv_obj_check_type(o, &lv_label_class) && !strcmp(lv_label_get_text(o), t) && visible(o);
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        n += count_text(lv_obj_get_child(o, (int32_t)i), t);
    }
    return n;
}

static lv_obj_t *tap_of(const char *text)
{
    lv_obj_t *l = sim_find_label(ui_nav_top(), text);
    while (l && !lv_obj_has_flag(l, LV_OBJ_FLAG_CLICKABLE)) {
        l = lv_obj_get_parent(l);
    }
    return l;
}

/* Imágenes visibles con fuente (miniaturas puestas) */
static int count_images(lv_obj_t *o)
{
    int n = 0;
    if (lv_obj_check_type(o, &lv_image_class) && lv_image_get_src(o) && visible(o)) {
        n++;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        n += count_images(lv_obj_get_child(o, (int32_t)i));
    }
    return n;
}

static lv_obj_t *find_class(lv_obj_t *o, const lv_obj_class_t *cls, int nth, int *seen)
{
    if (lv_obj_check_type(o, cls)) {
        if ((*seen)++ == nth) {
            return o;
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *r = find_class(lv_obj_get_child(o, (int32_t)i), cls, nth, seen);
        if (r) {
            return r;
        }
    }
    return NULL;
}

static lv_obj_t *nth_of(const lv_obj_class_t *cls, int nth)
{
    int seen = 0;
    return find_class(ui_nav_top(), cls, nth, &seen);
}

/* Toca la tecla "OK" del teclado numérico (última fila, a la derecha del 0) */
static void pad_ok(void)
{
    lv_obj_t *kb = nth_of(&lv_buttonmatrix_class, 0);
    if (!kb) {
        return;
    }
    lv_area_t a;
    lv_obj_get_coords(kb, &a);
    const int32_t rh = (lv_area_get_height(&a) - 3 * 6) / 4;
    sim_tap(a.x1 + lv_area_get_width(&a) * 5 / 8, a.y2 - rh / 2);
    sim_run(100);
}

static void lint(const char *name, const char *out, const char *png)
{
    sim_lint(name);
    sim_check_glyphs(ui_nav_top(), name);
    if (png) {
        sim_save(out, png);
    }
}

static void put_file(const char *vpath, const uint8_t *d, size_t n)
{
    char real[512];
    if (ffs_real_path(vpath, real, sizeof(real))) {
        FILE *f = fopen(real, "wb");
        if (f) {
            fwrite(d, 1, n, f);
            fclose(f);
        }
    }
}

static gal_status_t status(void)
{
    gal_status_t s;
    memset(&s, 0, sizeof(s));
    if (s_e) {
        gal_status(s_e, &s);
    }
    return s;
}

#define WAIT(cond, ms) do { for (int _w = 0; _w < (ms) / 50 && !(cond); _w++) { sim_run(50); } } while (0)

static size_t scene_len(int i)
{
    return (size_t)(kSceneEnd[i] - kScene[i]);
}

void sim_suite_gallery(const char *out)
{
    const int f0 = sim_fails;
    lv_screen_load(ui_nav_screen());
    ui_nav_home();
    sim_run(400);
    const watch_storage_info_t saved = sim_stg;
    char root[400], cmd[460];
    snprintf(root, sizeof(root), "%s/fs_galeria", out);
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    EXPECT(system(cmd) == 0, "no se pudo limpiar %s", root);
    mkdir(root, 0775);
    ffs_set_root(root);
    sim_stg.state = STG_MOUNTED;
    sim_stg.gate = STG_GATE_OK;
    ffs_init_dirs(); /* /Paint, /Papelera... (ui_fs_ready las crea una sola vez por arranque) */
    ffs_mkdir("/Imagenes");
    put_file("/Imagenes/Playa.jpg", kScene[1], scene_len(1));
    put_file("/Imagenes/Montaña.jpg", kScene[0], scene_len(0));
    EXPECT(fxp_create("/Paint/Dibujo 1.fxp", 378, 250), "no se pudo crear el dibujo");
    const int16_t pts[] = {40, 40, 200, 120, 330, 60};
    fxp_append("/Paint/Dibujo 1.fxp", fxp_rgb565(230, 60, 60), 6, pts, 3);
    eco_setup();
    memset(s_st, 0, sizeof(s_st));
    s_wifi = true;
    const uint32_t blocks0 = sim_mem_blocks();
    const int timers0 = sim_timer_count();

    /* ---- 1. Solo el reloj: sin Flex OS Ultra ni cuenta ---- */
    EXPECT(ui_nav_open_app(ui_gallery_create), "no se abrió la Galería");
    sim_run(1500);
    gal_status_t s = status();
    EXPECT(s.nl == 3, "galería: el reloj tiene 2 fotos y 1 dibujo y se ven %d", s.nl);
    EXPECT(has("Todas") && has("Reloj") && has("Remoto"), "galería: faltan las pestañas");
    EXPECT(has_prefix("Conecta Flex OS Ultra o vincula Flex Account"), "galería: no dice qué configurar");
    EXPECT(count_images(ui_nav_top()) >= 2, "galería: sin miniaturas de las fotos del reloj (%d)",
           count_images(ui_nav_top()));
    EXPECT(count_text(ui_nav_top(), "Reloj") >= 3, "galería: sin marca de origen Reloj en las celdas");
    lint("galeria: solo el reloj", out, "gal_reloj");
    click(tap_of("Remoto"));
    sim_run(400);
    EXPECT(has("Sin fuente remota") && has("Elegir el origen"), "galería: pestaña remota sin explicación");
    lint("galeria: sin fuente remota", out, NULL);

    /* ---- 2. Origen: sin teléfono no se ofrece Flex Phone ---- */
    click(tap_of("Elegir el origen"));
    sim_run(500);
    EXPECT(has("Automático") && has("Flex OS Ultra") && has("Flex Cloud"), "origen: faltan opciones");
    EXPECT(!has("Flex Phone"), "origen: ofrece Flex Phone sin teléfono");
    EXPECT(!has("PC"), "origen: ofrece una fuente que no existe");
    lint("galeria: origen", out, "gal_origen_vacio");

    /* ---- 3. Emparejar con Flex OS Ultra: código malo y bueno ---- */
    snprintf(s_eco.phone_state, sizeof(s_eco.phone_state), "ready");
    eco_p4_add(&s_eco, "Atardecer.jpg", "photo", "JPEG", kScene[2], scene_len(2), kScene[2], scene_len(2), false);
    eco_p4_add(&s_eco, "Puerto.jpg", "photo", "JPEG", kScene[1], scene_len(1), kScene[1], scene_len(1), false);
    eco_p4_add(&s_eco, "Bosque.jpg", "photo", "JPEG", kScene[3], scene_len(3), kScene[3], scene_len(3), false);
    eco_p4_add(&s_eco, "Secreto.jpg", "photo", "JPEG", kScene[0], scene_len(0), NULL, 0, true);
    eco_p4_add(&s_eco, "Clip.mp4", "video", "MP4", "vid", 3, NULL, 0, false);
    eco_cloud_add(&s_eco, s_eco.ph, "Del teléfono.jpg", "image/jpeg", kScene[3], scene_len(3), kScene[3],
                  scene_len(3), "image/jpeg");
    click(tap_of("Conectar"));
    sim_run(500);
    lv_obj_t *ta0 = nth_of(&lv_textarea_class, 0), *ta1 = nth_of(&lv_textarea_class, 1);
    EXPECT(ta0 && ta1, "flex os ultra: sin campos de dirección y código");
    lint("galeria: conectar con flex os ultra", out, "gal_ultra");
    if (ta0 && ta1) {
        lv_textarea_set_text(ta0, "192.168.1.20");
        lv_textarea_set_text(ta1, "000000");
        pad_ok();
        WAIT(has_prefix("Código incorrecto"), 3000);
        EXPECT(has_prefix("Código incorrecto"), "flex os ultra: el código malo no se dice");
        EXPECT(!status().ultra_paired, "flex os ultra: emparejado con un código malo");
        lint("galeria: codigo incorrecto", out, NULL);
        lv_textarea_set_text(ta1, "123456");
        pad_ok();
        WAIT(status().ultra_paired, 3000);
        EXPECT(status().ultra_paired, "flex os ultra: el código bueno no empareja");
        WAIT(has("Origen"), 3000); /* se cierra sola */
    }
    EXPECT(has("Origen"), "flex os ultra: la página no vuelve a Origen tras emparejar");
    WAIT(status().health[GAL_SRC_PHONE].state == GAL_ST_READY && has("Flex Phone"), 4000);
    EXPECT(has("Flex Phone"), "origen: no aparece Flex Phone con el teléfono listo en Flex OS Ultra");
    EXPECT(has("Lista"), "origen: Flex OS Ultra no sale como lista");
    lint("galeria: origen con flex os ultra", out, "gal_origen");
    ui_nav_back();
    sim_run(600);

    /* ---- 4. Pestañas con Flex OS Ultra ---- */
    WAIT(status().nr == 5, 4000);
    s = status();
    EXPECT(s.choice.src == GAL_SRC_ULTRA && s.choice.automatic, "automático: no eligió Flex OS Ultra (%d)", s.choice.src);
    EXPECT(has("Flex OS Ultra"), "pestaña remota sin el nombre de la fuente");
    sim_run(1500);
    EXPECT(count_images(ui_nav_top()) >= 3, "remota: sin miniaturas del P4 (%d)", count_images(ui_nav_top()));
    EXPECT(has("Protegido") || has_prefix("Protegido"), "remota: lo protegido no se marca");
    lint("galeria: flex os ultra", out, "gal_ultra_fotos");
    click(tap_of("Todas"));
    sim_run(1500);
    EXPECT(has("Ultra") && has("Reloj"), "todas: sin marcas de origen");
    lint("galeria: todas", out, "gal_todas");

    /* ---- 5. Visor: la foto, 2x, deslizar, capa ---- */
    click(tap_of("Flex OS Ultra"));
    sim_run(800);
    {
        /* la primera celda de la rejilla */
        lv_obj_t *img = NULL;
        for (int i = 0; (img = nth_of(&lv_image_class, i)) != NULL; i++) {
            if (lv_image_get_src(img) && visible(img)) {
                break;
            }
        }
        EXPECT(img != NULL, "visor: no hay celda con miniatura");
        if (img) {
            lv_obj_send_event(lv_obj_get_parent(img), LV_EVENT_SHORT_CLICKED, NULL);
        }
    }
    sim_run(600);
    WAIT(status().view_state == GAL_V_READY, 4000);
    sim_run(300);
    EXPECT(status().view_state == GAL_V_READY, "visor: la foto no se abrió (%d)", status().view_state);
    EXPECT(count_images(ui_nav_top()) == 1, "visor: no enseña la foto");
    lint("galeria: visor", out, "gal_visor");
    sim_tap(205, 260);
    sim_run(120);
    sim_tap(205, 260); /* doble toque: 2x */
    sim_run(400);
    {
        lv_obj_t *img = nth_of(&lv_image_class, 0);
        const lv_image_dsc_t *d = img ? lv_image_get_src(img) : NULL;
        EXPECT(d && d->header.w > FLEX_SCREEN_W, "visor: el doble toque no amplía (%d)", d ? (int)d->header.w : -1);
    }
    sim_drag(205, 260, 120, 200, 6, 16); /* moverse por el 2x */
    sim_release();
    sim_run(300);
    lint("galeria: visor 2x", out, "gal_visor_2x");
    sim_tap(205, 260);
    sim_run(120);
    sim_tap(205, 260); /* otra vez: encajada */
    sim_run(400);
    sim_drag(330, 260, 120, 262, 8, 16); /* a la siguiente */
    sim_release();
    sim_run(300);
    WAIT(status().view_state == GAL_V_READY, 4000);
    sim_run(300);
    EXPECT(status().view_state == GAL_V_READY, "visor: la siguiente no se abrió");
    lint("galeria: visor siguiente", out, NULL);

    /* ---- 6. Información y descarga al reloj ---- */
    click(lv_obj_get_child(ui_nav_top(), 2)); /* la capa de abajo del visor: información */
    sim_run(600);
    EXPECT(has("Información") && has("Tamaño") && has("Formato"), "información: incompleta");
    EXPECT(has("Descargar al reloj"), "información: no ofrece descargar");
    EXPECT(!has("Subir a Flex OS Ultra"), "información: ofrece subir lo que ya está en Flex OS Ultra");
    lint("galeria: informacion", out, "gal_info");
    const int nl0 = status().nl;
    click(tap_of("Descargar al reloj"));
    sim_run(200);
    EXPECT(has("En la cola de transferencias"), "descarga: no se dice que está en la cola");
    WAIT(status().nl == nl0 + 1, 5000);
    EXPECT(status().nl == nl0 + 1, "descarga: la foto no llegó al reloj (%d -> %d)", nl0, status().nl);
    sim_run(800);
    EXPECT(!has("Descargar al reloj"), "información: sigue ofreciendo descargar lo que ya está");
    lint("galeria: informacion descargada", out, NULL);
    ui_nav_back();
    sim_run(400);
    ui_nav_back();
    sim_run(600);

    /* ---- 7. Una transferencia que falla: se dice, se cancela ---- */
    s_eco.p4_down = true;
    {
        gal_engine_lock(s_e);
        int nr = 0;
        const gal_item_t *l = gal_list(s_e, 1, &nr);
        gal_item_t it;
        bool found = false;
        for (int i = 0; i < nr && !found; i++) {
            if (!(l[i].flags & (GAL_I_ONWATCH | GAL_I_LOCKED)) && l[i].kind == GAL_K_PHOTO) {
                it = l[i];
                found = true;
            }
        }
        gal_engine_unlock(s_e);
        char why[96];
        EXPECT(found && gal_download(s_e, &it, why, sizeof(why)) > 0, "transferencia: no se pudo poner en cola");
    }
    WAIT(has_prefix("1 · "), 3000);
    EXPECT(has_prefix("1 · "), "transferencias: la cabecera no lo dice");
    lint("galeria: transferencia en curso", out, NULL);
    click(tap_prefix("1 · "));
    sim_run(600);
    EXPECT(has("Transferencias") && has("Cancelar"), "transferencias: no se abre la lista con Cancelar");
    lint("galeria: transferencias", out, "gal_transferencias");
    click(tap_of("Cancelar"));
    sim_run(600);
    EXPECT(has("Cancelada") && has("Quitar las terminadas"), "transferencias: no se cancela");
    click(tap_of("Quitar las terminadas"));
    sim_run(600);
    EXPECT(has("No hay transferencias"), "transferencias: no se quitan");
    s_eco.p4_down = false;
    ui_nav_back();
    sim_run(600);

    /* ---- 8. Cerrar: el motor se va entero ---- */
    ui_nav_home();
    sim_run(3500);
    EXPECT(s_e == NULL && s_live == 0 && s_live_n == 0, "galería cerrada: quedan %zu B en %d bloques del motor",
           s_live, s_live_n);

    /* ---- 9. Flex Cloud con la cuenta vinculada (la misma de la web) ---- */
    const char *tok = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQ";
    const uint8_t one = 1;
    st_set(NULL, "token", tok, 43);
    st_set(NULL, "address", "ana@flex", 8);
    st_set(NULL, "display", "Ana", 3);
    st_set(NULL, "linked", &one, 1);
    snprintf(s_eco.bearer, sizeof(s_eco.bearer), "%s", tok);
    s_eco.quota_used = 1288490189ull; /* 1,2 GB de 5 GB */
    static const char *const kCl[] = {"Vacaciones.jpg", "Cena.jpg", "Concierto.jpg", "Nieve.jpg", "Río.jpg"};
    for (int i = 0; i < 5; i++) {
        eco_cloud_add(&s_eco, s_eco.cl, kCl[i], "image/jpeg", kScene[i % 4], scene_len(i % 4), kScene[i % 4],
                      scene_len(i % 4), "image/jpeg");
    }
    eco_cloud_add(&s_eco, s_eco.cl, "web.png", "image/png", "png", 3, NULL, 0, NULL);
    EXPECT(ui_nav_open_app(ui_gallery_create), "no se volvió a abrir la Galería");
    sim_run(1200);
    WAIT(has_prefix("Automático: Flex Cloud"), 5000);
    EXPECT(has_prefix("Automático: Flex Cloud"), "automático: sin sesión del P4 no pasa a Flex Cloud");
    click(tap_prefix("Automático"));
    sim_run(600);
    EXPECT(has("Origen"), "galería: tocar el origen no abre Origen");
    click(tap_of("Flex Cloud"));
    sim_run(400);
    WAIT(status().choice.src == GAL_SRC_CLOUD && status().nr == 6, 5000);
    s = status();
    EXPECT(s.pick == GAL_PICK_CLOUD && s.choice.src == GAL_SRC_CLOUD && !s.choice.automatic,
           "origen: Flex Cloud elegido y se usa %d", s.choice.src);
    EXPECT(has("Espacio usado") && has("1,2 GB de 5,0 GB"), "origen: sin la cuota real de Flex Cloud");
    lint("galeria: origen flex cloud", out, "gal_origen_cloud");
    click(tap_of("Flex Account"));
    sim_run(600);
    EXPECT(has("ana@flex") && has("Desvincular este reloj"), "cuenta: no enseña la cuenta vinculada");
    lint("galeria: flex account", out, "gal_cuenta");
    ui_nav_back();
    sim_run(400);
    ui_nav_back();
    sim_run(600);
    click(tap_of("Flex Cloud"));
    sim_run(1500);
    EXPECT(count_images(ui_nav_top()) >= 4, "flex cloud: sin miniaturas (%d)", count_images(ui_nav_top()));
    EXPECT(has_prefix("PNG"), "flex cloud: el PNG no dice por qué no se ve");
    lint("galeria: flex cloud", out, "gal_cloud");

    /* ---- 10. Sin Wi-Fi y de vuelta, sin tocar nada ---- */
    s_wifi = false;
    sim_run(1500);
    EXPECT(has_prefix("Sin Wi-Fi"), "sin wi-fi: no se dice");
    lint("galeria: sin wi-fi", out, "gal_sin_wifi");
    s_wifi = true;
    WAIT(status().health[GAL_SRC_CLOUD].state == GAL_ST_READY && status().remote_state == GAL_L_READY, 8000);
    EXPECT(status().health[GAL_SRC_CLOUD].state == GAL_ST_READY, "wi-fi de vuelta: Flex Cloud no se recupera");

    /* ---- 11. Suspender y retomar ---- */
    EXPECT(ui_nav_suspend(false), "no se pudo suspender");
    sim_run(400);
    EXPECT(watch_gallery_engine() == NULL, "suspendida: el motor sigue abierto");
    EXPECT(ui_nav_open_app(ui_gallery_create), "no se retomó la Galería");
    sim_run(1500);
    EXPECT(count_images(ui_nav_top()) >= 4, "retomada: sin miniaturas (%d)", count_images(ui_nav_top()));

    /* ---- 12. Al cerrar: nada de más ---- */
    ui_nav_home();
    sim_run(3500);
    EXPECT(s_e == NULL && s_live == 0 && s_live_n == 0, "galería cerrada: quedan %zu B en %d bloques del motor",
           s_live, s_live_n);
    EXPECT(sim_mem_blocks() == blocks0, "fuga en la Galería: %u -> %u bloques de LVGL", (unsigned)blocks0,
           (unsigned)sim_mem_blocks());
    EXPECT(sim_timer_count() == timers0, "temporizadores %d -> %d", timers0, sim_timer_count());
    eco_free(&s_eco);
    s_eco_ready = false;
    sim_stg = saved;
    ffs_set_root(NULL);
    EXPECT(system(cmd) == 0, "no se pudo limpiar %s", root);
    printf("galeria: %s\n", sim_fails == f0 ? "OK" : "FALLOS");
}
