/*
 * gal_race — la Galería con DOS hilos, como en el reloj, para ThreadSanitizer.
 *
 * Las pruebas de PC (test_gallery.c) y la vista previa (tools/sim) ejecutan el
 * motor en el mismo hilo que la interfaz: nunca ejercitan lo que pasa en el
 * reloj, donde la tarea "watch_gal" (watch_gallery.c) corre en un núcleo y
 * LVGL en el otro. Aquí:
 *  - el hilo "tarea" repite el bucle de watch_gallery.c: gal_engine_step, la
 *    espera y, con la app cerrada y sin transferencias, la liberación del
 *    motor a los 2 s (con el mismo cerrojo de vida s_life);
 *  - el hilo "interfaz" hace lo que hace ui_gallery.c: abrir, pedir la lista,
 *    pedir y fijar miniaturas, abrir el visor, suspender, retomar y cerrar.
 *
 *   make -C tests/host race LVGL_DIR=/ruta/lvgl-9.5.0
 * (se compila con -fsanitize=thread; las fotos son las de fixtures/).
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "flexfs.h"
#include "gal_engine.h"

static pthread_mutex_t s_mx;   /* el del motor (recursivo), como s_mx en watch_gallery.c */
static pthread_mutex_t s_life; /* abrir, cerrar y el final de la tarea */
static pthread_mutex_t s_kick_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_kick_cv = PTHREAD_COND_INITIALIZER;
static gal_engine_t *s_e;
static atomic_bool s_open, s_task_alive, s_quit;
static gal_env_t s_env;
static gal_io_t s_io;
static atomic_uint s_frees, s_opens;

static uint32_t now_ms(void *ctx)
{
    (void)ctx;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000u);
}

static void mx_lock(void *ctx)
{
    (void)ctx;
    pthread_mutex_lock(&s_mx);
}

static void mx_unlock(void *ctx)
{
    (void)ctx;
    pthread_mutex_unlock(&s_mx);
}

static void do_kick(void *ctx)
{
    (void)ctx;
    pthread_mutex_lock(&s_kick_mx);
    pthread_cond_signal(&s_kick_cv);
    pthread_mutex_unlock(&s_kick_mx);
}

static void *ps_alloc(size_t n)
{
    return malloc(n);
}

static uint64_t fs_free(void *ctx)
{
    (void)ctx;
    return 8u * 1024 * 1024;
}

static bool mem_ok(void *ctx, size_t n)
{
    (void)ctx;
    (void)n;
    return true;
}

/* Sin red: solo las fotos del reloj (las de fixtures). */
static int io_connect(void *c, const char *h, uint16_t p, bool tls, uint32_t to)
{
    (void)c, (void)h, (void)p, (void)tls, (void)to;
    return GAL_IO_E_CONNECT;
}
static int io_write(void *c, const uint8_t *b, size_t n, uint32_t to)
{
    (void)c, (void)b, (void)n, (void)to;
    return -1;
}
static int io_read(void *c, uint8_t *b, size_t n, uint32_t to)
{
    (void)c, (void)b, (void)n, (void)to;
    return -1;
}
static void io_close(void *c)
{
    (void)c;
}
static bool io_cancelled(void *c)
{
    (void)c;
    return false;
}

static bool st_get(void *c, const char *k, void *b, size_t *l)
{
    (void)c, (void)k, (void)b, (void)l;
    return false;
}
static bool st_set(void *c, const char *k, const void *b, size_t l)
{
    (void)c, (void)k, (void)b, (void)l;
    return true;
}
static bool st_erase(void *c, const char *k)
{
    (void)c, (void)k;
    return true;
}
static void st_random(void *c, uint8_t *o, size_t n)
{
    (void)c;
    for (size_t i = 0; i < n; i++) {
        o[i] = (uint8_t)rand();
    }
}
static gal_store_t s_store = {.get = st_get, .set = st_set, .erase = st_erase, .random = st_random};

