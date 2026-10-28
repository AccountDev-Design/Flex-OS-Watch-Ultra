/*
 * Pruebas de watch_media: la máquina de estados de la microSD (wm_sd_logic) y
 * el clasificador de lo que hay en una tarjeta que no se monta (wm_fsclass).
 *
 * Además de los recorridos a mano, un "controlador simulado" ejecuta las
 * acciones que pide la lógica sobre una tarjeta de mentira que se mete, se
 * saca, falla o tiene otro formato al azar, y comprueba en cada paso las
 * invariantes que protegen los datos:
 *  - nunca se desmonta con un arrendamiento vivo;
 *  - nunca se formatea sin que se haya pedido;
 *  - no se dan arrendamientos fuera de READY;
 *  - la tarjeta encendida siempre se acaba apagando (sin fugas del host).
 */
#include <string.h>

#include "test_util.h"
#include "wm_fsclass.h"
#include "wm_path.h"
#include "wm_sd_logic.h"
#include "wm_web.h"

/* ---------------- recorridos a mano ---------------- */

static wsd_action_t step(wsd_t *s, uint32_t *now, uint32_t adv)
{
    *now += adv;
    return wsd_next(s, *now);
}

static void test_boot_and_mount(void)
{
    wsd_t s;
    uint32_t now = 1000;
    wsd_init(&s, now);
    CHECK_EQ(wsd_next(&s, now), WSD_DO_PROBE); /* al arrancar se mira ya */
    CHECK_EQ(s.st, WSD_DETECTING);
    CHECK_EQ(wsd_visible(&s), WSD_ABSENT);     /* un sondeo rutinario no parpadea */
    const uint32_t g0 = s.gen;
    wsd_on_probe(&s, WSD_PROBE_NOCARD, 0, now);
    CHECK_EQ(s.st, WSD_ABSENT);
    CHECK_EQ(s.gen, g0);                       /* sin tarjeta, sin cambio visible */
    CHECK_EQ(step(&s, &now, 100), WSD_DO_WAIT);
    CHECK_EQ(step(&s, &now, WSD_PROBE_MS_IDLE), WSD_DO_PROBE);
    wsd_on_probe(&s, WSD_PROBE_OK, 0x1234, now);
    CHECK_EQ(s.st, WSD_MOUNTING);
    CHECK(s.gen != g0);
    CHECK(s.card_up);
    CHECK(!wsd_lease(&s));                     /* aún no está montada */
    CHECK_EQ(wsd_next(&s, now), WSD_DO_MOUNT);
    wsd_on_mount(&s, WSD_E_NONE, now);
    CHECK_EQ(s.st, WSD_READY);
    CHECK(s.fs_up);
    CHECK(wsd_lease(&s));
    CHECK_EQ(s.leases, 1);
    wsd_unlease(&s);
    CHECK_EQ(s.leases, 0);
    wsd_unlease(&s);                           /* de más: no baja de 0 */
    CHECK_EQ(s.leases, 0);
    CHECK_EQ(step(&s, &now, 10), WSD_DO_WAIT);
    CHECK_EQ(step(&s, &now, WSD_STATUS_MS_IDLE), WSD_DO_STATUS);
    wsd_on_status(&s, true, now);
    CHECK_EQ(s.st, WSD_READY);
}

static void mounted(wsd_t *s, uint32_t *now)
{
    wsd_init(s, *now);
    CHECK_EQ(wsd_next(s, *now), WSD_DO_PROBE);
    wsd_on_probe(s, WSD_PROBE_OK, 77, *now);
    CHECK_EQ(wsd_next(s, *now), WSD_DO_MOUNT);
    wsd_on_mount(s, WSD_E_NONE, *now);
    CHECK_EQ(s->st, WSD_READY);
}

