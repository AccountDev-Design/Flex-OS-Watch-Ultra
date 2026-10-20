/*
 * Pruebas del núcleo del navegador (F6): las de tests/host/test_browser.cpp
 * de Flex OS Ultra (build 6) pasadas a C sin tocar ni un vector: omnibox y
 * su política anti-SSRF, utilidades de URL, códec FBP/1 con todos los
 * truncamientos, SHA-1/Base64/WebSocket, ajustes y fuente. Después, los
 * vectores dorados del perfil S3 (bytes exactos del códec de la referencia)
 * y las pruebas propias del reloj.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "br_logic.h"
#include "test_util.h"

#define BCHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fail++; \
    fprintf(stderr, "FALLO %s:%d  ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static const char *kindName(int k)
{
    switch (k) {
    case BRI_EMPTY: return "VACIO";
    case BRI_INTERNAL: return "INTERNA";
    case BRI_URL: return "URL";
    case BRI_SEARCH: return "BUSQUEDA";
    case BRI_BLOCKED: return "BLOQUEADO";
    default: return "?";
    }
}

/* expectKind(st, in, kind[, url]) de la referencia (argumento por defecto) */
static void ek(const BrSettings *st, const char *in, int kind, const char *url, ...)
{
    BrResolved r;
    const int k = flexBrResolveInput(in, st, &r);
    g_checks++;
    if (k != kind) {
        g_fail++;
        fprintf(stderr, "FALLO  \"%s\" -> %s (esperado %s)%s%s\n", in, kindName(k), kindName(kind),
                r.block ? "  motivo: " : "", r.block ? flexBrBlockReason(r.block) : "");
        return;
    }
    if (url) {
        g_checks++;
        if (strcmp(r.url, url) != 0) {
            g_fail++;
            fprintf(stderr, "FALLO  \"%s\" -> url \"%s\"\n         esperado \"%s\"\n", in, r.url, url);
        }
    }
}
#define expectKind(st, in, ...) ek(&(st), in, __VA_ARGS__, NULL)

static void eb(const BrSettings *st, const char *in, int reason)
{
    BrResolved r;
    const int k = flexBrResolveInput(in, st, &r);
    g_checks++;
    if (k != BRI_BLOCKED || r.block != reason) {
        g_fail++;
        fprintf(stderr, "FALLO  \"%s\" -> %s/%d (esperado BLOQUEADO/%d)\n", in, kindName(k), r.block, reason);
    }
}
#define expectBlock(st, in, reason) eb(&(st), in, reason)

