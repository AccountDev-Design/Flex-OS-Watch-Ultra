#define _DEFAULT_SOURCE /* mkdtemp */
/*
 * Pruebas de PC de la puerta de escritura de "storage" (F4.6).
 */
#include <string.h>

#include "stg_logic.h"
#include "stg_usage.h"
#include "ui_logic.h"
#include "test_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static stg_flash_t good(void)
{
    stg_flash_t f = {STG_JEDEC_GD25Q256, STG_FLASH_32MB, STG_FLASH_32MB, true, 0x1000000, 0x1000000,
                     FLASH_PROBE_UPPER_DISTINCT};
    return f;
}

static void test_gate(void)
{
    stg_flash_t f = good();
    CHECK_EQ(stg_gate(&f), STG_GATE_OK);
    /* cada condición, por separado, cierra la puerta */
    f = good();
    f.storage_found = false;
    CHECK_EQ(stg_gate(&f), STG_GATE_NO_PARTITION);
    f = good();
    f.jedec_id = 0xEF4019;
    CHECK_EQ(stg_gate(&f), STG_GATE_CHIP);
    f = good();
    f.physical_size = 16u * 1024 * 1024;
    CHECK_EQ(stg_gate(&f), STG_GATE_SIZE);
    f = good();
    f.configured_size = 16u * 1024 * 1024;
    CHECK_EQ(stg_gate(&f), STG_GATE_SIZE);
    f = good();
    f.storage_offset = 0x1001000; /* se saldría del chip */
    CHECK_EQ(stg_gate(&f), STG_GATE_BOUNDS);
    f = good();
    f.storage_offset = 0x1000100; /* sin alinear */
    f.storage_size = 0x800000;
    CHECK_EQ(stg_gate(&f), STG_GATE_BOUNDS);
    static const flash_probe_verdict_t bad[] = {FLASH_PROBE_ALIASED, FLASH_PROBE_INCONCLUSIVE, FLASH_PROBE_READ_ERROR,
                                                FLASH_PROBE_NOT_CONFIGURED};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        f = good();
        f.verdict = bad[i];
        CHECK_EQ(stg_gate(&f), STG_GATE_PROBE);
    }
    for (int g = STG_GATE_OK; g <= STG_GATE_PROBE; g++) {
        CHECK(stg_gate_text((stg_gate_t)g)[0] != '?');
    }
}

static void test_sector(void)
{
    /* el último sector de storage: si diera la vuelta, caería en assets, no en el bootloader */
    CHECK_EQ(stg_test_offset(0x1000000), 0xFFF000);
    CHECK_EQ(0x1000000 + stg_test_offset(0x1000000) - 0x1000000, 0xFFF000);
    CHECK_EQ(stg_test_offset(4096), 0);
    CHECK_EQ(stg_test_offset(100), 0);
    uint8_t a[4096], b[4096];
    stg_pattern(a, sizeof(a), 0x1FFF000);
    stg_pattern(b, sizeof(b), 0x1FFF000);
    CHECK(memcmp(a, b, sizeof(a)) == 0); /* determinista */
    int bad = 0, distinct = 0;
    for (size_t i = 0; i < sizeof(a); i++) {
        bad += a[i] == 0xFF || a[i] == 0x00;
        distinct += i && a[i] != a[i - 1];
    }
    CHECK_EQ(bad, 0); /* ni borrado (0xFF) ni a cero */
    CHECK(distinct > 3500);
    stg_pattern(b, sizeof(b), 0x1234);
    CHECK(memcmp(a, b, sizeof(a)) != 0);
    CHECK(stg_crc32("123456789", 9) == 0xCBF43926u);
}

static void put(const char *root, const char *rel, size_t n)
{
    char p[256];
    snprintf(p, sizeof(p), "%s%s", root, rel);
    FILE *f = fopen(p, "wb");
    CHECK(f != NULL);
    if (f) {
        for (size_t i = 0; i < n; i++) {
            fputc((int)(i & 0xFF), f);
        }
        fclose(f);
    }
}

static void mk(const char *root, const char *rel)
{
    char p[256];
    snprintf(p, sizeof(p), "%s%s", root, rel);
    CHECK(mkdir(p, 0755) == 0);
}