static void test_surprise_removal_waits_for_leases(void)
{
    wsd_t s;
    uint32_t now = 5000;
    mounted(&s, &now);
    CHECK(wsd_lease(&s)); /* el reproductor */
    CHECK(wsd_lease(&s)); /* la grabadora */
    CHECK_EQ(step(&s, &now, WSD_STATUS_MS_IDLE), WSD_DO_STATUS);
    wsd_on_status(&s, false, now);
    CHECK_EQ(s.st, WSD_READY);                 /* un fallo suelto no basta */
    CHECK_EQ(step(&s, &now, WSD_STATUS_RETRY_MS), WSD_DO_STATUS);
    wsd_on_status(&s, false, now);
    CHECK_EQ(s.st, WSD_REMOVING);
    CHECK(s.surprise);
    CHECK(!wsd_lease(&s));                     /* nadie nuevo */
    CHECK_EQ(wsd_next(&s, now), WSD_DO_WAIT);  /* con arrendamientos NO se desmonta */
    CHECK_EQ(step(&s, &now, WSD_HELD_MS + 1), WSD_DO_WAIT);
    CHECK_EQ(s.err, WSD_E_HELD);               /* y se dice por qué */
    wsd_unlease(&s);
    CHECK_EQ(wsd_next(&s, now), WSD_DO_WAIT);
    wsd_unlease(&s);
    CHECK_EQ(wsd_next(&s, now), WSD_DO_UNMOUNT);
    wsd_on_unmounted(&s, now);
    CHECK_EQ(wsd_next(&s, now), WSD_DO_RELEASE); /* retirada: nunca se formatea */
    wsd_on_released(&s, now);
    CHECK_EQ(s.st, WSD_ABSENT);
    CHECK(!s.ejected);
    CHECK(!s.card_up && !s.fs_up);
}

static void test_eject_keeps_card_out(void)
{
    wsd_t s;
    uint32_t now = 100;
    mounted(&s, &now);
    CHECK(wsd_request_eject(&s, now));
    CHECK_EQ(s.st, WSD_REMOVING);
    CHECK_EQ(wsd_next(&s, now), WSD_DO_UNMOUNT);
    wsd_on_unmounted(&s, now);
    CHECK_EQ(wsd_next(&s, now), WSD_DO_RELEASE);
    wsd_on_released(&s, now);
    CHECK_EQ(s.st, WSD_ABSENT);
    CHECK(s.ejected);
    /* Sigue puesta: el sondeo la ve, pero NO se vuelve a montar. */
    CHECK_EQ(step(&s, &now, WSD_PROBE_MS_IDLE), WSD_DO_PROBE);
    wsd_on_probe(&s, WSD_PROBE_OK, 77, now);
    CHECK_EQ(s.st, WSD_ABSENT);
    CHECK(s.card_up);
    CHECK_EQ(wsd_next(&s, now), WSD_DO_RELEASE);
    wsd_on_released(&s, now);
    CHECK(!s.card_up);
    CHECK(s.ejected);
    /* Otra tarjeta distinta sí se monta. */
    CHECK_EQ(step(&s, &now, WSD_PROBE_MS_IDLE), WSD_DO_PROBE);
    wsd_on_probe(&s, WSD_PROBE_OK, 99, now);
    CHECK_EQ(s.st, WSD_MOUNTING);
    CHECK(!s.ejected);
    /* La expulsada, sacada y vuelta a meter: se monta. */
    mounted(&s, &now);
    CHECK(wsd_request_eject(&s, now));
    wsd_next(&s, now);
    wsd_on_unmounted(&s, now);
    wsd_next(&s, now);
    wsd_on_released(&s, now);
    CHECK(s.ejected);
    CHECK_EQ(step(&s, &now, WSD_PROBE_MS_IDLE), WSD_DO_PROBE);
    wsd_on_probe(&s, WSD_PROBE_NOCARD, 0, now);
    CHECK(!s.ejected);
    CHECK_EQ(step(&s, &now, WSD_PROBE_MS_IDLE), WSD_DO_PROBE);
    wsd_on_probe(&s, WSD_PROBE_OK, 77, now);
    CHECK_EQ(s.st, WSD_MOUNTING);
    /* "Volver a montar" sin sacarla. */
    mounted(&s, &now);
    wsd_request_eject(&s, now);
    wsd_next(&s, now);
    wsd_on_unmounted(&s, now);
    wsd_next(&s, now);
    wsd_on_released(&s, now);
    CHECK(wsd_request_retry(&s, now));
    CHECK(!s.ejected);
    CHECK_EQ(wsd_next(&s, now), WSD_DO_PROBE);
    wsd_on_probe(&s, WSD_PROBE_OK, 77, now);
    CHECK_EQ(s.st, WSD_MOUNTING);
}