/* ===== Pruebas de la referencia (test_browser.cpp:64-637), en C ===== */
static void testOmnibox(){
  /* [omnibox] resolucion de entradas */
  BrSettings st; flexBrSettingsDefaults(&st);

  // --- 1) vacio ---
  expectKind(st, "", BRI_EMPTY);
  expectKind(st, "    ", BRI_EMPTY);
  expectKind(st, "\t\r\n", BRI_EMPTY);
  { BrResolved r; BCHECK(flexBrResolveInput(NULL, &st, &r) == BRI_EMPTY, "NULL no dio VACIO"); }

  // --- 2) paginas internas ---
  expectKind(st, "flex://newtab", BRI_INTERNAL, "flex://newtab");
  expectKind(st, "flex://history", BRI_INTERNAL, "flex://history");
  expectKind(st, "flex://bookmarks", BRI_INTERNAL, "flex://bookmarks");
  expectKind(st, "flex://settings", BRI_INTERNAL, "flex://settings");
  expectKind(st, "flex://offline", BRI_INTERNAL, "flex://offline");
  expectKind(st, "flex://about", BRI_INTERNAL, "flex://about");
  expectKind(st, "flex://downloads", BRI_INTERNAL, "flex://downloads");
  expectKind(st, "FLEX://HISTORY", BRI_INTERNAL, "flex://history");
  expectKind(st, "flex://history/", BRI_INTERNAL, "flex://history");
  expectKind(st, "  flex://about  ", BRI_INTERNAL, "flex://about");
  expectKind(st, "flex://", BRI_INTERNAL, "flex://newtab");
  // Una pagina interna que no existe NO se convierte en busqueda: se
  // bloquea, para que "flex://" no sea una via de escape hacia nada.
  expectBlock(st, "flex://noexiste", BRB_SCHEME);
  expectBlock(st, "flex://history/../../etc", BRB_SCHEME);
  expectBlock(st, "flex://settings?x=1", BRB_SCHEME);
  expectBlock(st, "flex:algo", BRB_SCHEME);

  // --- 3) http/https validos ---
  expectKind(st, "https://example.com", BRI_URL, "https://example.com");
  expectKind(st, "http://example.com/a/b?c=d#e", BRI_URL, "http://example.com/a/b?c=d#e");
  expectKind(st, "https://sub.dominio.co.uk:8443/ruta", BRI_URL);
  expectKind(st, "HTTPS://Example.COM/X", BRI_URL, "HTTPS://Example.COM/X");

  // --- 4) otros esquemas: todos bloqueados ---
  const char* badSchemes[] = {
    "file:///etc/passwd", "file://server/share", "javascript:alert(1)",
    "data:text/html,<h1>x", "ftp://ftp.example.com/f", "about:blank",
    "chrome://settings", "view-source:https://example.com", "blob:https://x/y",
    "ws://example.com/s", "wss://example.com/s", "mailto:a@b.com",
    "tel:+34600000000", "intent://x", "content://media/1", "jar:file:///a!/b", 0
  };
  for(int i = 0; badSchemes[i]; i++) expectBlock(st, badSchemes[i], BRB_SCHEME);

  // --- 5) dominio sin esquema -> https ---
  expectKind(st, "example.com", BRI_URL, "https://example.com");
  expectKind(st, "example.com/ruta?q=1", BRI_URL, "https://example.com/ruta?q=1");
  expectKind(st, "www.rtve.es", BRI_URL, "https://www.rtve.es");
  expectKind(st, "sub.dominio.org:8080/x", BRI_URL, "https://sub.dominio.org:8080/x");
  expectKind(st, "  wikipedia.org  ", BRI_URL, "https://wikipedia.org");

  // --- 6) IP y hosts locales: bloqueados por defecto ---
  expectBlock(st, "127.0.0.1", BRB_PRIVATE_IP);
  expectBlock(st, "http://127.0.0.1:8080/", BRB_PRIVATE_IP);
  expectBlock(st, "https://10.0.0.5/admin", BRB_PRIVATE_IP);
  expectBlock(st, "192.168.1.1", BRB_PRIVATE_IP);
  expectBlock(st, "172.16.0.1", BRB_PRIVATE_IP);
  expectBlock(st, "169.254.169.254", BRB_PRIVATE_IP);          // metadatos de nube
  expectBlock(st, "http://169.254.169.254/latest/meta-data/", BRB_PRIVATE_IP);
  expectBlock(st, "http://[::1]:80/", BRB_PRIVATE_IP);
  expectBlock(st, "http://[fd00::1]/", BRB_PRIVATE_IP);
  expectBlock(st, "0.0.0.0", BRB_PRIVATE_IP);
  expectBlock(st, "2130706433", BRB_PRIVATE_IP);               // 127.0.0.1 en decimal
  expectBlock(st, "0x7f000001", BRB_PRIVATE_IP);               // 127.0.0.1 en hexadecimal
  expectBlock(st, "010.0.0.1", BRB_PRIVATE_IP);                // octal
  expectBlock(st, "http://93.184.216.34/", BRB_PRIVATE_IP);    // IP publica: tambien fuera por defecto
  expectBlock(st, "localhost", BRB_LOCALHOST);
  expectBlock(st, "http://localhost:3000/", BRB_LOCALHOST);
  expectBlock(st, "http://impresora.local/", BRB_LOCALHOST);
  expectBlock(st, "http://nas.internal/", BRB_LOCALHOST);
  expectBlock(st, "http://router.lan/", BRB_LOCALHOST);
  expectBlock(st, "http://intranet/", BRB_LOCALHOST);          // una sola etiqueta
  expectBlock(st, "http://servidor/recurso", BRB_LOCALHOST);

  // --- usuario:clave incrustados ---
  expectBlock(st, "https://admin:secreto@banco.com/", BRB_USERINFO);
  expectBlock(st, "http://a@b.example.com/", BRB_USERINFO);

  // --- caracteres de control e inyeccion ---
  expectBlock(st, "https://example.com/\nHost: otro", BRB_BADCHARS);
  expectBlock(st, "https://example.com/\r\nX", BRB_BADCHARS);
  expectBlock(st, "https://example.com/<script>", BRB_BADCHARS);
  expectBlock(st, "https://example.com/a\\b", BRB_BADCHARS);

  // --- longitud ---
  {
    static char tooLong[20 + FLEXBR_INPUT_MAX + 41];
    strcpy(tooLong, "https://example.com/");
    memset(tooLong + 20, 'a', FLEXBR_INPUT_MAX + 40);
    tooLong[20 + FLEXBR_INPUT_MAX + 40] = 0;
    expectBlock(st, tooLong, BRB_TOOLONG);
    char longButOk[20 + 151];
    strcpy(longButOk, "https://example.com/");
    memset(longButOk + 20, 'b', 150);
    longButOk[170] = 0;
    expectKind(st, longButOk, BRI_URL);
  }

  // --- 7) busqueda ---
  expectKind(st, "gatos", BRI_SEARCH, "https://duckduckgo.com/?q=gatos");
  expectKind(st, "como hacer pan", BRI_SEARCH, "https://duckduckgo.com/?q=como%20hacer%20pan");
  expectKind(st, "que es el ma\xC3\xB1" "ana", BRI_SEARCH,
             "https://duckduckgo.com/?q=que%20es%20el%20ma%C3%B1ana");
  expectKind(st, "c++ std::vector", BRI_SEARCH,
             "https://duckduckgo.com/?q=c%2B%2B%20std%3A%3Avector");
  expectKind(st, "100% & m\xC3\xA1s", BRI_SEARCH,
             "https://duckduckgo.com/?q=100%25%20%26%20m%C3%A1s");
  // Una sola palabra con punto pero sin TLD valido -> busqueda, no URL.
  expectKind(st, "version 1.2", BRI_SEARCH);
  expectKind(st, "archivo.7z", BRI_SEARCH);        // "7z" no es un TLD alfabetico
  expectKind(st, "3.14159", BRI_SEARCH);
  expectKind(st, "a@b.com", BRI_SEARCH);           // parece un correo: al buscador
  // Dominio con acentos: sin IDNA no se puede construir la URL correcta,
  // asi que se busca en vez de inventar la conversion.
  expectKind(st, "espa\xC3\xB1" "a.es", BRI_SEARCH);

  // --- permiso de desarrollo ---
  {
    BrSettings dev = st; dev.allowLocal = true;
    expectKind(dev, "http://192.168.1.50:8080/panel", BRI_URL, "http://192.168.1.50:8080/panel");
    expectKind(dev, "localhost:3000", BRI_URL, "https://localhost:3000");
    expectKind(dev, "127.0.0.1", BRI_URL, "https://127.0.0.1");
    // Ni con el permiso se acepta otro esquema ni usuario incrustado.
    expectBlock(dev, "file:///etc/passwd", BRB_SCHEME);
    expectBlock(dev, "https://a:b@10.0.0.1/", BRB_USERINFO);
  }

  // --- otros buscadores ---
  {
    BrSettings g = st; g.searchEngine = 1;
    expectKind(g, "hola mundo", BRI_SEARCH, "https://www.google.com/search?q=hola%20mundo");
    BrSettings b = st; b.searchEngine = 2;
    expectKind(b, "hola", BRI_SEARCH, "https://www.bing.com/search?q=hola");
    BrSettings c = st; c.searchEngine = 3;
    strcpy(c.searchCustom, "https://buscador.example.org/s?query=%s&lang=es");
    expectKind(c, "una cosa", BRI_SEARCH,
               "https://buscador.example.org/s?query=una%20cosa&lang=es");
    // Plantilla personalizada que apunta a un destino interno: se
    // bloquea igual que cualquier otra URL.
    BrSettings bad = st; bad.searchEngine = 3;
    strcpy(bad.searchCustom, "http://192.168.0.1/s?q=%s");
    expectBlock(bad, "algo", BRB_PRIVATE_IP);
    // Plantilla sin %s: no se puede usar.
    BrSettings nop = st; nop.searchEngine = 3;
    strcpy(nop.searchCustom, "https://buscador.example.org/s");
    expectBlock(nop, "algo", BRB_BADCHARS);
    // Plantilla con especificadores de printf: NO se interpretan.
    BrSettings pf = st; pf.searchEngine = 3;
    strcpy(pf.searchCustom, "https://buscador.example.org/s?q=%s&n=%n%d");
    BrResolved r;
    int k = flexBrResolveInput("x", &pf, &r);
    BCHECK(k == BRI_SEARCH && strstr(r.url, "%n%d") != NULL,
          "la plantilla personalizada se paso por printf");
  }

  // --- la salida SIEMPRE cabe ---
  {
    BrResolved r;
    char q[FLEXBR_INPUT_MAX - 1];
    memset(q, 'z', FLEXBR_INPUT_MAX - 2);
    q[FLEXBR_INPUT_MAX - 2] = 0;
    flexBrResolveInput(q, &st, &r);
    BCHECK(strlen(r.url) < FLEXBR_URL_MAX, "la URL resuelta desborda el buffer");
  }
}