/* ---- la tarea (copia del bucle de watch_gallery.c) ---- */
static void *task(void *arg)
{
    (void)arg;
    uint32_t idle_since = 0;
    for (;;) {
        const uint32_t now = now_ms(NULL);
        gal_set_wifi(s_e, false);
        const uint32_t wait = gal_engine_step(s_e);
        if (!atomic_load(&s_open) && !gal_engine_busy(s_e)) {
            if (!idle_since) {
                idle_since = now | 1u;
            }
            if (now - idle_since > 60) { /* 2 s en el reloj; aquí 60 ms para repetir muchas veces */
                pthread_mutex_lock(&s_life);
                if (!atomic_load(&s_open)) {
                    gal_engine_free(s_e);
                    s_e = NULL;
                    atomic_fetch_add(&s_frees, 1);
                    atomic_store(&s_task_alive, false);
                    pthread_mutex_unlock(&s_life);
                    return NULL;
                }
                pthread_mutex_unlock(&s_life);
            }
        } else {
            idle_since = 0;
        }
        pthread_mutex_lock(&s_kick_mx);
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        const uint32_t w = wait < 2 ? 2 : wait > 20 ? 20 : wait;
        ts.tv_nsec += (long)w * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&s_kick_cv, &s_kick_mx, &ts);
        pthread_mutex_unlock(&s_kick_mx);
    }
}

static pthread_t s_thr;

/* watch_gallery_open / watch_gallery_close, con la misma lógica */
static gal_engine_t *gallery_open(void)
{
    pthread_mutex_lock(&s_life);
    if (!s_e) {
        s_e = gal_engine_new(&s_env);
    }
    bool ok = s_e != NULL;
    if (ok && !atomic_load(&s_task_alive)) {
        if (s_thr) {
            pthread_join(s_thr, NULL); /* la anterior ya salió (s_task_alive = false) */
        }
        atomic_store(&s_task_alive, true);
        pthread_create(&s_thr, NULL, task, NULL);
    }
    if (ok) {
        atomic_store(&s_open, true);
        ok = gal_engine_open(s_e);
        if (!ok) {
            atomic_store(&s_open, false);
        }
    }
    gal_engine_t *e = ok ? s_e : NULL;
    pthread_mutex_unlock(&s_life);
    atomic_fetch_add(&s_opens, 1);
    return e;
}

static void gallery_close(void)
{
    pthread_mutex_lock(&s_life);
    if (s_e && atomic_load(&s_open)) {
        atomic_store(&s_open, false);
        gal_engine_close(s_e);
    }
    pthread_mutex_unlock(&s_life);
}

/* ---- la interfaz (lo que hace ui_gallery.c con G->e) ---- */
#define CELLS 15
typedef struct {
    gal_item_t key;
    int slot;
    const uint16_t *px;
} cell_t;

static uint64_t s_sum; /* "dibuja" los píxeles fijados: lee lo que LVGL leería */

static void cells_clear(gal_engine_t *e, cell_t *c)
{
    for (int k = 0; k < CELLS; k++) {
        if (c[k].slot >= 0 && e) {
            gal_thumb_release(e, c[k].slot);
        }
        c[k].slot = -1;
        c[k].px = NULL;
    }
}