static void test_unsupported_formats(void)
{
    const wsd_err_t errs[] = {WSD_E_EXFAT, WSD_E_NTFS, WSD_E_GPT, WSD_E_NOFS, WSD_E_FS_DAMAGED};
    for (size_t i = 0; i < sizeof(errs) / sizeof(errs[0]); i++) {
        wsd_t s;
        uint32_t now = 10;
        wsd_init(&s, now);
        wsd_next(&s, now);
        wsd_on_probe(&s, WSD_PROBE_OK, 5, now);
        wsd_next(&s, now);
        wsd_on_mount(&s, errs[i], now);
        CHECK_EQ(s.st, WSD_ERROR);
        CHECK_EQ(s.err, errs[i]);
        CHECK(!s.fs_up && s.card_up);
        CHECK(!wsd_lease(&s));
        CHECK(wsd_can_format(&s));
        /* Pasa el tiempo: se comprueba que sigue puesta, NUNCA se formatea sola. */
        for (int k = 0; k < 50; k++) {
            wsd_action_t a = step(&s, &now, 3000);
            CHECK(a != WSD_DO_FORMAT && a != WSD_DO_MOUNT && a != WSD_DO_UNMOUNT);
            if (a == WSD_DO_STATUS) {
                wsd_on_status(&s, true, now);
            }
        }
        CHECK_EQ(s.st, WSD_ERROR);
        CHECK(wsd_request_format(&s, now)); /* la persona lo pide (doble confirmación en la interfaz) */
        CHECK_EQ(s.st, WSD_BUSY);
        CHECK_EQ(wsd_next(&s, now), WSD_DO_FORMAT);
        wsd_on_format(&s, true, now);
        CHECK_EQ(s.st, WSD_MOUNTING);
        CHECK_EQ(wsd_next(&s, now), WSD_DO_MOUNT);
        wsd_on_mount(&s, WSD_E_NONE, now);
        CHECK_EQ(s.st, WSD_READY);
    }
    /* Errores que NO se arreglan formateando. */
    wsd_t s;
    uint32_t now = 1;
    wsd_init(&s, now);
    wsd_next(&s, now);
    wsd_on_probe(&s, WSD_PROBE_UNSUPPORTED, 0, now);
    CHECK_EQ(s.st, WSD_ERROR);
    CHECK_EQ(s.err, WSD_E_UNSUPPORTED);
    CHECK(!wsd_can_format(&s));
    CHECK(!wsd_request_format(&s, now));
    const uint32_t g = s.gen;
    CHECK_EQ(step(&s, &now, WSD_PROBE_MS_IDLE), WSD_DO_PROBE); /* se vuelve a mirar */
    CHECK_EQ(wsd_visible(&s), WSD_DETECTING);
    wsd_on_probe(&s, WSD_PROBE_UNSUPPORTED, 0, now);
    CHECK_EQ(s.gen, g);                                        /* mismo resultado: sin parpadeo */
    CHECK_EQ(step(&s, &now, WSD_PROBE_MS_IDLE), WSD_DO_PROBE);
    wsd_on_probe(&s, WSD_PROBE_NOCARD, 0, now);
    CHECK_EQ(s.st, WSD_ABSENT);
    CHECK_EQ(s.err, WSD_E_NONE);
    CHECK(s.gen != g);
}

static void test_format_from_ready(void)
{
    wsd_t s;
    uint32_t now = 1;
    mounted(&s, &now);
    CHECK(wsd_lease(&s));
    CHECK(wsd_request_format(&s, now));
    CHECK_EQ(s.st, WSD_REMOVING);
    CHECK_EQ(wsd_next(&s, now), WSD_DO_WAIT); /* el archivo abierto se cierra antes */
    wsd_unlease(&s);
    CHECK_EQ(wsd_next(&s, now), WSD_DO_UNMOUNT);
    wsd_on_unmounted(&s, now);
    CHECK_EQ(wsd_next(&s, now), WSD_DO_FORMAT);
    CHECK_EQ(s.st, WSD_BUSY);
    CHECK(!wsd_lease(&s));
    wsd_on_format(&s, false, now);
    CHECK_EQ(s.st, WSD_ERROR);
    CHECK_EQ(s.err, WSD_E_FORMAT);
    CHECK(wsd_can_format(&s)); /* se puede reintentar */
    /* Retirada durante el formateo pedido: no se formatea. */
    mounted(&s, &now);
    CHECK(wsd_request_format(&s, now));
    CHECK_EQ(wsd_next(&s, now), WSD_DO_UNMOUNT);
    s.surprise = true; /* dejó de contestar mientras tanto */
    wsd_on_unmounted(&s, now);
    CHECK_EQ(wsd_next(&s, now), WSD_DO_RELEASE);
}