// -------------------------------------------------------------
//  2) Utilidades de URL
// -------------------------------------------------------------
static void testUrlHelpers(){
  /* [url] utilidades */
  char buf[FLEXBR_URL_MAX];

  BCHECK(flexBrPercentEncode("abc-_.~", buf, sizeof(buf)) == 7 && !strcmp(buf, "abc-_.~"),
        "los caracteres no reservados no deben codificarse: \"%s\"", buf);
  BCHECK(flexBrPercentEncode(" ", buf, sizeof(buf)) == 3 && !strcmp(buf, "%20"),
        "el espacio debe ser %%20 y no '+': \"%s\"", buf);
  BCHECK(flexBrPercentEncode("/?&=#", buf, sizeof(buf)) > 0 && !strcmp(buf, "%2F%3F%26%3D%23"),
        "reservados mal codificados: \"%s\"", buf);
  { char tiny[4];
    BCHECK(flexBrPercentEncode("abcdef", tiny, sizeof(tiny)) == -1 && tiny[0] == 0,
          "el desbordamiento debe fallar y dejar la salida vacia"); }

  BCHECK(flexBrUrlHost("https://www.example.com:8443/x", buf, sizeof(buf)) &&
        !strcmp(buf, "www.example.com"), "host mal extraido: \"%s\"", buf);
  BCHECK(!flexBrUrlHost("flex://about", buf, sizeof(buf)), "flex:// no tiene host");

  flexBrUrlLabel("https://www.example.com/a/b", buf, sizeof(buf));
  BCHECK(!strcmp(buf, "example.com"), "etiqueta \"%s\", esperado \"example.com\"", buf);
  flexBrUrlLabel("flex://history", buf, sizeof(buf));
  BCHECK(!strcmp(buf, "Historial"), "etiqueta de pagina interna \"%s\"", buf);

  BCHECK(flexBrInternalPage("flex://about") == BRP_ABOUT, "flex://about mal reconocida");
  BCHECK(flexBrInternalPage("https://example.com") == BRP_NONE, "una URL no es pagina interna");
  BCHECK(flexBrInternalPage(NULL) == BRP_NONE, "NULL no es pagina interna");
  for(int p = 0; p < BRP_N; p++){
    char u[64]; snprintf(u, sizeof(u), "flex://%s", flexBrPageName(p));
    BCHECK(flexBrInternalPage(u) == p, "ida y vuelta de la pagina %d", p);
    BCHECK(flexBrPageTitle(p)[0] != 0, "la pagina %d no tiene titulo", p);
  }
}

