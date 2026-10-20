/*
 * Pruebas del OTA (F7): los vectores de docs/F7-OTA.md (sacados de
 * FlexOS_OTA.cpp compilado en el PC), los cambios del reloj y el motor de
 * instalación entero contra ota_fake (servidor y flash falsos, firma real).
 */
#include <stdlib.h>
#include <string.h>
#include "ota_core.h"
#include "ota_fake.h"
#include "ota_sig.h"
#include "test_util.h"

static const ota_opts_t kRef = {false, false};    /* interruptores por defecto de la referencia */
static const ota_opts_t kStrict = {true, true};   /* ANTIROLLBACK=1 y REQUIRE_SIGNATURE=1 */

static bool vp(const char *s, int a, int b, int c)
{
    ota_ver_t v;
    return ota_ver_parse(s, &v) && v.v[0] == a && v.v[1] == b && v.v[2] == c;
}

static int vc(const char *a, const char *b)
{
    ota_ver_t x, y;
    if (!ota_ver_parse(a, &x) || !ota_ver_parse(b, &y)) {
        return 99;
    }
    return ota_ver_cmp(&x, &y);
}

static void test_versions(void)
{
    /* §5.1 */
    CHECK(vp("1.0.0", 1, 0, 0));                 /* V1 */
    CHECK(vp("1.2.0", 1, 2, 0));                 /* V2 */
    CHECK(vp("0.1.0", 0, 1, 0));                 /* V3 */
    CHECK(vp("0.1.0-b1", 0, 1, 0));              /* V4. RELOJ: la referencia daba false */
    CHECK(!ota_ver_parse("1.0", NULL));          /* V5 */
    CHECK(!ota_ver_parse("1.0.0.0", NULL));      /* V6 */
    CHECK(!ota_ver_parse("", NULL));             /* V7 */
    CHECK(!ota_ver_parse("1..0", NULL));         /* V8 */
    CHECK(!ota_ver_parse("1.0.", NULL));         /* V9 */
    CHECK(!ota_ver_parse(".1.0", NULL));         /* V10 */
    CHECK(vp("01.002.0003", 1, 2, 3));           /* V11 */
    CHECK(vp("65535.0.0", 65535, 0, 0));         /* V12 */
    CHECK(!ota_ver_parse("65536.0.0", NULL));    /* V13 */
    CHECK(!ota_ver_parse(" 1.0.0", NULL));       /* V14 */
    CHECK(!ota_ver_parse("1.0.0 ", NULL));       /* V15 */
    CHECK(!ota_ver_parse("v1.0.0", NULL));       /* V16 */
    CHECK(!ota_ver_parse("-1.0.0", NULL));       /* V17 */
    CHECK(vp("1.2.3", 1, 2, 3));                 /* V18 */
    CHECK(vp("0.0.0", 0, 0, 0));                 /* V19 */
    CHECK(!ota_ver_parse("1.0.0\n", NULL));      /* V20 */
    /* RELOJ: preliberación */
    ota_ver_t v;
    CHECK(ota_ver_parse("1.0.0-rc.1", &v) && !strcmp(v.pre, "rc.1"));
    CHECK(ota_ver_parse("1.0.0-x-y", NULL));
    CHECK(!ota_ver_parse("1.0.0-", NULL));
    CHECK(!ota_ver_parse("1.0.0-a..b", NULL));
    CHECK(!ota_ver_parse("1.0.0-a.", NULL));
    CHECK(!ota_ver_parse("1.0.0-a_b", NULL));
    CHECK(!ota_ver_parse("1.0.0+build", NULL));
    CHECK(!ota_ver_parse("1.0.0-ñ", NULL));
    CHECK(ota_ver_parse("1.0.0-abcdefghijklmnopq", NULL));   /* 23 caracteres */
    CHECK(!ota_ver_parse("1.0.0-abcdefghijklmnopqr", NULL)); /* 24: no cabe */
    CHECK(!ota_ver_parse(NULL, NULL));

    /* §5.2 */
    CHECK_EQ(vc("1.2.0", "1.0.0"), 1);    /* C1 */
    CHECK_EQ(vc("1.0.0", "1.0.0"), 0);    /* C2 */
    CHECK_EQ(vc("1.0.0", "1.0.1"), -1);   /* C3 */
    CHECK_EQ(vc("1.10.0", "1.9.9"), 1);   /* C4 */
    CHECK_EQ(vc("2.0.0", "1.99.99"), 1);  /* C5 */
    CHECK_EQ(vc("01.0.0", "1.0.0"), 0);   /* C6 */
    CHECK_EQ(vc("0.1.1", "0.1.0"), 1);    /* C7 */
    CHECK_EQ(vc("1.0.0", "2.0.0"), -1);   /* C8 */
    /* RELOJ: orden de SemVer 2.0.0, punto 11 */
    static const char *const order[] = {"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                                        "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0"};
    for (size_t i = 0; i + 1 < sizeof(order) / sizeof(order[0]); i++) {
        CHECK_EQ(vc(order[i], order[i + 1]), -1);
        CHECK_EQ(vc(order[i + 1], order[i]), 1);
        CHECK_EQ(vc(order[i], order[i]), 0);
    }
    CHECK_EQ(vc("0.1.0-b1", "0.1.0"), -1);  /* el version.txt de hoy < la primera final */
    CHECK_EQ(vc("0.1.0-b1", "0.1.0-b2"), -1);
    CHECK_EQ(vc("0.1.0-b1", "0.0.9"), 1);
    CHECK_EQ(vc("0.1.0-b.10", "0.1.0-b.9"), 1);  /* numérico: 10 > 9 */
    CHECK_EQ(vc("0.1.0-b10", "0.1.0-b9"), -1);   /* alfanumérico: "b10" < "b9" (SemVer) */
    CHECK_EQ(vc("1.0.0-01", "1.0.0-1"), 0);
}

static void test_utils(void)
{
    /* §5.3 */
    CHECK(ota_md5_usable("e26fc99e596248dea0b33b497d1a99df"));   /* H1 */
    CHECK(!ota_md5_usable("00000000000000000000000000000000"));  /* H2 */
    CHECK(ota_md5_usable("E26FC99E596248DEA0B33B497D1A99DF"));   /* H3 */
    CHECK(!ota_md5_usable("e26fc99e596248dea0b33b497d1a99d"));   /* H4 */
    CHECK(!ota_md5_usable("e26fc99e596248dea0b33b497d1a99df0")); /* H5 */
    CHECK(!ota_md5_usable("g26fc99e596248dea0b33b497d1a99df"));  /* H6 */
    CHECK(!ota_md5_usable(""));                                  /* H7 */
    CHECK(ota_md5_usable("00000000000000000000000000000001"));   /* H8 */

    /* §5.4 */
    static const struct {
        uint32_t b;
        const char *t;
    } fb[] = {{0, "0 B"},           {1, "1 B"},           {1023, "1023 B"},      {1024, "1 KB"},
              {1536, "1 KB"},       {1048575, "1023 KB"}, {1048576, "1,0 MB"},   {1153433, "1,0 MB"},
              {1325144, "1,2 MB"},  {1326144, "1,2 MB"},  {1572864, "1,5 MB"},   {1900544, "1,8 MB"},
              {6291456, "6,0 MB"},  {8388608, "8,0 MB"},  {4294967295u, "4095,9 MB"}};
    for (size_t i = 0; i < sizeof(fb) / sizeof(fb[0]); i++) {
        char b[24];
        ota_fmt_bytes(fb[i].b, b, sizeof(b));
        CHECK(!strcmp(b, fb[i].t));
    }

    uint8_t bin[4];
    char hex[9];
    CHECK(ota_hex_decode("00fFa1B2", bin, 4) && bin[0] == 0 && bin[1] == 0xFF && bin[2] == 0xA1 && bin[3] == 0xB2);
    CHECK(!ota_hex_decode("00fFa1B", bin, 4));
    CHECK(!ota_hex_decode("00fFa1Bz", bin, 4));
    ota_hex_encode(bin, 4, hex);
    CHECK(!strcmp(hex, "00ffa1b2"));

    /* Textos: los números de la referencia no se mueven; los nuevos van detrás. */
    CHECK_EQ(OTA_ERR_ROLLBACK, 21);
    CHECK_EQ(OTA_ERROR, 10);
    CHECK(!strcmp(ota_err_text(OTA_ERR_NO_WIFI), "Sin conexión a Internet"));
    CHECK(!strcmp(ota_err_text(OTA_ERR_HTTP_STATUS), "Archivo no encontrado"));
    CHECK(!strcmp(ota_err_text(OTA_ERR_ROLLBACK), "Esa versión es anterior a la instalada"));
    CHECK(!strcmp(ota_err_text(0), "Error desconocido"));
    CHECK(!strcmp(ota_err_text(200), "Error desconocido"));
    for (int e = 1; e < OTA_ERR_COUNT; e++) {
        CHECK(ota_err_text(e) && strcmp(ota_err_text(e), "Error desconocido") != 0);
    }
    CHECK(!strcmp(ota_phase_text(OTA_DOWNLOADING), "Descargando..."));
    CHECK(!strcmp(ota_phase_text(OTA_IDLE), "Preparando..."));
    CHECK(ota_busy_state(OTA_CHECKING) && ota_busy_state(OTA_REBOOTING) && ota_busy_state(OTA_VERIFYING));
    CHECK(!ota_busy_state(OTA_IDLE) && !ota_busy_state(OTA_AVAILABLE) && !ota_busy_state(OTA_ERROR) &&
          !ota_busy_state(OTA_UP_TO_DATE));
}

/* ---------------- Manifiestos (§5.5) ---------------- */

static ota_info_t s_info;

static int pm(const char *json, const char *key, const char *local, const ota_opts_t *o, bool *avail)
{
    return ota_parse_manifest(json, strlen(json), key, local, o, &s_info, avail);
}

/* Resultado en una cifra: 100 = AVAILABLE, 101 = UP_TO_DATE, si no el error. */
static int pr(const char *json, const char *key, const char *local, const ota_opts_t *o)
{
    bool a = false;
    const int e = pm(json, key, local, o, &a);
    return e ? e : (a ? 100 : 101);
}
#define AV 100
#define UTD 101

static const char kReal[] =
    "{\n"
    "  \"flex_os_ultra\": {\n"
    "    \"version\": \"1.2.0\",\n"
    "    \"url\": \"https://raw.githubusercontent.com/AccountDev-Design/Flex-OS-Ecosistema/main/ota/flexos_ultra.bin\",\n"
    "    \"size\": 1325144,\n"
    "    \"md5\": \"e26fc99e596248dea0b33b497d1a99df\",\n"
    "    \"changelog\": \"Almacenamiento real con LittleFS.\\nWiFi y BLE reales (conectividad de verdad).\\nApp Paint con guardado real de dibujos.\\nApp Notas con guardado real de texto.\\nExplorador de archivos real.\n"
    "  },\n"
    "  \"flex_os_ultra_s3\": {\n"
    "    \"version\": \"1.0.0\",\n"
    "    \"url\": \"https://raw.githubusercontent.com/AccountDev-Design/Flex-OS-Ecosistema/main/ota/flexos_ultra_s3.bin\",\n"
    "    \"size\": 1900544,\n"
    "    \"changelog\": \"Sistema global de actualizaciones OTA.\"\n"
    "  },\n"
    "  \"flex_os_pro\": {\n"
    "    \"version\": \"1.0.0\",\n"
    "    \"url\": \"https://raw.githubusercontent.com/AccountDev-Design/Flex-OS-Ecosistema/main/ota/flexos_pro.bin\",\n"
    "    \"size\": 1572864\n"
    "  }\n"
    "}\n";

static const char kOk[] =
    "{\n"
    "  \"flex_os_watch_ultra\": {\n"
    "    \"version\": \"1.1.0\",\n"
    "    \"url\": \"https://example.invalid/flex_os_watch_ultra.bin\",\n"
    "    \"size\": 2000000,\n"
    "    \"md5\": \"e26fc99e596248dea0b33b497d1a99df\",\n"
    "    \"changelog\": \"Linea 1.\\nLinea 2.\",\n"
    "    \"sig\": \"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\",\n"
    "    \"sigalg\": \"ed25519\"\n"
    "  }\n"
    "}\n";

static void test_manifest(void)
{
    /* M_REAL: el manifiesto publicado no es JSON válido (cadena sin cerrar en
     * la línea 7). RELOJ: ningún bloque se da por bueno; la referencia leía
     * flex_os_ultra_s3 y flex_os_pro porque no seguía las cadenas. */
    CHECK_EQ(pr(kReal, "flex_os_ultra", "1.0.0", &kRef), OTA_ERR_NO_BLOCK);
    CHECK_EQ(pr(kReal, "flex_os_ultra_s3", "1.0.0", &kRef), OTA_ERR_NO_BLOCK);
    CHECK_EQ(pr(kReal, "flex_os_pro", "1.0.0", &kRef), OTA_ERR_NO_BLOCK);
    CHECK_EQ(pr(kReal, OTA_PRODUCT, "1.0.0", &kRef), OTA_ERR_NO_BLOCK);

    /* M_OK */
    CHECK_EQ(pr(kOk, OTA_PRODUCT, "1.0.0", &kRef), AV);
    CHECK(!strcmp(s_info.ver, "1.1.0"));
    CHECK(!strcmp(s_info.url, "https://example.invalid/flex_os_watch_ultra.bin"));
    CHECK_EQ(s_info.size, 2000000);
    CHECK(s_info.md5_ok);
    CHECK_EQ(strlen(s_info.sig), 128);
    CHECK(!strcmp(s_info.changelog, "Linea 1.\nLinea 2."));
    CHECK_EQ(strlen(s_info.changelog), 17);
    CHECK_EQ(pr(kOk, OTA_PRODUCT, "1.1.0", &kRef), UTD);
    CHECK_EQ(pr(kOk, OTA_PRODUCT, "1.2.0", &kRef), UTD);
    CHECK_EQ(pr(kOk, OTA_PRODUCT, "1.2.0", &kStrict), OTA_ERR_ROLLBACK);
    CHECK_EQ(pr(kOk, OTA_PRODUCT, "0.1.0-b1", &kRef), AV); /* RELOJ: la referencia daba BAD_VERSION */
    CHECK_EQ(pr(kOk, "flex_os_watch", "1.0.0", &kRef), OTA_ERR_NO_BLOCK);
    /* RELOJ: con firma obligatoria, otro algoritmo no vale */
    CHECK_EQ(pr(kOk, OTA_PRODUCT, "1.0.0", &kStrict), OTA_ERR_BAD_SIG);

    /* Casos mínimos (clave k, local 1.0.0): referencia / estricto */
    static const struct {
        const char *json;
        int ref, strict;
    } m[] = {
        {"{\"flex_os_ultra_s3\":{\"version\":\"9.0.0\",\"url\":\"https://a/b\"}}", OTA_ERR_NO_BLOCK, OTA_ERR_NO_BLOCK},
        {"{\"k\":{\"version\":\"2.0.0\",\"url\":\"http://a/b.bin\"}}", OTA_ERR_BAD_URL, OTA_ERR_BAD_URL},   /* M_HTTP */
        {"{\"k\":{\"version\":\"2.0.0\"}}", OTA_ERR_JSON, OTA_ERR_JSON},                                  /* M_SIN_URL */
        {"{\"k\":{\"version\":\"1.0.0\"}}", UTD, UTD},                                                   /* M_SIN_URL_IGUAL */
        {"{\"k\":{\"version\":\"2.0.0\",\"url\":5}}", OTA_ERR_JSON, OTA_ERR_JSON},                         /* M_URL_NUM */
        {"{\"k\":{\"version\":\"1.1\",\"url\":\"https://a/b\"}}", OTA_ERR_BAD_VERSION, OTA_ERR_BAD_VERSION}, /* M_VER_2C */
        {"{\"k\":{\"version\":\"1.1.0-rc1\",\"url\":\"https://a/b\"}}", AV, OTA_ERR_NO_SIG},             /* M_VER_SUFIJO (RELOJ) */
        {"{\"k\":{\"version\":110,\"url\":\"https://a/b\"}}", OTA_ERR_JSON, OTA_ERR_JSON},                 /* M_VER_NUM */
        {"{\"k\":{\"url\":\"https://a/b\"}}", OTA_ERR_JSON, OTA_ERR_JSON},                                 /* M_SIN_VER */
        {"{\"k\":{\"version\":\"1.1.0}}", OTA_ERR_NO_BLOCK, OTA_ERR_NO_BLOCK},                             /* M_VER_ABIERTA */
        {"{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\"", OTA_ERR_NO_BLOCK, OTA_ERR_NO_BLOCK},      /* M_LLAVES */
        {"{\"k\":\"2.0.0\"}", OTA_ERR_NO_BLOCK, OTA_ERR_NO_BLOCK},                                         /* M_NO_OBJ */
        {"<html>404</html>", OTA_ERR_NO_BLOCK, OTA_ERR_NO_BLOCK},                                          /* M_BASURA */
        {"{\"k\":{\"version\":\"0.9.0\",\"url\":\"https://a/b\"}}", UTD, OTA_ERR_ROLLBACK},                /* M_ANTERIOR */
        {"{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"size\":\"123\"}}", AV, OTA_ERR_NO_SIG},  /* M_SIZE_STR */
        {"{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"sigalg\":\"ed25519\"}}", AV, OTA_ERR_NO_SIG}, /* M_SOLO_SIGALG */
        {"{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"md5\":\"00000000000000000000000000000000\"}}", AV,
         OTA_ERR_NO_SIG},                                                                                  /* M_MD5_CEROS */
        {"{ \"k\" :\n { \"version\" :\t\"2.0.0\" , \"url\" : \"https://a/b\" } }", AV, OTA_ERR_NO_SIG},     /* M_ESPACIOS */
        {"{\"k\":{\"url\":\"https://a/b\",\"version\":\"2.0.0\"}}", AV, OTA_ERR_NO_SIG},                    /* M_ORDEN */
        /* RELOJ: más casos */
        {"{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\"}} x", OTA_ERR_NO_BLOCK, OTA_ERR_NO_BLOCK},  /* basura detrás */
        {"{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://\"}}", OTA_ERR_BAD_URL, OTA_ERR_BAD_URL},
        {"{\"k\":{\"version\":\"2.0.0\",\"url\":\"https:///b\"}}", OTA_ERR_BAD_URL, OTA_ERR_BAD_URL},
        {"{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b c\"}}", OTA_ERR_BAD_URL, OTA_ERR_BAD_URL},
        {"{\"k\":{\"version\":\"1.0.0-abcdefghijklmnopqr\",\"url\":\"https://a/b\"}}", OTA_ERR_BAD_VERSION,
         OTA_ERR_BAD_VERSION},
        {"{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"sig\":\"\"}}", AV, OTA_ERR_NO_SIG},
        {"{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"sig\":7}}", AV, OTA_ERR_NO_SIG},
        {"{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"sig\":\"abc\"}}", AV, OTA_ERR_BAD_SIG},
        {"{\"k\":[1,2]}", OTA_ERR_NO_BLOCK, OTA_ERR_NO_BLOCK},
        {"[]", OTA_ERR_NO_BLOCK, OTA_ERR_NO_BLOCK},
        {"{\"x\":[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]],\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\"}}",
         OTA_ERR_NO_BLOCK, OTA_ERR_NO_BLOCK}, /* más de 16 niveles */
        {"{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/\001b\"}}", OTA_ERR_NO_BLOCK, OTA_ERR_NO_BLOCK}, /* control crudo */
        {"{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"x\":01}}", OTA_ERR_NO_BLOCK, OTA_ERR_NO_BLOCK},
        {"{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"x\":tru}}", OTA_ERR_NO_BLOCK, OTA_ERR_NO_BLOCK},
        {"{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"x\":\"\\q\"}}", OTA_ERR_NO_BLOCK, OTA_ERR_NO_BLOCK},
        {"{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"x\":[true,false,null,-1.5e3,{}]}}", AV,
         OTA_ERR_NO_SIG},
    };
    for (size_t i = 0; i < sizeof(m) / sizeof(m[0]); i++) {
        const char *key = i == 0 ? "flex_os_ultra" : "k";
        const int r = pr(m[i].json, key, "1.0.0", &kRef), s = pr(m[i].json, key, "1.0.0", &kStrict);
        if (r != m[i].ref || s != m[i].strict) {
            fprintf(stderr, "  manifiesto %zu: %d/%d, esperado %d/%d\n", i, r, s, m[i].ref, m[i].strict);
        }
        CHECK_EQ(r, m[i].ref);
        CHECK_EQ(s, m[i].strict);
    }
    /* M_PREFIJO_INV */
    CHECK_EQ(pr("{\"flex_os_ultra\":{\"version\":\"9.0.0\",\"url\":\"https://a/b\"}}", "flex_os_ultra_s3", "1.0.0",
                &kRef),
             OTA_ERR_NO_BLOCK);
    /* M_VACIO */
    bool a;
    CHECK_EQ(ota_parse_manifest("", 0, "k", "1.0.0", &kRef, &s_info, &a), OTA_ERR_JSON);
    /* M_SIZE_STR: size = 0 */
    CHECK_EQ(pr("{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"size\":\"123\"}}", "k", "1.0.0", &kRef), AV);
    CHECK_EQ(s_info.size, 0);
    CHECK_EQ(pr("{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"size\":4294967295}}", "k", "1.0.0", &kRef), AV);
    CHECK_EQ(s_info.size, 4294967295u);
    CHECK_EQ(pr("{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"size\":4294967296}}", "k", "1.0.0", &kRef), AV);
    CHECK_EQ(s_info.size, 0);
    CHECK_EQ(pr("{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"size\":12.5}}", "k", "1.0.0", &kRef), AV);
    CHECK_EQ(s_info.size, 0);
    CHECK_EQ(pr("{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"size\":-5}}", "k", "1.0.0", &kRef), AV);
    CHECK_EQ(s_info.size, 0);
    /* M_ESCAPES. RELOJ: \u00f1 da "ñ" (la referencia, "?") */
    CHECK_EQ(pr("{\"k\":{\"version\":\"2.0.0\",\"url\":\"https:\\/\\/a\\/b\",\"changelog\":\"A\\tB\\r\\nC\\u00f1D \\\"q\\\"\"}}",
                "k", "1.0.0", &kRef),
             AV);
    CHECK(!strcmp(s_info.url, "https://a/b"));
    CHECK(!strcmp(s_info.changelog, "A B\nCñD \"q\""));
    CHECK_EQ(pr("{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"changelog\":\"\\u0000\\ud83d\\u20ac\"}}", "k",
                "1.0.0", &kRef),
             AV);
    CHECK(!strcmp(s_info.changelog, "??€"));
    /* M_SOLO_SIGALG: "sig" no casa con "sigalg" */
    CHECK_EQ(pr("{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"sigalg\":\"ed25519\"}}", "k", "1.0.0", &kRef), AV);
    CHECK(!s_info.sig[0]);
    /* M_ANIDADO. RELOJ: el campo se busca en su nivel (la referencia leía 9.9.9) */
    CHECK_EQ(pr("{\"k\":{\"meta\":{\"version\":\"9.9.9\"},\"version\":\"2.0.0\",\"url\":\"https://a/b\"}}", "k", "1.0.0",
                &kRef),
             AV);
    CHECK(!strcmp(s_info.ver, "2.0.0"));
    CHECK_EQ(pr("{\"meta\":{\"k\":{\"version\":\"9.9.9\",\"url\":\"https://x/y\"}},\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\"}}",
                "k", "1.0.0", &kRef),
             AV);
    CHECK(!strcmp(s_info.url, "https://a/b"));
    /* Clave repetida: gana la primera, como jFind */
    CHECK_EQ(pr("{\"k\":{\"version\":\"2.0.0\",\"version\":\"3.0.0\",\"url\":\"https://a/b\"}}", "k", "1.0.0", &kRef), AV);
    CHECK(!strcmp(s_info.ver, "2.0.0"));
    /* M_MD5_CEROS */
    CHECK_EQ(pr("{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"md5\":\"00000000000000000000000000000000\"}}",
                "k", "1.0.0", &kRef),
             AV);
    CHECK(!s_info.md5_ok);
    CHECK_EQ(pr("{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"md5\":\"e26fc99e596248dea0b33b497d1a99df0\"}}",
                "k", "1.0.0", &kRef),
             AV);
    CHECK(!s_info.md5_ok);

    /* M_LOG400 y M_URL258. RELOJ: novedades hasta 511 B sin partir un carácter; URL larga = error */
    char big[2400];
    char x[1300];
    memset(x, 'x', 600);
    x[600] = 0;
    snprintf(big, sizeof(big), "{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"changelog\":\"%.400s\"}}", x);
    CHECK_EQ(pr(big, "k", "1.0.0", &kRef), AV);
    CHECK_EQ(strlen(s_info.changelog), 400);
    snprintf(big, sizeof(big), "{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"changelog\":\"%s\"}}", x);
    CHECK_EQ(pr(big, "k", "1.0.0", &kRef), AV);
    CHECK_EQ(strlen(s_info.changelog), 511);
    char enye[700] = "";
    for (int i = 0; i < 300; i++) {
        strcat(enye, "ñ");
    }
    snprintf(big, sizeof(big), "{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"changelog\":\"%s\"}}", enye);
    CHECK_EQ(pr(big, "k", "1.0.0", &kRef), AV);
    CHECK_EQ(strlen(s_info.changelog), 510);
    CHECK(!strncmp(s_info.changelog + 508, "ñ", 2));
    memset(x, 'a', 250);
    x[250] = 0;
    snprintf(big, sizeof(big), "{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://%s\"}}", x);
    CHECK_EQ(pr(big, "k", "1.0.0", &kRef), OTA_ERR_BAD_URL);
    x[247] = 0; /* 255 justos */
    snprintf(big, sizeof(big), "{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://%s\"}}", x);
    CHECK_EQ(pr(big, "k", "1.0.0", &kRef), AV);
    CHECK_EQ(strlen(s_info.url), 255);

    /* Firma con el formato del reloj */
    memset(x, 'a', 128);
    x[128] = 0;
    snprintf(big, sizeof(big), "{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"sig\":\"%s\",\"sigalg\":\"%s\"}}", x,
             OTA_SIGALG);
    CHECK_EQ(pr(big, "k", "1.0.0", &kStrict), AV);
    CHECK_EQ(strlen(s_info.sig), 128);
    snprintf(big, sizeof(big), "{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"sig\":\"%s\"}}", x);
    CHECK_EQ(pr(big, "k", "1.0.0", &kStrict), AV); /* sin sigalg vale */
    x[127] = 'g';
    snprintf(big, sizeof(big), "{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"sig\":\"%s\"}}", x);
    CHECK_EQ(pr(big, "k", "1.0.0", &kStrict), OTA_ERR_BAD_SIG);
    CHECK_EQ(pr(big, "k", "1.0.0", &kRef), AV);
    CHECK(!s_info.sig[0]); /* una firma mal formada no se guarda */
    strcpy(x + 127, "aa");
    snprintf(big, sizeof(big), "{\"k\":{\"version\":\"2.0.0\",\"url\":\"https://a/b\",\"sig\":\"%s\"}}", x);
    CHECK_EQ(pr(big, "k", "1.0.0", &kStrict), OTA_ERR_BAD_SIG); /* 129 */
    /* Versión local mala */
    CHECK_EQ(pr(kOk, OTA_PRODUCT, "1.0", &kRef), OTA_ERR_BAD_VERSION);
}

/* ---------------- Firma, imagen y arranque ---------------- */

/* Vector de tools/ota/ota_sign.py: clave de usar y tirar (la privada se
 * borró y nunca estuvo en el repositorio), mensaje con SHA-256("abc"). */
static const char kPyPub[] =
    "0440d23b5dd3c7f5b5f999b0ada2c41c4014949aef3affe0c3afd23b4a41ea71c4"
    "92baaae3fd5110d1bc41c168de723d1fecdfafa4637712d2b5f2cac8d3209cef";
static const char kPySig[] =
    "1b898c128302d1837bd9007a13d244aa03ef3282b183af66a8f1107ce550e763"
    "04a41fdc92b91eb5df6e8b6a92cddf18edfca28cc25f05afa17e62366225f990";

static void test_sig(void)
{
    uint8_t sha[32];
    ota_sha256((const uint8_t *)"abc", 3, sha);
    char hex[65];
    ota_hex_encode(sha, 32, hex);
    CHECK(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    char msg[160];
    const size_t n = ota_sign_message(OTA_PRODUCT, "1.1.0", 70000, sha, msg, sizeof(msg));
    CHECK(!strcmp(msg, "flexos-ota-v1\nflex_os_watch_ultra\n1.1.0\n70000\n"
                       "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(n, strlen(msg));
    CHECK_EQ(ota_sign_message(OTA_PRODUCT, "1.1.0", 70000, sha, msg, 20), 0); /* no cabe */
    (void)ota_sign_message(OTA_PRODUCT, "1.1.0", 70000, sha, msg, sizeof(msg));

    uint8_t pub[65], sig[64];
    CHECK(ota_hex_decode(kPyPub, pub, 65));
    CHECK(ota_hex_decode(kPySig, sig, 64));
    CHECK(ota_p256_verify(pub, (const uint8_t *)msg, n, sig));
    /* Otra versión, otro tamaño, un bit cambiado: no */
    CHECK(!ota_p256_verify(pub, (const uint8_t *)msg, n - 1, sig));
    char m2[160];
    const size_t n2 = ota_sign_message(OTA_PRODUCT, "1.1.1", 70000, sha, m2, sizeof(m2));
    CHECK(!ota_p256_verify(pub, (const uint8_t *)m2, n2, sig));
    const size_t n3 = ota_sign_message(OTA_PRODUCT, "1.1.0", 70001, sha, m2, sizeof(m2));
    CHECK(!ota_p256_verify(pub, (const uint8_t *)m2, n3, sig));
    sig[10] ^= 1;
    CHECK(!ota_p256_verify(pub, (const uint8_t *)msg, n, sig));
    sig[10] ^= 1;
    pub[0] = 0x05;
    CHECK(!ota_p256_verify(pub, (const uint8_t *)msg, n, sig));
    pub[0] = 0x04;
    pub[64] ^= 1; /* punto fuera de la curva */
    CHECK(!ota_p256_verify(pub, (const uint8_t *)msg, n, sig));
    pub[64] ^= 1;
    uint8_t zero[64] = {0};
    CHECK(!ota_p256_verify(pub, (const uint8_t *)msg, n, zero));

    /* La clave de prueba generada al ejecutar firma lo mismo que el reloj verifica */
    uint8_t img[4096];
    ota_fake_image(img, sizeof(img), OTA_PRODUCT, "2.0.0", 9);
    char sh[OTA_SIG_HEX + 1];
    ota_fake_sign("2.0.0", img, sizeof(img), sh);
    uint8_t fpub[65], fsig[64];
    ota_fake_pubkey(fpub);
    CHECK(ota_hex_decode(sh, fsig, 64));
    ota_sha256(img, sizeof(img), sha);
    const size_t fn = ota_sign_message(OTA_PRODUCT, "2.0.0", sizeof(img), sha, msg, sizeof(msg));
    CHECK(ota_p256_verify(fpub, (const uint8_t *)msg, fn, fsig));

    /* Resúmenes por trozos = de una vez; MD5 de "abc" */
    ota_hash_t *h = ota_hash_new(true);
    CHECK(h != NULL);
    ota_hash_update(h, (const uint8_t *)"a", 1);
    ota_hash_update(h, (const uint8_t *)"bc", 2);
    uint8_t s2[32], md5[16];
    ota_hash_finish(h, s2, md5);
    ota_sha256((const uint8_t *)"abc", 3, sha);
    CHECK(!memcmp(sha, s2, 32));
    ota_hex_encode(md5, 16, hex);
    CHECK(!strcmp(hex, "900150983cd24fb0d6963f7d28e17f72"));
    ota_hash_free(ota_hash_new(false));
    ota_hash_free(NULL);
}

static void test_image(void)
{
    uint8_t img[256];
    ota_fake_image(img, sizeof(img), OTA_PRODUCT, "1.2.0", 9);
    CHECK_EQ(ota_image_check(img, sizeof(img), OTA_PRODUCT, "1.2.0"), OTA_ERR_NONE);
    CHECK_EQ(ota_image_check(img, OTA_HEAD_LEN - 1, OTA_PRODUCT, "1.2.0"), OTA_ERR_WRONG_IMAGE);
    CHECK_EQ(ota_image_check(img, sizeof(img), OTA_PRODUCT, "1.2.1"), OTA_ERR_WRONG_IMAGE);
    CHECK_EQ(ota_image_check(img, sizeof(img), OTA_PRODUCT, "1.2"), OTA_ERR_WRONG_IMAGE);
    CHECK_EQ(ota_image_check(img, sizeof(img), "flex_os_watch", "1.2.0"), OTA_ERR_WRONG_IMAGE);
    ota_fake_image(img, sizeof(img), "arduino-lib-builder", "1.2.0", 9);
    CHECK_EQ(ota_image_check(img, sizeof(img), OTA_PRODUCT, "1.2.0"), OTA_ERR_WRONG_IMAGE);
    ota_fake_image(img, sizeof(img), OTA_PRODUCT, "1.2.0", 18); /* la imagen del P4 */
    CHECK_EQ(ota_image_check(img, sizeof(img), OTA_PRODUCT, "1.2.0"), OTA_ERR_WRONG_IMAGE);
    ota_fake_image(img, sizeof(img), OTA_PRODUCT, "1.2.0", 9);
    img[0] = 0xE8;
    CHECK_EQ(ota_image_check(img, sizeof(img), OTA_PRODUCT, "1.2.0"), OTA_ERR_WRONG_IMAGE);
    img[0] = 0xE9;
    img[33] ^= 1;
    CHECK_EQ(ota_image_check(img, sizeof(img), OTA_PRODUCT, "1.2.0"), OTA_ERR_WRONG_IMAGE);

    /* E13, E14 y el aviso de reversión (RELOJ) */
    CHECK_EQ(ota_boot_notice("1.2.0", "1.2.0", false), OTA_BOOT_UPDATED);
    CHECK_EQ(ota_boot_notice("1.2.0", "1.1.0", false), OTA_BOOT_NONE);
    CHECK_EQ(ota_boot_notice("1.2.0", "1.1.0", true), OTA_BOOT_REVERTED);
    CHECK_EQ(ota_boot_notice("", "1.1.0", true), OTA_BOOT_NONE);
    CHECK_EQ(ota_boot_notice(NULL, "1.1.0", true), OTA_BOOT_NONE);

    /* Comprobación automática. RELOJ: 20 s después de tener Wi-Fi y cada 24 h
     * (la referencia: una vez a los 20 s del arranque y nunca más, E15). */
    ota_auto_t a = {0};
    CHECK(!ota_auto_due(&a, 0, false, OTA_IDLE));
    CHECK(!ota_auto_due(&a, 50000, false, OTA_IDLE));
    CHECK(!ota_auto_due(&a, 60000, true, OTA_IDLE));
    CHECK(!ota_auto_due(&a, 79999, true, OTA_IDLE));
    CHECK(!ota_auto_due(&a, 80000, true, OTA_CHECKING));
    CHECK(ota_auto_due(&a, 80000, true, OTA_IDLE));
    CHECK(!ota_auto_due(&a, 80001, true, OTA_UP_TO_DATE));
    CHECK(!ota_auto_due(&a, 90000, false, OTA_UP_TO_DATE));
    CHECK(!ota_auto_due(&a, 200000, true, OTA_UP_TO_DATE)); /* volver a conectar no repite */
    CHECK(!ota_auto_due(&a, 80000 + OTA_AUTO_EVERY_MS - 1, true, OTA_UP_TO_DATE));
    CHECK(!ota_auto_due(&a, 80000 + OTA_AUTO_EVERY_MS, true, OTA_AVAILABLE));
    CHECK(ota_auto_due(&a, 80000 + OTA_AUTO_EVERY_MS, true, OTA_ERROR));
    CHECK(!ota_auto_due(&a, 80000 + OTA_AUTO_EVERY_MS + 1, true, OTA_ERROR));
}

/* ---------------- Motor ---------------- */

#define MAN_URL "https://ota.example/manifest.json"
#define BIN_URL "https://ota.example/flex_os_watch_ultra.bin"
#define IMG_N   200000

typedef struct {
    ota_fake_t f;
    ota_io_t io;
    ota_t *o;
    uint8_t *img;
    char man[OTA_MANIFEST_MAX + 64];
    char sig[OTA_SIG_HEX + 1];
    uint8_t pub[65];
} rig_t;

/* Reloj 0.1.0-b1, servidor con la versión ver (la imagen dice img_ver). */
static void rig_init(rig_t *r, const char *ver, const char *img_ver, bool with_size, const char *md5)
{
    memset(r, 0, sizeof(*r));
    ota_fake_init(&r->f);
    r->io = ota_fake_io(&r->f);
    r->o = calloc(1, sizeof(ota_t));
    r->img = malloc(IMG_N);
    ota_fake_image(r->img, IMG_N, OTA_PRODUCT, img_ver, 9);
    ota_fake_pubkey(r->pub);
    ota_fake_sign(ver, r->img, IMG_N, r->sig);
    ota_fake_manifest(r->man, sizeof(r->man), ver, BIN_URL, with_size ? IMG_N : 0, md5, r->sig, "Novedad 1.\\nNovedad 2.");
    ota_fake_route(&r->f, MAN_URL, r->man, strlen(r->man));
    ota_fake_route(&r->f, BIN_URL, r->img, IMG_N);
    ota_init(r->o, OTA_PRODUCT, "0.1.0-b1", MAN_URL, r->pub);
}

static void rig_free(rig_t *r)
{
    ota_fake_free(&r->f);
    free(r->o);
    free(r->img);
}

static void rig_check(rig_t *r)
{
    if (ota_req_check(r->o, r->f.online)) {
        ota_run_check(r->o, &r->io);
    }
}

static int rig_install(rig_t *r)
{
    if (ota_req_install(r->o, r->f.online)) {
        ota_run_install(r->o, &r->io);
    }
    return r->o->state == OTA_ERROR ? r->o->err : (r->o->state == OTA_REBOOTING ? 100 : -r->o->state);
}

static void cancel_at(ota_fake_t *f, void *ud)
{
    rig_t *r = ud;
    if (f->pos >= 50000) {
        ota_req_cancel(r->o);
    }
}

static void batt_drop(ota_fake_t *f, void *ud)
{
    (void)ud;
    if (f->pos >= 100000) {
        f->batt_pct = 10;
    }
}

static void test_engine_check(void)
{
    rig_t r;
    rig_init(&r, "1.0.0", "1.0.0", true, NULL);
    char t[64];
    ota_status_text(r.o, t, sizeof(t));
    CHECK(!strcmp(t, "Buscar actualización"));

    /* E1: sin red, error al momento y sin orden a la tarea */
    r.f.online = false;
    const uint32_t g = r.o->gen;
    CHECK(!ota_req_check(r.o, false));
    CHECK_EQ(r.o->state, OTA_ERROR);
    CHECK_EQ(r.o->err, OTA_ERR_NO_WIFI);
    CHECK(r.o->gen != g);
    CHECK_EQ(r.f.opens, 0);
    r.f.online = true;

    /* Disponible */
    rig_check(&r);
    CHECK_EQ(r.o->state, OTA_AVAILABLE);
    CHECK(r.o->has_upd);
    CHECK(!strcmp(r.o->info.ver, "1.0.0"));
    CHECK(!strcmp(r.o->info.changelog, "Novedad 1.\nNovedad 2."));
    CHECK_EQ(r.o->total, IMG_N);
    CHECK(!strcmp(r.f.last_url, MAN_URL));
    CHECK_EQ(r.f.opens, r.f.closes);
    CHECK(r.f.locks > 0 && r.f.locks == r.f.unlocks);
    ota_status_text(r.o, t, sizeof(t));
    CHECK(!strcmp(t, "1.0.0 disponible"));

    /* E2: ocupado, no hace nada */
    r.o->state = OTA_DOWNLOADING;
    CHECK(!ota_req_check(r.o, true));
    CHECK(!ota_req_install(r.o, true));
    CHECK_EQ(r.o->state, OTA_DOWNLOADING);
    ota_status_text(r.o, t, sizeof(t));
    CHECK(!strcmp(t, "Descargando..."));
    r.o->state = OTA_AVAILABLE;

    /* E5: 404; el certificado; tiempo */
    ota_fake_route(&r.f, MAN_URL, r.man, strlen(r.man))->open_err = OTA_ERR_HTTP_STATUS;
    rig_check(&r);
    CHECK_EQ(r.o->state, OTA_ERROR);
    CHECK_EQ(r.o->err, OTA_ERR_HTTP_STATUS);
    CHECK(r.o->has_upd); /* lo de antes sigue valiendo */
    ota_status_text(r.o, t, sizeof(t));
    CHECK(!strcmp(t, "Archivo no encontrado"));
    ota_fake_route(&r.f, MAN_URL, r.man, strlen(r.man))->open_err = OTA_ERR_TLS;
    rig_check(&r);
    CHECK_EQ(r.o->err, OTA_ERR_TLS);
    ota_fake_route_t *rt = ota_fake_route(&r.f, MAN_URL, r.man, strlen(r.man));
    rt->open_err = 0;
    rt->stall_at = 10;
    rig_check(&r);
    CHECK_EQ(r.o->err, OTA_ERR_TIMEOUT);
    CHECK_EQ(r.f.opens, r.f.closes);
    rt->stall_at = OTA_FAKE_NOCUT;
    /* Cortado antes de su Content-Length */
    rt->cut_at = 20;
    rig_check(&r);
    CHECK_EQ(r.o->err, OTA_ERR_TRUNCATED);
    rt->cut_at = OTA_FAKE_NOCUT;
    /* Sin Content-Length se lee hasta el cierre */
    rt->clen = -1;
    rt->chunk = 7;
    rig_check(&r);
    CHECK_EQ(r.o->state, OTA_AVAILABLE);
    /* RELOJ: un manifiesto de más de 4095 B no se recorta: se rechaza */
    static char huge[5000];
    memset(huge, ' ', sizeof(huge));
    memcpy(huge + sizeof(huge) - strlen(r.man), r.man, strlen(r.man));
    ota_fake_route(&r.f, MAN_URL, huge, sizeof(huge));
    rig_check(&r);
    CHECK_EQ(r.o->err, OTA_ERR_JSON);
    ota_fake_route(&r.f, MAN_URL, huge, 4095); /* 4095 justos: cabe (y no es JSON) */
    rig_check(&r);
    CHECK_EQ(r.o->err, OTA_ERR_NO_BLOCK);
    /* Vacío */
    ota_fake_route(&r.f, MAN_URL, r.man, 0);
    rig_check(&r);
    CHECK_EQ(r.o->err, OTA_ERR_JSON);
    /* Memoria */
    ota_fake_route(&r.f, MAN_URL, r.man, strlen(r.man));
    r.f.mem_ok = false;
    rig_check(&r);
    CHECK_EQ(r.o->err, OTA_ERR_LOWMEM);
    r.f.mem_ok = true;

    /* Al día: se olvida lo que había */
    ota_fake_manifest(r.man, sizeof(r.man), "0.1.0-b1", BIN_URL, 0, NULL, NULL, NULL);
    ota_fake_route(&r.f, MAN_URL, r.man, strlen(r.man));
    rig_check(&r);
    CHECK_EQ(r.o->state, OTA_UP_TO_DATE);
    CHECK(!r.o->has_upd);
    CHECK(!r.o->info.ver[0]);
    ota_status_text(r.o, t, sizeof(t));
    CHECK(!strcmp(t, "El sistema está al día"));
    /* E3: sin actualización, instalar no hace nada */
    CHECK(!ota_req_install(r.o, true));
    CHECK_EQ(r.o->state, OTA_UP_TO_DATE);
    /* Sin firma: el reloj no la ofrece */
    ota_fake_manifest(r.man, sizeof(r.man), "9.0.0", BIN_URL, 0, NULL, NULL, NULL);
    ota_fake_route(&r.f, MAN_URL, r.man, strlen(r.man));
    rig_check(&r);
    CHECK_EQ(r.o->err, OTA_ERR_NO_SIG);
    rig_free(&r);
}

static void test_engine_install(void)
{
    /* Camino feliz */
    rig_t r;
    rig_init(&r, "1.0.0", "1.0.0", true, NULL);
    r.f.r[1].chunk = 1000;
    rig_check(&r);
    CHECK_EQ(r.o->state, OTA_AVAILABLE);
    CHECK_EQ(rig_install(&r), 100);
    CHECK(r.f.fl_finished && !r.f.fl_aborted);
    CHECK_EQ(r.f.fl_written, IMG_N);
    CHECK(!memcmp(r.f.flash, r.img, IMG_N));
    CHECK(!strcmp(r.f.pend, "1.0.0"));
    CHECK_EQ(r.o->pct, 100);
    CHECK_EQ(r.o->done, IMG_N);
    /* 1000 B cada 10 ms = 100 000 B/s */
    CHECK_EQ(r.o->bps, 100000);
    CHECK_EQ(r.f.sleeps_ms, 2000); /* 400 ms + 1,6 s, como la referencia */
    CHECK_EQ(r.f.opens, r.f.closes);
    rig_free(&r);

    /* Con MD5 bueno y malo (E12) */
    rig_init(&r, "1.0.0", "1.0.0", true, NULL);
    {
        uint8_t sha[32], md5[16];
        char hex[33];
        ota_hash_t *h = ota_hash_new(true);
        ota_hash_update(h, r.img, IMG_N);
        ota_hash_finish(h, sha, md5);
        ota_hex_encode(md5, 16, hex);
        ota_fake_manifest(r.man, sizeof(r.man), "1.0.0", BIN_URL, IMG_N, hex, r.sig, NULL);
        ota_fake_route(&r.f, MAN_URL, r.man, strlen(r.man));
        rig_check(&r);
        CHECK(r.o->info.md5_ok);
        CHECK_EQ(rig_install(&r), 100);
        r.o->state = OTA_IDLE; /* en el reloj, aquí reinicia */
        hex[0] = hex[0] == 'a' ? 'b' : 'a';
        ota_fake_manifest(r.man, sizeof(r.man), "1.0.0", BIN_URL, IMG_N, hex, r.sig, NULL);
        ota_fake_route(&r.f, MAN_URL, r.man, strlen(r.man));
        rig_check(&r);
        CHECK_EQ(rig_install(&r), OTA_ERR_CORRUPT);
        CHECK(r.f.fl_aborted && !r.f.fl_finished);
    }
    rig_free(&r);

    /* Errores de la instalación: cada uno deja la ranura activa sin tocar */
    static const struct {
        int what;
        int want;
    } cases[] = {
        {0, OTA_ERR_TRUNCATED},    /* E9 */
        {1, OTA_ERR_TIMEOUT},      /* E10 */
        {2, OTA_ERR_CANCELLED},    /* E11 */
        {3, OTA_ERR_BAD_SIZE},     /* E6: sin Content-Length */
        {4, OTA_ERR_BAD_SIZE},     /* tamaño distinto del manifiesto */
        {5, OTA_ERR_BEGIN},
        {6, OTA_ERR_WRITE},
        {7, OTA_ERR_CORRUPT},      /* esp_ota_end rechaza la imagen */
        {8, OTA_ERR_NO_PARTITION},
        {9, OTA_ERR_BATTERY},      /* 20 % sin cargador */
        {10, OTA_ERR_BATTERY},     /* baja al 10 % descargando */
        {11, OTA_ERR_NO_WIFI},
        {12, OTA_ERR_LOWMEM},
        {13, OTA_ERR_HTTP_STATUS},
        {14, OTA_ERR_NO_KEY},
        {15, OTA_ERR_CANCELLED},   /* "Cancelar" antes de que la tarea empiece (A9) */
        {16, OTA_ERR_BAD_SIZE},    /* E7: más grande que la ranura */
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        rig_init(&r, "1.0.0", "1.0.0", cases[i].what != 4, NULL);
        rig_check(&r);
        ota_fake_route_t *b = &r.f.r[1];
        bool pre_cancel = false;
        switch (cases[i].what) {
        case 0: b->cut_at = 150000; break;
        case 1: b->stall_at = 150000; break;
        case 2: r.f.on_read = cancel_at; r.f.ud = &r; break;
        case 3: b->clen = -1; break;
        case 4: r.o->info.size = IMG_N + 1; break;
        case 5: r.f.fail_begin = OTA_ERR_BEGIN; break;
        case 6: r.f.fail_write_at = 120000; break;
        case 7: r.f.fail_finish = OTA_ERR_CORRUPT; break;
        case 8: r.f.slot = 0; break;
        case 9: r.f.batt_pct = 20; break;
        case 10: r.f.on_read = batt_drop; break;
        case 11: r.f.online = false; break;
        case 12: r.f.mem_ok = false; break;
        case 13: b->open_err = OTA_ERR_HTTP_STATUS; break;
        case 14: r.o->have_key = false; break;
        case 15: pre_cancel = true; break;
        case 16: r.f.slot = IMG_N - 1; break;
        }
        int got;
        if (pre_cancel) {
            CHECK(ota_req_install(r.o, true));
            ota_req_cancel(r.o);
            ota_run_install(r.o, &r.io);
            got = r.o->state == OTA_ERROR ? r.o->err : -1;
            CHECK_EQ(r.f.opens, 1); /* solo la del manifiesto */
        } else {
            got = rig_install(&r);
        }
        if (got != cases[i].want) {
            fprintf(stderr, "  instalación %zu: %d, esperado %d\n", i, got, cases[i].want);
        }
        CHECK_EQ(got, cases[i].want);
        CHECK(!r.f.fl_finished);
        CHECK(!r.f.fl_begun); /* nada a medias: o no empezó o se abortó */
        CHECK(!r.f.pend[0]);
        CHECK_EQ(r.f.opens, r.f.closes);
        CHECK(r.o->has_upd); /* se puede reintentar */
        rig_free(&r);
    }

    /* E7/E8: los bordes del tamaño */
    rig_init(&r, "1.0.0", "1.0.0", false, NULL);
    rig_check(&r);
    r.f.r[1].clen = OTA_MIN_BIN - 1;
    CHECK_EQ(rig_install(&r), OTA_ERR_BAD_SIZE);
    CHECK_EQ(r.f.begins, 0);
    r.f.r[1].clen = OTA_MIN_BIN;
    r.f.fail_begin = OTA_ERR_BEGIN; /* pasa el filtro y llega a begin */
    CHECK_EQ(rig_install(&r), OTA_ERR_BEGIN);
    CHECK_EQ(r.f.begins, 1);
    r.f.r[1].clen = 0x600001;
    CHECK_EQ(rig_install(&r), OTA_ERR_BAD_SIZE);
    r.f.r[1].clen = 0x600000;
    CHECK_EQ(rig_install(&r), OTA_ERR_BEGIN);
    CHECK_EQ(r.f.begins, 2);
    rig_free(&r);

    /* 20 % pero cargando: adelante */
    rig_init(&r, "1.0.0", "1.0.0", true, NULL);
    rig_check(&r);
    r.f.batt_pct = 20;
    r.f.charging = true;
    CHECK_EQ(rig_install(&r), 100);
    rig_free(&r);
    /* Sin dato de batería: no bloquea */
    rig_init(&r, "1.0.0", "1.0.0", true, NULL);
    rig_check(&r);
    r.f.batt_pct = -1;
    CHECK_EQ(rig_install(&r), 100);
    rig_free(&r);

    /* Firma de otra versión: BAD_SIG tras descargar, sin tocar el arranque */
    rig_init(&r, "1.0.0", "1.0.0", true, NULL);
    ota_fake_sign("1.0.1", r.img, IMG_N, r.sig);
    ota_fake_manifest(r.man, sizeof(r.man), "1.0.0", BIN_URL, IMG_N, NULL, r.sig, NULL);
    ota_fake_route(&r.f, MAN_URL, r.man, strlen(r.man));
    rig_check(&r);
    CHECK_EQ(rig_install(&r), OTA_ERR_BAD_SIG);
    CHECK_EQ(r.f.fl_written, IMG_N);
    CHECK(r.f.fl_aborted && !r.f.fl_finished);
    rig_free(&r);
    /* Un byte cambiado en el servidor: BAD_SIG */
    rig_init(&r, "1.0.0", "1.0.0", true, NULL);
    rig_check(&r);
    r.img[IMG_N / 2] ^= 0x40;
    CHECK_EQ(rig_install(&r), OTA_ERR_BAD_SIG);
    rig_free(&r);
    /* Otra clave en el reloj */
    rig_init(&r, "1.0.0", "1.0.0", true, NULL);
    rig_check(&r);
    CHECK(ota_hex_decode(kPyPub, r.o->pubkey, 65));
    CHECK_EQ(rig_install(&r), OTA_ERR_BAD_SIG);
    rig_free(&r);

    /* La imagen dice otra versión: se para al leer la cabecera (WRONG_IMAGE) */
    rig_init(&r, "1.0.0", "0.9.0", true, NULL);
    rig_check(&r);
    CHECK_EQ(rig_install(&r), OTA_ERR_WRONG_IMAGE);
    CHECK(r.f.fl_written < OTA_CHUNK + 1);
    CHECK(r.f.fl_aborted);
    rig_free(&r);
    /* La imagen es de otro chip, entregada de 7 en 7 bytes */
    rig_init(&r, "1.0.0", "1.0.0", true, NULL);
    r.img[12] = 18;
    ota_fake_sign("1.0.0", r.img, IMG_N, r.sig);
    ota_fake_manifest(r.man, sizeof(r.man), "1.0.0", BIN_URL, IMG_N, NULL, r.sig, NULL);
    ota_fake_route(&r.f, MAN_URL, r.man, strlen(r.man));
    r.f.r[1].chunk = 7;
    rig_check(&r);
    CHECK_EQ(rig_install(&r), OTA_ERR_WRONG_IMAGE);
    CHECK(r.f.fl_written < OTA_HEAD_LEN);
    rig_free(&r);

    /* Reintento tras un fallo: la segunda vez sale */
    rig_init(&r, "1.0.0", "1.0.0", true, NULL);
    rig_check(&r);
    r.f.r[1].cut_at = 1000;
    CHECK_EQ(rig_install(&r), OTA_ERR_TRUNCATED);
    r.f.r[1].cut_at = OTA_FAKE_NOCUT;
    CHECK_EQ(rig_install(&r), 100);
    rig_free(&r);

    /* Sin clave de confianza (el firmware de este repositorio): NO_KEY sin descargar */
    rig_init(&r, "1.0.0", "1.0.0", true, NULL);
    ota_init(r.o, OTA_PRODUCT, "0.1.0-b1", MAN_URL, NULL);
    rig_check(&r);
    CHECK_EQ(r.o->state, OTA_AVAILABLE);
    const int opens = r.f.opens;
    CHECK_EQ(rig_install(&r), OTA_ERR_NO_KEY);
    CHECK_EQ(r.f.opens, opens);
    rig_free(&r);
}

void test_ota_all(void)
{
    test_versions();
    test_utils();
    test_manifest();
    test_sig();
    test_image();
    test_engine_check();
    test_engine_install();
}