static void test_retry_and_interest(void)
{
    wsd_t s;
    uint32_t now = 1;
    wsd_init(&s, now);
    wsd_next(&s, now);
    wsd_on_probe(&s, WSD_PROBE_OK, 3, now);
    wsd_next(&s, now);
    wsd_on_mount(&s, WSD_E_IO, now);
    CHECK_EQ(s.st, WSD_ERROR);
    CHECK(wsd_request_retry(&s, now));
    CHECK_EQ(s.st, WSD_REMOVING);
    CHECK_EQ(wsd_next(&s, now), WSD_DO_RELEASE);
    wsd_on_released(&s, now);
    CHECK_EQ(s.st, WSD_ABSENT);
    CHECK(!s.ejected);
    CHECK_EQ(wsd_next(&s, now), WSD_DO_PROBE); /* reintento inmediato */
    wsd_on_probe(&s, WSD_PROBE_NOCARD, 0, now);
    /* Interés: se mira ya, y luego más a menudo. */
    CHECK_EQ(step(&s, &now, 5), WSD_DO_WAIT);
    wsd_interest(&s, true, now);
    CHECK_EQ(wsd_next(&s, now), WSD_DO_PROBE);
    wsd_on_probe(&s, WSD_PROBE_NOCARD, 0, now);
    CHECK_EQ(step(&s, &now, WSD_PROBE_MS_ACTIVE - 1), WSD_DO_WAIT);
    CHECK_EQ(step(&s, &now, 1), WSD_DO_PROBE);
    wsd_on_probe(&s, WSD_PROBE_NOCARD, 0, now);
    wsd_interest(&s, false, now);
    wsd_interest(&s, false, now); /* de más: no baja de 0 */
    CHECK_EQ(s.interest, 0);
    CHECK_EQ(step(&s, &now, WSD_PROBE_MS_ACTIVE), WSD_DO_PROBE); /* la ya programada */
    wsd_on_probe(&s, WSD_PROBE_NOCARD, 0, now);
    CHECK_EQ(step(&s, &now, WSD_PROBE_MS_ACTIVE), WSD_DO_WAIT);  /* y después, al ritmo lento */
    CHECK_EQ(step(&s, &now, WSD_PROBE_MS_IDLE - WSD_PROBE_MS_ACTIVE), WSD_DO_PROBE);
    wsd_on_probe(&s, WSD_PROBE_NOCARD, 0, now);
    /* Aviso de E/S de una app: CMD13 ya. */
    mounted(&s, &now);
    CHECK_EQ(step(&s, &now, 1), WSD_DO_WAIT);
    wsd_io_error(&s, now);
    CHECK_EQ(wsd_next(&s, now), WSD_DO_STATUS);
    /* Contador de tiempo que da la vuelta (49 días). */
    wsd_init(&s, 0xFFFFFF00u);
    wsd_next(&s, 0xFFFFFF00u);
    wsd_on_probe(&s, WSD_PROBE_NOCARD, 0, 0xFFFFFF00u);
    CHECK_EQ(wsd_next(&s, 0xFFFFFF00u + 100), WSD_DO_WAIT);
    CHECK_EQ(wsd_next(&s, 0xFFFFFF00u + WSD_PROBE_MS_IDLE), WSD_DO_PROBE);
}

/* ---------------- controlador simulado al azar ---------------- */

static uint32_t rnd_state = 12345;
static uint32_t rnd(void)
{
    rnd_state = rnd_state * 1103515245u + 12345u;
    return rnd_state >> 8;
}