// -------------------------------------------------------------
//  3) Protocolo FBP/1
// -------------------------------------------------------------
static void testProtocol(){
  /* [fbp] codec del protocolo */
  uint8_t buf[4096];
  FbpHeader h;

  // --- cabecera ---
  fbpWriteHeader(buf, FBP_C_PING, 0x03, 2, 0x1234, 7);
  BCHECK(fbpReadHeader(buf, FBP_HDR_SIZE, &h), "cabecera valida rechazada");
  BCHECK(h.type == FBP_C_PING && h.flags == 3 && h.channel == 2 && h.seq == 0x1234 && h.len == 7,
        "cabecera mal leida");
  { uint8_t b2[FBP_HDR_SIZE]; memcpy(b2, buf, sizeof(b2)); b2[0] = 'X';
    BCHECK(!fbpReadHeader(b2, sizeof(b2), &h), "magia incorrecta aceptada"); }
  { uint8_t b2[FBP_HDR_SIZE]; memcpy(b2, buf, sizeof(b2)); b2[2] = 99;
    BCHECK(!fbpReadHeader(b2, sizeof(b2), &h), "version incorrecta aceptada"); }
  { uint8_t b2[FBP_HDR_SIZE]; memcpy(b2, buf, sizeof(b2));
    b2[8] = 0xFF; b2[9] = 0xFF; b2[10] = 0xFF; b2[11] = 0xFF;
    BCHECK(!fbpReadHeader(b2, sizeof(b2), &h), "longitud absurda aceptada"); }
  BCHECK(!fbpReadHeader(buf, FBP_HDR_SIZE - 1, &h), "cabecera corta aceptada");

  // --- constructores del cliente ---
  int n = fbpBuildHello(buf, sizeof(buf), 1, "token-secreto", "flexos-abc", 480, 800, 60, BRQ_BALANCED, 0x0F);
  BCHECK(n > FBP_HDR_SIZE, "HELLO no se construyo");
  BCHECK(fbpReadHeader(buf, (size_t)n, &h) && h.type == FBP_C_HELLO &&
        h.len == (uint32_t)(n - FBP_HDR_SIZE), "cabecera de HELLO incoherente");

  n = fbpBuildNavigate(buf, sizeof(buf), 2, 1, "https://example.com/x");
  BCHECK(n == FBP_HDR_SIZE + 2 + 21, "tamano de NAVIGATE: %d", n);
  BCHECK(fbpReadHeader(buf, (size_t)n, &h) && h.type == FBP_C_NAVIGATE && h.channel == 1,
        "NAVIGATE mal formado");

  n = fbpBuildPointer(buf, sizeof(buf), 3, 1, FBP_PTR_TAP, 240, 400, 0);
  BCHECK(n == FBP_HDR_SIZE + 6, "tamano de POINTER");
  BCHECK(buf[FBP_HDR_SIZE] == FBP_PTR_TAP &&
        (buf[FBP_HDR_SIZE + 1] | (buf[FBP_HDR_SIZE + 2] << 8)) == 240 &&
        (buf[FBP_HDR_SIZE + 3] | (buf[FBP_HDR_SIZE + 4] << 8)) == 400, "POINTER mal codificado");

  n = fbpBuildScroll(buf, sizeof(buf), 4, 1, -120, 340);
  BCHECK(n == FBP_HDR_SIZE + 4, "tamano de SCROLL");
  BCHECK((int16_t)(buf[FBP_HDR_SIZE] | (buf[FBP_HDR_SIZE + 1] << 8)) == -120,
        "SCROLL no conserva el signo");

  n = fbpBuildKey(buf, sizeof(buf), 5, 1, FBP_KEY_TEXT, 0, 0, "\xC3\xB1");
  BCHECK(n == FBP_HDR_SIZE + 3 + 2 + 2, "tamano de KEY con UTF-8: %d", n);

  n = fbpBuildViewport(buf, sizeof(buf), 6, 1, 480, 736, 70, BRQ_LOWLAT, 100);
  BCHECK(n == FBP_HDR_SIZE + 7, "tamano de VIEWPORT");

  n = fbpBuildAck(buf, sizeof(buf), 7, 1000, 2);
  BCHECK(n == FBP_HDR_SIZE + 3, "tamano de ACK");

  n = fbpBuildMedia(buf, sizeof(buf), 8, 1, FBP_MED_SEEK, 45000);
  BCHECK(n == FBP_HDR_SIZE + 5, "tamano de MEDIA");

  // Buffer insuficiente: se rechaza, nunca se escribe de mas.
  { uint8_t tiny[8];
    BCHECK(fbpBuildNavigate(tiny, sizeof(tiny), 1, 0, "https://example.com") == -1,
          "NAVIGATE escribio en un buffer pequeno");
    BCHECK(fbpBuildHello(tiny, sizeof(tiny), 1, "t", "d", 1, 1, 1, 1, 0) == -1,
          "HELLO escribio en un buffer pequeno");
    BCHECK(fbpBuildPointer(tiny, sizeof(tiny), 1, 0, 0, 0, 0, 0) == -1,
          "POINTER escribio en un buffer pequeno"); }
  // Una URL mas larga que el buffer no se trunca: falla.
  { static char url[20 + 3001];
    strcpy(url, "https://example.com/");
    memset(url + 20, 'u', 3000);
    url[3020] = 0;
    uint8_t small[512];
    BCHECK(fbpBuildNavigate(small, sizeof(small), 1, 0, url) == -1,
          "NAVIGATE trunco una URL larga"); }

  // --- lectores del servidor ---
  {
    uint8_t p[64]; uint32_t o = 0;
    p[o++] = FBP_VERSION;
    p[o++] = 0xE0; p[o++] = 0x01;              // 480
    p[o++] = 0x20; p[o++] = 0x03;              // 800
    p[o++] = 0x0F; p[o++] = 0; p[o++] = 0; p[o++] = 0;
    p[o++] = 4; p[o++] = 0;
    p[o++] = 0x00; p[o++] = 0x00; p[o++] = 0x03; p[o++] = 0x00;
    const char* sid = "sess-123";
    p[o++] = (uint8_t)strlen(sid); p[o++] = 0;
    memcpy(p + o, sid, strlen(sid)); o += (uint32_t)strlen(sid);
    FbpWelcome w;
    BCHECK(fbpParseWelcome(p, o, &w), "WELCOME valido rechazado");
    BCHECK(w.viewW == 480 && w.viewH == 800 && w.caps == 0x0F && w.maxTabs == 4 &&
          w.maxFrameBytes == 0x30000 && !strcmp(w.sessionId, "sess-123"),
          "WELCOME mal leido");
    // Truncado en cada posicion: nunca debe aceptarlo ni leer de mas.
    for(uint32_t cut = 0; cut < o; cut++){
      FbpWelcome bad;
      BCHECK(!fbpParseWelcome(p, cut, &bad), "WELCOME truncado a %u aceptado", cut);
    }
    // Version distinta.
    { uint8_t q[64]; memcpy(q, p, o); q[0] = 2;
      FbpWelcome bad; BCHECK(!fbpParseWelcome(q, o, &bad), "WELCOME de otra version aceptado"); }
    // Viewport imposible.
    { uint8_t q[64]; memcpy(q, p, o); q[1] = 0; q[2] = 0;
      FbpWelcome bad; BCHECK(!fbpParseWelcome(q, o, &bad), "WELCOME con ancho 0 aceptado"); }
  }
  // --- build del Browser Relay del telefono, en el id de sesion ---
  //  Los MISMOS ejemplos que la prueba de Kotlin (FbpTest: "el build del relay viaja en el id de
  //  sesion"): si un extremo cambia el formato de la marca ".rN" y el otro no, falla una de las dos.
  {
    BCHECK(flexBrRelayBuild("flexphone-18c3f2a.r6") == 6, "build r6 mal leido");
    BCHECK(flexBrRelayBuild("flexphone-18c3f2a") == -1, "un relay anterior no lleva marca");
    BCHECK(flexBrRelayBuild("s-77f2") == -1, "un servicio de otro tipo no lleva marca");
    BCHECK(flexBrRelayBuild("abc.r") == -1, "marca sin numero");
    BCHECK(flexBrRelayBuild("abc.r6x") == -1, "basura detras de los digitos");
    BCHECK(flexBrRelayBuild("abc.r12345") == -1, "demasiado largo para ser un build");
    BCHECK(flexBrRelayBuild("a.r1.b.r12") == 12, "cuenta la ULTIMA marca");
    BCHECK(flexBrRelayBuild(NULL) == -1 && flexBrRelayBuild("") == -1, "nulo y vacio");
    BCHECK(flexBrRelayKind("flexphone-18c3f2a.r6") == BRRELAY_PHONE, "el relay del telefono se reconoce");
    BCHECK(flexBrRelayKind("sess-123") == BRRELAY_OTHER, "el servicio Ubuntu/PC es otro tipo");
    BCHECK(flexBrRelayKind("") == BRRELAY_UNKNOWN && flexBrRelayKind(NULL) == BRRELAY_UNKNOWN, "sin id no se sabe");
    // Solo es "antiguo" el relay del TELEFONO anterior al build que arregla viewport y desplazamiento.
    BCHECK(!flexBrRelayIsStale("flexphone-1.r6"), "r6 no es antiguo");
    BCHECK(!flexBrRelayIsStale("flexphone-1.r7"), "un build posterior tampoco");
    BCHECK(flexBrRelayIsStale("flexphone-1.r5"), "r5 es antiguo");
    BCHECK(flexBrRelayIsStale("flexphone-1"), "sin marca (anterior a r6) es antiguo");
    BCHECK(!flexBrRelayIsStale("sess-123"), "un servicio de otro tipo nunca es antiguo por esto");
    BCHECK(!flexBrRelayIsStale(NULL) && !flexBrRelayIsStale(""), "sin id no hay aviso");
    // El id etiquetado cabe en el campo del firmware (FbpWelcome.sessionId, 40 bytes).
    BCHECK(sizeof(((FbpWelcome*)0)->sessionId) >= 24, "el id de sesion del firmware no cabe");
  }
  {
    uint8_t p[256]; uint32_t o = 0;
    p[o++] = FBP_ST_LOADING | FBP_ST_SECURE | FBP_ST_CAN_BACK;
    p[o++] = 55;
    const char* t = "Un t\xC3\xADtulo";
    p[o++] = (uint8_t)strlen(t); p[o++] = 0;
    memcpy(p + o, t, strlen(t)); o += (uint32_t)strlen(t);
    const char* u = "https://example.com/";
    p[o++] = (uint8_t)strlen(u); p[o++] = 0;
    memcpy(p + o, u, strlen(u)); o += (uint32_t)strlen(u);
    FbpState s;
    BCHECK(fbpParseState(p, o, &s), "STATE valido rechazado");
    BCHECK(s.progress == 55 && (s.flags & FBP_ST_SECURE) && !strcmp(s.url, u),
          "STATE mal leido");
    for(uint32_t cut = 0; cut < o; cut++){
      FbpState bad; BCHECK(!fbpParseState(p, cut, &bad), "STATE truncado a %u aceptado", cut);
    }
    // Un titulo con saltos de linea se limpia: una pagina externa no
    // puede partir la barra del navegador con su propio texto.
    { uint8_t q[256]; memcpy(q, p, o);
      q[4] = '\n'; q[5] = '\r';
      FbpState s2;
      BCHECK(fbpParseState(q, o, &s2), "STATE con controles rechazado");
      for(const char* c = s2.title; *c; c++)
        BCHECK((unsigned char)*c >= 0x20, "quedo un caracter de control en el titulo"); }
    // Progreso fuera de rango se acota.
    { uint8_t q[256]; memcpy(q, p, o); q[1] = 240;
      FbpState s2; BCHECK(fbpParseState(q, o, &s2) && s2.progress == 100,
                         "el progreso no se acoto a 100"); }
  }
  {
    uint8_t p[128]; uint32_t o = 0;
#define w16(v) do { uint16_t v_ = (uint16_t)(v); p[o++] = (uint8_t)(v_ & 0xFF); p[o++] = (uint8_t)(v_ >> 8); } while (0)
#define w32(v) do { uint32_t v_ = (uint32_t)(v); p[o++] = (uint8_t)v_; p[o++] = (uint8_t)(v_ >> 8); \
                    p[o++] = (uint8_t)(v_ >> 16); p[o++] = (uint8_t)(v_ >> 24); } while (0)
    w16(0); w16(64); w16(480); w16(32);
    p[o++] = FBP_IMG_JPEG; p[o++] = FBP_FR_KEYFRAME | FBP_FR_LAST;
    w32(77);
    const uint8_t img[10] = { 0xFF, 0xD8, 1, 2, 3, 4, 5, 6, 7, 8 };
    w32(sizeof(img));
    memcpy(p + o, img, sizeof(img)); o += (uint32_t)sizeof(img);
    FbpFrame f;
    BCHECK(fbpParseFrame(p, o, &f), "FRAME valido rechazado");
    BCHECK(f.y == 64 && f.w == 480 && f.h == 32 && f.frameId == 77 &&
          f.imgLen == sizeof(img) && f.img[0] == 0xFF, "FRAME mal leido");
    BCHECK((f.flags & FBP_FR_KEYFRAME) != 0, "no se leyo el flag de keyframe");
    // La longitud declarada tiene que cuadrar EXACTA con los bytes que hay:
    // ni de mas (leeria fuera) ni de menos (dejaria basura pegada).
    BCHECK(!fbpParseFrame(p, o - 1, &f), "FRAME con un byte de menos aceptado");
    BCHECK(!fbpParseFrame(p, o + 1, &f), "FRAME con un byte de mas aceptado");
    { uint8_t q[128]; memcpy(q, p, o); q[8] = 99;
      BCHECK(!fbpParseFrame(q, o, &f), "FRAME con formato desconocido aceptado"); }
    { uint8_t q[128]; memcpy(q, p, o); q[4] = 0; q[5] = 0;
      BCHECK(!fbpParseFrame(q, o, &f), "FRAME de ancho 0 aceptado"); }
    { uint8_t q[128]; memcpy(q, p, o);
      q[14] = 0; q[15] = 0; q[16] = 0; q[17] = 0;
      BCHECK(!fbpParseFrame(q, o, &f), "FRAME sin imagen aceptado"); }
    for(uint32_t cut = 0; cut < o; cut++)
      BCHECK(!fbpParseFrame(p, cut, &f), "FRAME truncado a %u aceptado", cut);
  }
  {
    uint8_t p[256]; uint32_t o = 0;
    p[o++] = 1;                                   // video
    p[o++] = 0xE8; p[o++] = 0x03; p[o++] = 0; p[o++] = 0;   // 1000 ms
    p[o++] = 0x80; p[o++] = 0x02;                 // 640
    p[o++] = 0xE0; p[o++] = 0x01;                 // 480
    const char* mime = "video/mp4";
    p[o++] = (uint8_t)strlen(mime); p[o++] = 0;
    memcpy(p + o, mime, strlen(mime)); o += (uint32_t)strlen(mime);
    const char* url = "https://cdn.example.com/v.mp4";
    p[o++] = (uint8_t)strlen(url); p[o++] = 0;
    memcpy(p + o, url, strlen(url)); o += (uint32_t)strlen(url);
    p[o++] = 0; p[o++] = 0;                       // titulo vacio
    FbpMedia m;
    BCHECK(fbpParseMedia(p, o, &m), "MEDIA valido rechazado");
    BCHECK(m.kind == 1 && m.durationMs == 1000 && m.w == 640 && m.h == 480 &&
          !strcmp(m.mime, "video/mp4"), "MEDIA mal leido");
    for(uint32_t cut = 0; cut < o; cut++){
      FbpMedia bad; BCHECK(!fbpParseMedia(p, cut, &bad), "MEDIA truncado a %u aceptado", cut);
    }
  }
  {
    uint8_t p[64]; uint32_t o = 0;
    p[o++] = FBP_ERR_BLOCKED; p[o++] = 0;
    const char* m = "destino interno";
    p[o++] = (uint8_t)strlen(m); p[o++] = 0;
    memcpy(p + o, m, strlen(m)); o += (uint32_t)strlen(m);
    FbpError e;
    BCHECK(fbpParseError(p, o, &e), "ERROR valido rechazado");
    BCHECK(e.code == FBP_ERR_BLOCKED && !strcmp(e.msg, m), "ERROR mal leido");
    for(int c = 0; c <= FBP_ERR_EXPIRED; c++) BCHECK(flexBrErrorText(c)[0] != 0, "error %d sin texto", c);
  }

  // Una cadena que dice medir mas de lo que hay: rechazada siempre.
  {
    uint8_t p[32] = { 0, 0, 0xFF, 0x7F };      // flags, progreso, len=0x7FFF
    FbpState s;
    BCHECK(!fbpParseState(p, sizeof(p), &s), "cadena con longitud mentirosa aceptada");
  }
  // Una cadena mas larga que el buffer de destino se recorta pero no
  // desborda (los titulos de las paginas reales son largos de verdad).
  {
    uint8_t p[2 + 2 + 500 + 2];
    p[0] = 0; p[1] = 0;
    p[2] = (uint8_t)(500 & 0xFF); p[3] = (uint8_t)(500 >> 8);
    memset(p + 4, 'T', 500);
    p[504] = 0; p[505] = 0;
    FbpState s;
    BCHECK(fbpParseState(p, (uint32_t)sizeof(p), &s), "titulo largo rechazado");
    BCHECK(strlen(s.title) == FLEXBR_TITLE_MAX - 1, "titulo largo mal recortado: %zu",
          strlen(s.title));
  }
}