/* Recuento por categorías del rediseño de Almacenamiento (stg_usage) */
static void test_usage(void)
{
    char root[128];
    const char *tmp = getenv("TMPDIR");
    snprintf(root, sizeof(root), "%.90s/stg_usage_XXXXXX", tmp && tmp[0] ? tmp : "/tmp");
    CHECK(mkdtemp(root) != NULL);
    stg_usage_t u;
    stg_usage_scan(root, &u); /* sin carpetas: todo a cero, sin errores */
    CHECK(u.docs == 0 && u.trash == 0 && u.files == 0 && !u.truncated);
    mk(root, "/Documentos");
    mk(root, "/Documentos/Viaje");
    mk(root, "/Notas");
    mk(root, "/Paint");
    mk(root, "/Imagenes");
    mk(root, "/Papelera");
    mk(root, "/System");
    put(root, "/Documentos/a.txt", 1000);
    put(root, "/Documentos/Viaje/b.txt", 2500);
    put(root, "/Notas/n.txt", 300);
    put(root, "/Paint/d.fxp", 4096);
    put(root, "/Imagenes/f.jpg", 10000);
    put(root, "/Papelera/x@Notas@y.txt", 777);
    put(root, "/System/cache.bin", 50000); /* datos de apps: no son documentos */
    stg_usage_scan(root, &u);
    CHECK_EQ(u.docs, 1000 + 2500 + 300 + 4096 + 10000);
    CHECK_EQ(u.trash, 777);
    CHECK_EQ(u.files, 6);
    CHECK(!u.truncated);
    uint32_t files = 0;
    bool tr = false;
    char p[256];
    snprintf(p, sizeof(p), "%s/System", root);
    CHECK_EQ(stg_du(p, &files, &tr), 50000);
    snprintf(p, sizeof(p), "%s/no-existe", root);
    CHECK_EQ(stg_du(p, &files, &tr), 0);
    /* limpieza */
    static const char *const fs[] = {"/Documentos/a.txt", "/Documentos/Viaje/b.txt", "/Notas/n.txt", "/Paint/d.fxp",
                                     "/Imagenes/f.jpg", "/Papelera/x@Notas@y.txt", "/System/cache.bin"};
    for (size_t i = 0; i < sizeof(fs) / sizeof(fs[0]); i++) {
        snprintf(p, sizeof(p), "%s%s", root, fs[i]);
        unlink(p);
    }
    static const char *const ds[] = {"/Documentos/Viaje", "/Documentos", "/Notas", "/Paint", "/Imagenes", "/Papelera",
                                     "/System", ""};
    for (size_t i = 0; i < sizeof(ds) / sizeof(ds[0]); i++) {
        snprintf(p, sizeof(p), "%s%s", root, ds[i]);
        rmdir(p);
    }
}

static void test_usage_text(void)
{
    char b[48];
    ui_fmt_usage(b, sizeof(b), 16ull << 20, 32ull << 20);
    CHECK(strcmp(b, "16/32 MB  (50%)") == 0);
    ui_fmt_usage(b, sizeof(b), 4718592, 8ull << 20); /* 4,5 MB de 8 */
    CHECK(strcmp(b, "4,5/8 MB  (56%)") == 0);
    ui_fmt_usage(b, sizeof(b), 5, 0);
    CHECK(strcmp(b, "--") == 0);
    ui_fmt_usage(b, sizeof(b), 40ull << 20, 32ull << 20); /* nunca más del 100 % */
    CHECK(strcmp(b, "32/32 MB  (100%)") == 0);
    ui_fmt_left_s(0, b, sizeof(b));
    CHECK(strcmp(b, "0 s") == 0);
    ui_fmt_left_s(59, b, sizeof(b));
    CHECK(strcmp(b, "59 s") == 0);
    ui_fmt_left_s(45 * 60 + 30, b, sizeof(b));
    CHECK(strcmp(b, "45 min") == 0);
    ui_fmt_left_s(3600 + 20 * 60, b, sizeof(b));
    CHECK(strcmp(b, "1 h 20 min") == 0);
    ui_fmt_left_s(2 * 3600, b, sizeof(b));
    CHECK(strcmp(b, "2 h") == 0);
    ui_fmt_left_s(24 * 3600 + 59 * 60, b, sizeof(b)); /* a partir de 10 h, sin minutos */
    CHECK(strcmp(b, "24 h") == 0);
    ui_fmt_left_s(0xFFFFFFFFu, b, sizeof(b));
    CHECK(strcmp(b, "1193046 h") == 0);
    CHECK_EQ(ui_permille(1, 0), 0);
    CHECK_EQ(ui_permille(3, 2), 1000);
    CHECK_EQ(ui_permille(1, 4), 250);
    CHECK_EQ(ui_permille(0xFFFFFFFFFFull, 0x1FFFFFFFFFFull), 499);
    CHECK_EQ(ui_mem_health(180 * 1024, 110 * 1024, 6900 * 1024, 0), UI_MEM_OK);
    CHECK_EQ(ui_mem_health(180 * 1024, 110 * 1024, 6900 * 1024, 3), UI_MEM_TIGHT);
    CHECK_EQ(ui_mem_health(40 * 1024, 30 * 1024, 6900 * 1024, 0), UI_MEM_TIGHT);
    CHECK_EQ(ui_mem_health(180 * 1024, 110 * 1024, 600 * 1024, 0), UI_MEM_TIGHT);
    CHECK_EQ(ui_mem_health(20 * 1024, 10 * 1024, 6900 * 1024, 0), UI_MEM_LOW);
    CHECK_EQ(ui_mem_health(180 * 1024, 110 * 1024, 100 * 1024, 0), UI_MEM_LOW);
    CHECK(strcmp(ui_mem_health_text(UI_MEM_OK), "Óptimo") == 0);
}

void test_storage_all(void)
{
    test_gate();
    test_sector();
    test_usage();
    test_usage_text();
}