static void test_random_driver(void)
{
    for (int run = 0; run < 40; run++) {
        wsd_t s;
        uint32_t now = rnd();
        wsd_init(&s, now);
        bool inserted = false, card_on = false, mounted_fs = false;
        int fmt_kind = 0;      /* 0 FAT, 1 exFAT, 2 roto */
        uint32_t serial = 1;
        uint32_t holders = 0;  /* arrendamientos que tienen las "apps" */
        bool format_asked = false;
        int formats = 0;
        for (int it = 0; it < 4000; it++) {
            now += rnd() % 700;
            switch (rnd() % 23) {
            case 0: /* meter o sacar */
                inserted = !inserted;
                if (inserted) {
                    serial = 1 + rnd() % 3;
                    fmt_kind = rnd() % 3;
                }
                break;
            case 1: case 2: case 3: /* una app abre algo */
                if (wsd_lease(&s)) {
                    CHECK(s.st == WSD_READY && mounted_fs);
                    holders++;
                }
                break;
            case 4: case 5: case 6: /* una app cierra */
                if (holders) {
                    holders--;
                    wsd_unlease(&s);
                }
                break;
            case 7: wsd_request_eject(&s, now); break;
            case 8:
                if (rnd() % 4 == 0 && wsd_request_format(&s, now)) {
                    format_asked = true;
                }
                break;
            case 9: wsd_request_retry(&s, now); break;
            case 10: wsd_interest(&s, rnd() & 1, now); break;
            case 11: wsd_io_error(&s, now); break;
            default: break;
            }
            for (int guard = 0; guard < 8; guard++) {
                const wsd_action_t a = wsd_next(&s, now);
                CHECK(s.leases == holders);
                if (a == WSD_DO_WAIT) {
                    break;
                }
                switch (a) {
                case WSD_DO_PROBE:
                    CHECK(!card_on);
                    if (!inserted) {
                        wsd_on_probe(&s, WSD_PROBE_NOCARD, 0, now);
                    } else if (fmt_kind == 2 && rnd() % 3 == 0) {
                        wsd_on_probe(&s, WSD_PROBE_BAD, 0, now);
                    } else {
                        card_on = true;
                        wsd_on_probe(&s, WSD_PROBE_OK, serial, now);
                    }
                    break;
                case WSD_DO_MOUNT:
                    CHECK(card_on && !mounted_fs);
                    if (!inserted) {
                        wsd_on_mount(&s, WSD_E_IO, now);
                    } else if (fmt_kind == 1) {
                        wsd_on_mount(&s, WSD_E_EXFAT, now);
                    } else {
                        mounted_fs = true;
                        wsd_on_mount(&s, WSD_E_NONE, now);
                    }
                    break;
                case WSD_DO_STATUS:
                    CHECK(card_on);
                    wsd_on_status(&s, inserted, now);
                    break;
                case WSD_DO_UNMOUNT:
                    /* INVARIANTE: nunca con un archivo abierto. */
                    CHECK(holders == 0);
                    CHECK(mounted_fs);
                    mounted_fs = false;
                    wsd_on_unmounted(&s, now);
                    break;
                case WSD_DO_FORMAT:
                    /* INVARIANTE: solo si se pidió, sin montar y sin nadie dentro. */
                    CHECK(format_asked);
                    CHECK(!mounted_fs && holders == 0 && card_on);
                    format_asked = false;
                    formats++;
                    if (inserted) {
                        fmt_kind = 0;
                    }
                    wsd_on_format(&s, inserted, now);
                    break;
                case WSD_DO_RELEASE:
                    CHECK(card_on && !mounted_fs);
                    card_on = false;
                    wsd_on_released(&s, now);
                    break;
                default:
                    break;
                }
            }
            CHECK(s.card_up == card_on);
            CHECK(s.fs_up == mounted_fs);
            CHECK(s.st < WSD_STATE_N);
            if (s.st != WSD_READY && s.st != WSD_REMOVING) {
                CHECK(!mounted_fs);
            }
        }
        /* Al final: todo cerrado y la tarjeta fuera -> nada encendido. */
        while (holders) {
            holders--;
            wsd_unlease(&s);
        }
        inserted = false;
        for (int k = 0; k < 40; k++) {
            now += 3000;
            const wsd_action_t a = wsd_next(&s, now);
            if (a == WSD_DO_PROBE) {
                wsd_on_probe(&s, WSD_PROBE_NOCARD, 0, now);
            } else if (a == WSD_DO_STATUS) {
                wsd_on_status(&s, false, now);
            } else if (a == WSD_DO_UNMOUNT) {
                mounted_fs = false;
                wsd_on_unmounted(&s, now);
            } else if (a == WSD_DO_RELEASE) {
                card_on = false;
                wsd_on_released(&s, now);
            } else if (a == WSD_DO_MOUNT) {
                wsd_on_mount(&s, WSD_E_IO, now);
            } else if (a == WSD_DO_FORMAT) {
                wsd_on_format(&s, false, now);
            }
        }
        CHECK(!card_on && !mounted_fs);
        CHECK_EQ(wsd_visible(&s), WSD_ABSENT);
        (void)formats;
    }
}