// -------------------------------------------------------------
//  4) Apreton de manos WebSocket
// -------------------------------------------------------------
static void testWebsocket(){
  /* [ws] apreton de manos */
  // Vectores de SHA-1 del FIPS 180-1 / RFC 3174.
  struct { const char* in; const char* hex; } sv[] = {
    { "", "da39a3ee5e6b4b0d3255bfef95601890afd80709" },
    { "abc", "a9993e364706816aba3e25717850c26c9cd0d89d" },
    { "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
      "84983e441c3bd26ebaae4aa1f95129e5e54670f1" },
    { 0, 0 }
  };
  for(int i = 0; sv[i].in; i++){
    uint8_t d[20]; char hex[41];
    flexBrSha1((const uint8_t*)sv[i].in, strlen(sv[i].in), d);
    for(int k = 0; k < 20; k++) sprintf(hex + k * 2, "%02x", d[k]);
    BCHECK(!strcmp(hex, sv[i].hex), "SHA-1(\"%.20s\") = %s", sv[i].in, hex);
  }
  // Un millon de 'a': comprueba el relleno multibloque.
  {
    static uint8_t big[1000000];
    memset(big, 'a', sizeof(big));
    uint8_t d[20]; char hex[41];
    flexBrSha1(big, sizeof(big), d);
    for(int k = 0; k < 20; k++) sprintf(hex + k * 2, "%02x", d[k]);
    BCHECK(!strcmp(hex, "34aa973cd4c4daa4f61eeb2bdbad27316534016f"), "SHA-1(1e6 x 'a') = %s", hex);
  }
  // Base64.
  { char b[32];
    BCHECK(flexBrBase64((const uint8_t*)"", 0, b, sizeof(b)) == 0 && b[0] == 0, "base64 vacio");
    BCHECK(flexBrBase64((const uint8_t*)"f", 1, b, sizeof(b)) == 4 && !strcmp(b, "Zg=="), "base64 \"f\" = %s", b);
    BCHECK(flexBrBase64((const uint8_t*)"fo", 2, b, sizeof(b)) == 4 && !strcmp(b, "Zm8="), "base64 \"fo\" = %s", b);
    BCHECK(flexBrBase64((const uint8_t*)"foobar", 6, b, sizeof(b)) == 8 && !strcmp(b, "Zm9vYmFy"), "base64 \"foobar\" = %s", b);
    char tiny[4];
    BCHECK(flexBrBase64((const uint8_t*)"foobar", 6, tiny, sizeof(tiny)) == -1, "base64 desbordo"); }
  // Vector del propio RFC 6455 (seccion 1.3).
  { char acc[40];
    BCHECK(flexBrWsAccept("dGhlIHNhbXBsZSBub25jZQ==", acc, sizeof(acc)) &&
          !strcmp(acc, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="),
          "Sec-WebSocket-Accept = %s", acc); }

  // Troceo de la URL del servicio.
  {
    bool tls; char host[128]; uint16_t port; char path[128];
    BCHECK(flexBrParseWsUrl("wss://nav.example.com/v1/session", &tls, host, sizeof(host), &port, path, sizeof(path)) &&
          tls && !strcmp(host, "nav.example.com") && port == 443 && !strcmp(path, "/v1/session"),
          "wss por defecto mal troceada");
    BCHECK(flexBrParseWsUrl("ws://192.168.1.10:8080/s", &tls, host, sizeof(host), &port, path, sizeof(path)) &&
          !tls && port == 8080 && !strcmp(host, "192.168.1.10") && !strcmp(path, "/s"),
          "ws con puerto mal troceada");
    BCHECK(flexBrParseWsUrl("wss://nav.example.com", &tls, host, sizeof(host), &port, path, sizeof(path)) &&
          !strcmp(path, "/"), "ruta vacia debe ser /");
    BCHECK(!flexBrParseWsUrl("https://nav.example.com/", &tls, host, sizeof(host), &port, path, sizeof(path)),
          "https no es una URL de WebSocket");
    BCHECK(!flexBrParseWsUrl("wss://user:clave@nav.example.com/", &tls, host, sizeof(host), &port, path, sizeof(path)),
          "credenciales en la URL: deben rechazarse");
    BCHECK(!flexBrParseWsUrl("wss://nav.example.com:0/", &tls, host, sizeof(host), &port, path, sizeof(path)),
          "puerto 0 aceptado");
    BCHECK(!flexBrParseWsUrl("wss://nav.example.com:99999/", &tls, host, sizeof(host), &port, path, sizeof(path)),
          "puerto fuera de rango aceptado");
    BCHECK(!flexBrParseWsUrl("", &tls, host, sizeof(host), &port, path, sizeof(path)), "URL vacia aceptada");
  }
}

// -------------------------------------------------------------
//  5) Ajustes por defecto
// -------------------------------------------------------------
static void testDefaults(){
  /* [ajustes] valores por defecto */
  BrSettings st;
  flexBrSettingsDefaults(&st);
  BCHECK(!st.allowLocal, "allowLocal debe venir apagado");
  BCHECK(!st.allowInsecure, "allowInsecure debe venir apagado");
  BCHECK(!st.telemetry, "la telemetria debe venir apagada");
  BCHECK(st.server[0] == 0 && st.token[0] == 0, "no debe haber servidor ni credencial de fabrica");
  BCHECK(st.quality >= 20 && st.quality <= 90, "calidad por defecto fuera de rango");
  BCHECK(st.scalePct >= 50 && st.scalePct <= 100, "escala por defecto fuera de rango");
  BCHECK(st.profile < BRQ_N, "perfil por defecto invalido");
}

// -------------------------------------------------------------
//  Fuente del navegador: telefono / nube / PC
// -------------------------------------------------------------
//  Lo que se prueba es que un modo EXCLUSIVO no se cae en secreto a
//  otra fuente. Un "Flex Phone" que en realidad tirara del PC seria
//  justo la mentira que este selector existe para evitar.
static void testSourceSelection(){
  /* [browser] fuente: automatico, exclusivos y sin fuente */
  BrSettings st; flexBrSettingsDefaults(&st);
  BCHECK(st.source == BRSRC_AUTO, "de fabrica deberia ser Automatico");

  // -- AUTOMATICO: telefono > nube > manual --
  st.source = BRSRC_AUTO;
  BCHECK(flexBrSourceResolve(&st, true,  true,  true)  == BRSRC_PHONE,  "auto no prefirio el telefono");
  BCHECK(flexBrSourceResolve(&st, false, true,  true)  == BRSRC_CLOUD,  "auto no cayo a la nube");
  BCHECK(flexBrSourceResolve(&st, false, false, true)  == BRSRC_MANUAL, "auto no cayo al manual");
  BCHECK(flexBrSourceResolve(&st, false, false, false) == BRSRC_NONE,   "auto invento una fuente");

  // -- EXCLUSIVOS: no hay respaldo silencioso --
  st.source = BRSRC_PHONE;
  BCHECK(flexBrSourceResolve(&st, true,  true, true) == BRSRC_PHONE, "el modo telefono no uso el telefono");
  BCHECK(flexBrSourceResolve(&st, false, true, true) == BRSRC_NONE,
        "el modo TELEFONO se cayo a otra fuente por detras");
  st.source = BRSRC_CLOUD;
  BCHECK(flexBrSourceResolve(&st, true, false, true) == BRSRC_NONE,
        "el modo NUBE se cayo a otra fuente por detras");
  st.source = BRSRC_MANUAL;
  BCHECK(flexBrSourceResolve(&st, true, true, false) == BRSRC_NONE,
        "el modo MANUAL se cayo a otra fuente por detras");
  BCHECK(flexBrSourceResolve(&st, false, false, true) == BRSRC_MANUAL,
        "el modo manual no uso el servidor de siempre");

  // -- Nunca devuelve AUTO: es una preferencia, no un destino --
  st.source = BRSRC_AUTO;
  for(int a = 0; a < 8; a++){
    const int r = flexBrSourceResolve(&st, (a & 1) != 0, (a & 2) != 0, (a & 4) != 0);
    BCHECK(r != BRSRC_AUTO, "resolvio a AUTO, que no es un destino");
  }

  // -- El motivo de "sin fuente" es concreto --
  st.source = BRSRC_PHONE;
  BCHECK(strstr(flexBrSourceWhyNone(&st, false, true, true), "Relay") != NULL,
        "el motivo no menciona el relay");
  st.source = BRSRC_CLOUD;
  BCHECK(strstr(flexBrSourceWhyNone(&st, false, false, true), "nube") != NULL,
        "el motivo no menciona la nube");
  st.source = BRSRC_AUTO;
  BCHECK(flexBrSourceWhyNone(&st, true, false, false)[0] == 0,
        "dio un motivo de fallo habiendo fuente");

  // -- Nombres legibles --
  BCHECK(strcmp(flexBrSourceName(BRSRC_PHONE), "Flex Phone") == 0, "nombre de fuente mal");
  BCHECK(strcmp(flexBrSourceName(BRSRC_NONE), "Sin fuente") == 0, "nombre de 'sin fuente' mal");

  // -- Argumentos imposibles --
  BCHECK(flexBrSourceResolve(NULL, true, true, true) == BRSRC_NONE, "acepto ajustes nulos");
}
/* ===== Vectores dorados del perfil S3 =====
 * Salida del códec REAL de la referencia compilado con el perfil S3
 * (FLEXBR_KEYFRAME_MAX 131072, 4 pestañas): estos bytes son los que espera
 * el servicio. Si el port cambiara un offset, falla aquí. */
static bool hexeq(const uint8_t *b, int n, const char *hex)
{
    if (n < 0 || (size_t)n * 2 != strlen(hex)) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1 || v != b[i]) {
            return false;
        }
    }
    return true;
}

