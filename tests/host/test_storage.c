/*
 * Pruebas de PC de la puerta de escritura de "storage" (F4.6).
 */
#include <string.h>

#include "stg_logic.h"
#include "test_util.h"

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

void test_storage_all(void)
{
    test_gate();
    test_sector();
}