/* ---------------- clasificador ---------------- */

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void fat32_vbr(uint8_t *s)
{
    memset(s, 0, 512);
    s[0] = 0xEB;
    s[1] = 0x58;
    s[2] = 0x90;
    memcpy(s + 3, "MSDOS5.0", 8);
    s[11] = 0x00;
    s[12] = 0x02; /* 512 B */
    s[13] = 64;   /* 32 KB */
    s[14] = 32;   /* reservados */
    s[16] = 2;
    put32(s + 36, 1000); /* sectores por FAT */
    memcpy(s + 82, "FAT32   ", 8);
    s[510] = 0x55;
    s[511] = 0xAA;
}

static void test_classify_sd(void)
{
    uint8_t s[512];
    memset(s, 0, sizeof(s));
    CHECK_EQ(wfs_classify(s, false).kind, WFS_NONE); /* en blanco */
    fat32_vbr(s);
    wfs_class_t c = wfs_classify(s, false);
    CHECK_EQ(c.kind, WFS_FAT);
    CHECK_EQ(c.fat_bits, 32);
    /* FAT16 */
    fat32_vbr(s);
    s[22] = 200;
    memcpy(s + 54, "FAT16   ", 8);
    c = wfs_classify(s, false);
    CHECK_EQ(c.kind, WFS_FAT);
    CHECK_EQ(c.fat_bits, 16);
    /* BPB incoherente: no es FAT */
    fat32_vbr(s);
    s[13] = 3;
    CHECK(wfs_classify(s, false).kind != WFS_FAT);
    /* exFAT y NTFS (en la partición o sin tabla) */
    memset(s, 0, sizeof(s));
    s[0] = 0xEB;
    memcpy(s + 3, "EXFAT   ", 8);
    s[510] = 0x55;
    s[511] = 0xAA;
    CHECK_EQ(wfs_classify(s, true).kind, WFS_EXFAT);
    CHECK_EQ(wfs_classify(s, false).kind, WFS_EXFAT);
    memcpy(s + 3, "NTFS    ", 8);
    CHECK_EQ(wfs_classify(s, true).kind, WFS_NTFS);
    /* MBR con una partición FAT32 LBA y con una 0x07 (exFAT de fábrica) */
    memset(s, 0, sizeof(s));
    s[510] = 0x55;
    s[511] = 0xAA;
    uint8_t *e = s + 446;
    e[4] = 0x0C;
    put32(e + 8, 8192);
    put32(e + 12, 60000000u);
    c = wfs_classify(s, false);
    CHECK_EQ(c.kind, WFS_PARTITION);
    CHECK_EQ(c.lba, 8192);
    CHECK_EQ(c.ptype, 0x0C);
    e[4] = 0x07;
    put32(e + 8, 32768);
    put32(e + 12, 0xF0000000u); /* una SDXC grande: 32 bits sin signo */
    c = wfs_classify(s, false);
    CHECK_EQ(c.kind, WFS_PARTITION);
    CHECK_EQ(c.lba, 32768);
    /* GPT protectora */
    e[4] = 0xEE;
    CHECK_EQ(wfs_classify(s, false).kind, WFS_GPT);
    /* Linux */
    e[4] = 0x83;
    CHECK_EQ(wfs_classify(s, false).kind, WFS_OTHER);
    /* Entrada rota (LBA 0): se ignora */
    e[4] = 0x0C;
    put32(e + 8, 0);
    CHECK_EQ(wfs_classify(s, false).kind, WFS_NONE);
    /* Primera vacía, segunda válida */
    memset(e, 0, 16);
    e[16 + 4] = 0x0B;
    put32(e + 16 + 8, 2048);
    put32(e + 16 + 12, 1000);
    c = wfs_classify(s, false);
    CHECK_EQ(c.kind, WFS_PARTITION);
    CHECK_EQ(c.lba, 2048);
    /* Sin 55AA */
    s[511] = 0;
    CHECK_EQ(wfs_classify(s, false).kind, WFS_NONE);
    /* Barrido al azar: nunca lee fuera ni devuelve un tipo inválido. */
    for (int i = 0; i < 20000; i++) {
        for (int k = 0; k < 512; k++) {
            s[k] = (uint8_t)rnd();
        }
        if (i & 1) {
            s[510] = 0x55;
            s[511] = 0xAA;
        }
        c = wfs_classify(s, i & 2);
        CHECK(c.kind <= WFS_PARTITION);
    }
}