static void testGoldenS3(void)
{
    uint8_t b[256];
    BCHECK(FLEXBR_KEYFRAME_MAX == 131072 && FLEXBR_MAX_TABS == 4 && FBP_PAYLOAD_MAX == 135168, "perfil S3");
    fbpWriteHeader(b, FBP_C_PING, 3, 2, 0x1234, 7);
    BCHECK(hexeq(b, 12, "464201020302341207000000"), "cabecera");
    int n = fbpBuildHello(b, sizeof(b), 0, "token-secreto", "flexos-abc", 410, 400, 62, 1, 0x7B);
    BCHECK(hexeq(b, n, "46420101000000002C000000019A0190013E017B0000000104000002000D00746F6B656E2D7365637265746F"
                       "0A00666C65786F732D616263"), "HELLO 410x400");
    n = fbpBuildHello(b, sizeof(b), 1, "token-secreto", "flexos-abc", 480, 800, 60, 1, 0x0F);
    BCHECK(hexeq(b, n, "46420101000001002C00000001E00120033C010F0000000104000002000D00746F6B656E2D7365637265746F"
                       "0A00666C65786F732D616263"), "HELLO 480x800");
    n = fbpBuildNavigate(b, sizeof(b), 2, 1, "https://example.com/x");
    BCHECK(hexeq(b, n, "464201100001020017000000150068747470733A2F2F6578616D706C652E636F6D2F78"), "NAVIGATE");
    n = fbpBuildSimple(b, sizeof(b), 0, 0, FBP_C_PING);
    BCHECK(hexeq(b, n, "464201020000000000000000"), "PING");
    n = fbpBuildSimple(b, sizeof(b), 5, 1, FBP_C_BACK);
    BCHECK(hexeq(b, n, "464201110001050000000000"), "BACK");
    n = fbpBuildSimple(b, sizeof(b), 6, 1, FBP_C_REQ_FRAME);
    BCHECK(hexeq(b, n, "464201410001060000000000"), "REQ_FRAME");
    n = fbpBuildPointer(b, sizeof(b), 3, 1, FBP_PTR_TAP, 240, 400, 0);
    BCHECK(hexeq(b, n, "46420120000103000600000003F000900100"), "POINTER");
    n = fbpBuildScroll(b, sizeof(b), 4, 1, -120, 340);
    BCHECK(hexeq(b, n, "46420121000104000400000088FF5401"), "SCROLL");
    n = fbpBuildKey(b, sizeof(b), 5, 1, FBP_KEY_TEXT, 0, 0, "\xC3\xB1");
    BCHECK(hexeq(b, n, "4642012200010500070000000000000200C3B1"), "KEY ñ");
    n = fbpBuildKey(b, sizeof(b), 9, 1, FBP_KEY_DOWN, FBP_VK_ENTER, 0, NULL);
    BCHECK(hexeq(b, n, "4642012200010900050000000101000000"), "KEY Intro");
    n = fbpBuildViewport(b, sizeof(b), 6, 1, 480, 736, 70, BRQ_LOWLAT, 100);
    BCHECK(hexeq(b, n, "464201230001060007000000E001E002460064"), "VIEWPORT");
    n = fbpBuildAck(b, sizeof(b), 0, 1000, 1);
    BCHECK(hexeq(b, n, "464201030000000003000000E80301"), "ACK");
    n = fbpBuildMedia(b, sizeof(b), 8, FBP_CH_MEDIA, FBP_MED_SEEK, 45000);
    BCHECK(hexeq(b, n, "4642014000C808000500000002C8AF0000"), "MEDIA");
}

void test_browser_all(void)
{
    testOmnibox();
    testUrlHelpers();
    testProtocol();
    testWebsocket();
    testDefaults();
    testSourceSelection();
    testGoldenS3();
}