static int ui_session(int round)
{
    gal_engine_t *e = gallery_open();
    if (!e) {
        return 1;
    }
    cell_t cell[CELLS];
    for (int k = 0; k < CELLS; k++) {
        cell[k].slot = -1;
        cell[k].px = NULL;
    }
    gal_item_t items[64];
    int n = 0;
    bool viewing = false;
    for (int it = 0; it < 60; it++) {
        gal_status_t st;
        gal_status(e, &st);
        /* rebuild(): copia la pestaña con el cerrojo */
        gal_engine_lock(e);
        int nl = 0;
        const gal_item_t *l = gal_list(e, 0, &nl);
        n = nl < 64 ? nl : 64;
        for (int i = 0; i < n; i++) {
            items[i] = l[i];
        }
        gal_engine_unlock(e);
        for (int k = 0; k < CELLS && k < n; k++) {
            if (cell[k].slot < 0) {
                gal_thumb_request(e, &items[k]);
                int stt;
                const uint16_t *px = NULL;
                const int s = gal_thumb_acquire(e, &items[k], &px, &stt);
                if (s >= 0) {
                    cell[k].slot = s;
                    cell[k].px = px;
                    cell[k].key = items[k];
                }
            }
            if (cell[k].px) {
                for (int p = 0; p < GAL_THUMB_SIDE * GAL_THUMB_SIDE; p += 97) {
                    s_sum += cell[k].px[p];
                }
            }
        }
        /* visor */
        if (n && !viewing && (it % 7) == 3) {
            gal_view_open(e, &items[(it + round) % n]);
            viewing = true;
        }
        if (viewing) {
            gal_view_t v;
            gal_view_get(e, &v);
            if (v.state == GAL_V_READY && v.fit) {
                for (int p = 0; p < v.fit_w * v.fit_h; p += 131) {
                    s_sum += v.fit[p];
                }
            }
            if ((it % 11) == 10) {
                gal_view_close(e);
                viewing = false;
            }
        }
        /* a veces cambia de "pestaña": suelta todas las celdas */
        if ((it % 13) == 12) {
            cells_clear(e, cell);
        }
        usleep(500 + (unsigned)(rand() % 1500));
    }
    /* suspender o cerrar, como suspend_cb / del_cb */
    if (viewing) {
        gal_view_close(e);
    }
    cells_clear(e, cell);
    gallery_close();
    /* a veces se vuelve enseguida (antes de que la tarea suelte el motor) y a veces tarde */
    usleep((round % 3) ? 2000 : 90000);
    return 0;
}

static void copy_fixture(const char *root, const char *name, const char *as)
{
    char src[512], dst[512];
    snprintf(src, sizeof(src), "fixtures/%s", name);
    snprintf(dst, sizeof(dst), "%s/Imagenes/%s", root, as);
    FILE *in = fopen(src, "rb");
    FILE *out = fopen(dst, "wb");
    if (in && out) {
        char buf[4096];
        size_t k;
        while ((k = fread(buf, 1, sizeof(buf), in)) > 0) {
            fwrite(buf, 1, k, out);
        }
    }
    if (in) {
        fclose(in);
    }
    if (out) {
        fclose(out);
    }
}

int main(int argc, char **argv)
{
    const int rounds = argc > 1 ? atoi(argv[1]) : 40;
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&s_mx, &a);
    pthread_mutex_init(&s_life, NULL);
    char root[] = "/tmp/galrace_XXXXXX";
    if (!mkdtemp(root)) {
        perror("mkdtemp");
        return 2;
    }
    ffs_set_root(root);
    ffs_init_dirs();
    ffs_mkdir("/Imagenes");
    const char *fx[] = {"gal_quad_640x480.jpg", "gal_tall_300x900.jpg", "gal_tiny_40x30.jpg"};
    for (int i = 0; i < 18; i++) {
        char as[64];
        snprintf(as, sizeof(as), "foto_%02d.jpg", i);
        copy_fixture(root, fx[i % 3], as);
    }
    s_io = (gal_io_t){.connect = io_connect, .write = io_write, .read = io_read, .close = io_close,
                      .now = now_ms, .cancelled = io_cancelled};
    s_env = (gal_env_t){.io_lan = &s_io, .io_cloud = &s_io, .store = &s_store, .alloc = ps_alloc, .free = free,
                        .lock = mx_lock, .unlock = mx_unlock, .kick = do_kick, .now = now_ms, .fs_free = fs_free,
                        .mem_ok = mem_ok, .mac_hex = "A1B2C3D4E5F6", .version = "race"};
    srand(7);
    int fails = 0;
    for (int r = 0; r < rounds; r++) {
        fails += ui_session(r);
    }
    atomic_store(&s_quit, true);
    /* deja que la tarea termine y suelte el motor */
    for (int i = 0; i < 200 && atomic_load(&s_task_alive); i++) {
        usleep(5000);
    }
    if (s_thr) {
        pthread_join(s_thr, NULL);
    }
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    if (system(cmd) != 0) {
        fprintf(stderr, "no se pudo borrar %s\n", root);
    }
    printf("Galería con dos hilos: %d sesiones, %u motores liberados, suma %llu, %d fallos\n", rounds,
           (unsigned)atomic_load(&s_frees), (unsigned long long)s_sum, fails);
    return fails ? 1 : 0;
}