static void test_capacity(void)
{
    /* 32 GB SDHC */
    CHECK(wm_card_bytes(62333952, 512) == 62333952ull * 512);
    /* SDXC de "2 TB": C_SIZE máximo 0x3FFEFF -> (C_SIZE + 1) << 10 sectores. El
     * campo es int en ESP-IDF 5.5.5 y se ve negativo; leído sin signo es exacto. */
    const uint32_t sectors = (0x3FFEFFu + 1u) << 10;
    CHECK_EQ(sectors, 4294705152u);
    const int as_int = (int)sectors;
    CHECK(as_int < 0);
    CHECK(wm_card_bytes(as_int, 512) == 4294705152ull * 512ull);
    CHECK(wm_card_bytes(as_int, 512) > 2000000000000ull);
    /* 1 TB (1,8 TiB cabe; por encima de 2^31 sectores) */
    CHECK(wm_card_bytes((int)3900000000u, 512) == 3900000000ull * 512ull);
    CHECK(wm_card_bytes(1000, 0) == 512000ull);
    CHECK_EQ(wm_format_cluster(32ull * 1000 * 1000 * 1000), 32 * 1024);
    CHECK_EQ(wm_format_cluster(64ull * 1000 * 1000 * 1000), 64 * 1024);
    CHECK_EQ(wm_format_cluster(2199023255552ull), 64 * 1024);
    /* FAT32 de 2 TB con 64 KB: < 2^28 clústeres (límite de FAT32) */
    CHECK(4294705152ull * 512ull / 65536ull < 0x0FFFFFF5ull);
    char b[24];
    wm_fmt_bytes(4294705152ull * 512ull, b, sizeof(b));
    CHECK(!strcmp(b, "2,1 TB"));
    wm_fmt_bytes(31914983424ull, b, sizeof(b));
    CHECK(!strcmp(b, "31,9 GB"));
    wm_fmt_bytes(512, b, sizeof(b));
    CHECK(!strcmp(b, "512 B"));
    wm_fmt_bytes(250000000000ull, b, sizeof(b));
    CHECK(!strcmp(b, "250 GB"));
    wm_fmt_bytes(0, b, sizeof(b));
    CHECK(!strcmp(b, "0 B"));
}

static void test_texts(void)
{
    for (int e = 0; e < WSD_E_N; e++) {
        CHECK(wsd_err_text((wsd_err_t)e) != NULL);
        CHECK(strcmp(wsd_err_text((wsd_err_t)e), "?") != 0);
    }
    for (int st = 0; st < WSD_STATE_N; st++) {
        CHECK(strcmp(wsd_state_name((wsd_state_t)st), "?") != 0);
    }
}

static void test_paths(void)
{
    char o[48];
    CHECK(wm_path_safe("/"));
    CHECK(wm_path_safe("/Musica/tema.mp3"));
    CHECK(wm_path_safe("/Grabaciones/Grabación 27-10 18.04.wav"));
    CHECK(wm_path_safe("/a/.oculto"));
    CHECK(wm_path_safe("/a/..b"));
    CHECK(!wm_path_safe(NULL));
    CHECK(!wm_path_safe(""));
    CHECK(!wm_path_safe("Musica/x"));
    CHECK(!wm_path_safe("/Musica/"));
    CHECK(!wm_path_safe("//x"));
    CHECK(!wm_path_safe("/a//b"));
    CHECK(!wm_path_safe("/.."));
    CHECK(!wm_path_safe("/a/../../etc"));
    CHECK(!wm_path_safe("/a/./b"));
    CHECK(!wm_path_safe("/a\\b"));
    CHECK(!wm_path_safe("/c:x"));
    CHECK(!wm_path_safe("/a*b"));
    CHECK(!wm_path_safe("/a\nb"));
    CHECK(!wm_path_safe("/a\x7f"));
    CHECK(!wm_path_safe("/\xC3"));          /* UTF-8 cortado */
    CHECK(!wm_path_safe("/\xC0\xAF"));     /* forma larga de '/' */
    CHECK(!wm_path_safe("/\xF5\x80\x80\x80"));
    CHECK(wm_path_join("/sdcard", "/Musica/x.mp3", o, sizeof(o)));
    CHECK(!strcmp(o, "/sdcard/Musica/x.mp3"));
    CHECK(wm_path_join("/sdcard", "/", o, sizeof(o)));
    CHECK(!strcmp(o, "/sdcard"));
    CHECK(!wm_path_join("/sdcard", "/../storage/x", o, sizeof(o)));
    CHECK_EQ(o[0], 0);
    CHECK(!wm_path_join("/sdcard", "/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", o, sizeof(o)));
    CHECK_EQ(o[0], 0);
    CHECK(!wm_path_join(NULL, "/x", o, sizeof(o)));
    CHECK(!wm_path_join("/sdcard", "/x", o, 0));
}

/* ---------------- wm_web: lo que no toca sockets ---------------- */

static void test_web_logic(void)
{
    char u[64];
    CHECK(wweb_url(u, sizeof(u), "192.168.1.50", 8080, "123456"));
    CHECK(!strcmp(u, "http://192.168.1.50:8080/?k=123456"));
    CHECK(!wweb_url(u, sizeof(u), "", 8080, "123456"));
    CHECK_EQ(u[0], 0);
    CHECK(!wweb_url(u, sizeof(u), "192.168.1.50", 8080, "12345")); /* sin código válido no hay QR */
    CHECK(!wweb_url(u, sizeof(u), "192.168.1.50", 8080, NULL));
    CHECK(!wweb_url(u, 20, "192.168.100.200", 8080, "123456")); /* no cabe: vacío, nunca cortado */
    CHECK_EQ(u[0], 0);

    char j[160];
    CHECK(wweb_dest_json(j, sizeof(j), WM_VOL_SD, true, 5000000000ull));
    CHECK(!strcmp(j, "\"vol\":\"sd\",\"name\":\"microSD\",\"ready\":1,\"free\":5000000000"));
    CHECK(wweb_dest_json(j, sizeof(j), WM_VOL_WATCH, false, 123));
    CHECK(!strcmp(j, "\"vol\":\"watch\",\"name\":\"Reloj\",\"ready\":0,\"free\":0"));
    CHECK(!wweb_dest_json(j, 10, WM_VOL_SD, true, 1));
    CHECK(strstr(wweb_media_json(), "\"video\"") != NULL && strstr(wweb_media_json(), "\"photo\"") != NULL);

    /* caducidad: redondea hacia arriba y no se confunde con la vuelta del contador */
    CHECK_EQ(wweb_code_left_s(1000, 1000, 600000), 600u);
    CHECK_EQ(wweb_code_left_s(1001, 1000, 600000), 600u);
    CHECK_EQ(wweb_code_left_s(600999, 1000, 600000), 1u);
    CHECK_EQ(wweb_code_left_s(601000, 1000, 600000), 0u);
    CHECK_EQ(wweb_code_left_s(5000, 0xFFFFF000u, 600000), 591u); /* 9096 ms de edad */
    CHECK_EQ(wweb_code_left_s(5, 0, 0), UINT32_MAX);

    /* vida del servidor */
    wweb_life_t l = {.wanted = true, .since_keep = 0, .idle_ms = 0, .busy = false};
    CHECK(!wweb_should_stop(&l));
    l.since_keep = WWEB_KEEP_MS;
    CHECK(wweb_should_stop(&l)); /* la pantalla ya no lo pide */
    l.busy = true;
    CHECK(!wweb_should_stop(&l)); /* lo que está en marcha termina */
    l = (wweb_life_t){.wanted = false, .busy = false};
    CHECK(wweb_should_stop(&l));
    l = (wweb_life_t){.wanted = true, .since_keep = 100, .idle_ms = WWEB_IDLE_STOP_MS};
    CHECK(wweb_should_stop(&l)); /* 15 min sin peticiones */
    l.busy = true;
    CHECK(!wweb_should_stop(&l));

    for (int st = WWEB_OFF; st <= WWEB_ERROR; st++) {
        CHECK(strcmp(wweb_state_text((wweb_state_t)st), "?") != 0);
    }
    const uint8_t mac[6] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0x06};
    wweb_dev_id(mac, u, sizeof(u));
    CHECK(!strcmp(u, "S3-A1B2C3D4E506"));
}

void test_media_all(void)
{
    test_paths();
    test_boot_and_mount();
    test_surprise_removal_waits_for_leases();
    test_eject_keeps_card_out();
    test_unsupported_formats();
    test_format_from_ready();
    test_retry_and_interest();
    test_random_driver();
    test_classify_sd();
    test_capacity();
    test_texts();
    test_web_logic();
}
