// #############################################################
//  test_id3.cpp  ·  RELOJ: pruebas de host de flex_id3.c
// #############################################################
//
//  Las etiquetas ID3 llegan de cualquier sitio (lo que sube el movil,
//  lo que hay en una microSD). Lo que se prueba:
//    · v2.2, v2.3 y v2.4 de verdad: textos en Latin-1, UTF-16 con y sin
//      BOM, UTF-16BE y UTF-8, y la portada (APIC/PIC) localizada sin leerla;
//    · tamaños que mienten (marco o etiqueta mas grandes que el archivo),
//      cabecera extendida, desincronizacion, marcos comprimidos;
//    · texto recortado sin partir caracteres y siempre UTF-8 valido;
//    · miles de etiquetas mutadas al azar bajo ASan/UBSan: nunca lee
//      fuera ni devuelve una portada que se salga del archivo.

#include "flex_id3.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

struct Mem { std::vector<uint8_t> b; uint32_t pos = 0; };
static int memRead(void* c, void* buf, uint32_t n){
  Mem* m = (Mem*)c;
  uint32_t left = m->pos < m->b.size() ? (uint32_t)m->b.size() - m->pos : 0;
  if(n > left) n = left;
  if(n > 333) n = 333;
  std::memcpy(buf, m->b.data() + m->pos, n);
  m->pos += n;
  return (int)n;
}
static bool memSeek(void* c, uint32_t off){ Mem* m = (Mem*)c; if(off > m->b.size()) return false; m->pos = off; return true; }
static uint32_t memSize(void* c){ return (uint32_t)((Mem*)c)->b.size(); }
static int rd(Mem& m, FlexId3Info* i){ FlexMediaIO io = { memRead, memSeek, memSize, &m }; return flexId3Read(&io, i); }

static void ss(uint8_t* p, uint32_t v){ p[0] = (v >> 21) & 0x7F; p[1] = (v >> 14) & 0x7F; p[2] = (v >> 7) & 0x7F; p[3] = v & 0x7F; }
static void b32(uint8_t* p, uint32_t v){ p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

// Marco v2.3/v2.4 (cabecera de 10 bytes).
static void frame(std::vector<uint8_t>& t, const char* id, const std::vector<uint8_t>& d, int ver, uint16_t fl = 0){
  size_t at = t.size(); t.resize(at + 10);
  std::memcpy(&t[at], id, 4);
  if(ver == 4) ss(&t[at + 4], (uint32_t)d.size()); else b32(&t[at + 4], (uint32_t)d.size());
  t[at + 8] = fl >> 8; t[at + 9] = fl & 0xFF;
  t.insert(t.end(), d.begin(), d.end());
}
static std::vector<uint8_t> txt(uint8_t enc, const std::vector<uint8_t>& s){ std::vector<uint8_t> v{enc}; v.insert(v.end(), s.begin(), s.end()); return v; }
static std::vector<uint8_t> bytes(const char* s){ return std::vector<uint8_t>(s, s + std::strlen(s)); }

// Etiqueta completa con relleno, seguida de "audio".
static std::vector<uint8_t> tag(int ver, uint8_t flags, const std::vector<uint8_t>& frames, size_t pad = 32){
  std::vector<uint8_t> t(10);
  std::memcpy(&t[0], "ID3", 3); t[3] = (uint8_t)ver; t[4] = 0; t[5] = flags;
  t.insert(t.end(), frames.begin(), frames.end());
  t.resize(t.size() + pad, 0);
  ss(&t[6], (uint32_t)(t.size() - 10));
  for(int i = 0; i < 4000; i++) t.push_back((uint8_t)(0xFF - i % 7));
  return t;
}

static bool utf8ok(const char* s){
  const unsigned char* p = (const unsigned char*)s;
  while(*p){
    int n = *p < 0x80 ? 0 : (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2 : (*p & 0xF8) == 0xF0 ? 3 : -1;
    if(n < 0) return false;
    for(int k = 1; k <= n; k++) if((p[k] & 0xC0) != 0x80) return false;
    p += n + 1;
  }
  return true;
}

static void testV23(){
  std::printf("-- ID3v2.3: textos en cuatro codificaciones y portada --\n");
  std::vector<uint8_t> f;
  frame(f, "TIT2", txt(0, {'C','a','n','c','i',0xF3,'n'}), 3);                         // Latin-1
  frame(f, "TPE1", txt(1, {0xFF,0xFE, 'N',0, 0xF1,0, 'u',0, 0x3D,0xD8,0xB5,0xDE, 0,0}), 3); // UTF-16LE + emoji
  frame(f, "TALB", txt(3, bytes("Álbum ñ")), 3);                                       // UTF-8
  frame(f, "TCON", txt(0, bytes("Rock")), 3);
  std::vector<uint8_t> apic{0};
  for(char c : std::string("image/jpeg")) apic.push_back((uint8_t)c);
  apic.push_back(0); apic.push_back(3); apic.push_back('x'); apic.push_back(0);
  for(int i = 0; i < 700; i++) apic.push_back((uint8_t)i);
  apic[apic.size() - 700] = 0xFF;
  frame(f, "APIC", apic, 3);
  Mem m; m.b = tag(3, 0, f);
  FlexId3Info i;
  CHECK(rd(m, &i) == FLEXID3_OK && i.version == 3, "v2.3");
  CHECK(!std::strcmp(i.title, "Canci\xC3\xB3n"), "titulo Latin-1 -> UTF-8: '%s'", i.title);
  CHECK(!std::strcmp(i.artist, "N\xC3\xB1u\xF0\x9F\x9A\xB5"), "artista UTF-16 con BOM y par sustituto: '%s'", i.artist);
  CHECK(!std::strcmp(i.album, "\xC3\x81lbum \xC3\xB1"), "album UTF-8: '%s'", i.album);
  CHECK(i.picKind == FLEXID3_PIC_JPEG && i.picBytes == 700, "portada JPEG de 700 B (%u)", i.picBytes);
  CHECK(i.picOffset + i.picBytes <= m.b.size() && m.b[i.picOffset] == 0xFF, "la portada empieza donde dice");
  CHECK(i.tagBytes == 10u + (uint32_t)(m.b.size() - 4000 - 10), "tamano de la etiqueta");
}

static void testV24(){
  std::printf("-- ID3v2.4: sincroseguro, longitud, pie y portada preferida --\n");
  std::vector<uint8_t> f;
  std::vector<uint8_t> t = txt(2, {0, 'H', 0, 'o', 0, 'l', 0, 'a'});          // UTF-16BE
  frame(f, "TIT2", t, 4);
  std::vector<uint8_t> dl{0, 0, 0, 5};                                         // indicador de longitud
  std::vector<uint8_t> art = txt(3, bytes("Yo"));
  dl.insert(dl.end(), art.begin(), art.end());
  frame(f, "TPE1", dl, 4, 0x0001);
  std::vector<uint8_t> comp = txt(0, bytes("NO"));
  frame(f, "TALB", comp, 4, 0x0008);                                           // comprimido: se salta
  std::vector<uint8_t> p1{0}; for(char c : std::string("image/png")) p1.push_back((uint8_t)c);
  p1.push_back(0); p1.push_back(4); p1.push_back(0); for(int k = 0; k < 50; k++) p1.push_back(1);
  frame(f, "APIC", p1, 4);                                                     // trasera
  std::vector<uint8_t> p2{1}; for(char c : std::string("image/jpeg")) p2.push_back((uint8_t)c);
  p2.push_back(0); p2.push_back(3); p2.push_back(0xFF); p2.push_back(0xFE); p2.push_back('a'); p2.push_back(0);
  p2.push_back(0); p2.push_back(0); for(int k = 0; k < 90; k++) p2.push_back(2);
  frame(f, "APIC", p2, 4);                                                     // delantera, descripcion UTF-16
  Mem m; m.b = tag(4, 0x10, f);
  FlexId3Info i;
  CHECK(rd(m, &i) == FLEXID3_OK && i.version == 4, "v2.4");
  CHECK(!std::strcmp(i.title, "Hola"), "UTF-16BE: '%s'", i.title);
  CHECK(!std::strcmp(i.artist, "Yo"), "indicador de longitud saltado: '%s'", i.artist);
  CHECK(i.album[0] == 0, "comprimido: ignorado");
  CHECK(i.picKind == FLEXID3_PIC_JPEG && i.picBytes == 90, "la portada delantera gana (%u)", i.picBytes);
  CHECK(i.tagBytes == 10u + (uint32_t)(m.b.size() - 4000 - 10) + 10u, "pie de 10 bytes contado");
}

static void testV22(){
  std::printf("-- ID3v2.2: marcos de 3 letras --\n");
  std::vector<uint8_t> f;
  auto fr = [&](const char* id, const std::vector<uint8_t>& d){
    size_t at = f.size(); f.resize(at + 6); std::memcpy(&f[at], id, 3);
    f[at + 3] = 0; f[at + 4] = (uint8_t)(d.size() >> 8); f[at + 5] = (uint8_t)d.size();
    f.insert(f.end(), d.begin(), d.end());
  };
  fr("TT2", txt(0, bytes("Viejo")));
  fr("TP1", txt(0, bytes("Grupo")));
  std::vector<uint8_t> pic{0, 'J', 'P', 'G', 3, 0}; for(int k = 0; k < 40; k++) pic.push_back(9);
  fr("PIC", pic);
  Mem m; m.b = tag(2, 0, f);
  FlexId3Info i;
  CHECK(rd(m, &i) == FLEXID3_OK && i.version == 2, "v2.2");
  CHECK(!std::strcmp(i.title, "Viejo") && !std::strcmp(i.artist, "Grupo"), "textos v2.2");
  CHECK(i.picKind == FLEXID3_PIC_JPEG && i.picBytes == 40, "PIC JPG");
}

static void testBroken(){
  std::printf("-- tamanos que mienten, extendida, desincronizacion --\n");
  FlexId3Info i;
  Mem none; none.b.assign(100, 0x33);
  CHECK(rd(none, &i) == FLEXID3_NONE, "sin etiqueta");
  Mem tiny; tiny.b = {'I','D','3'};
  CHECK(rd(tiny, &i) == FLEXID3_NONE, "archivo de 3 bytes");
  // Marco que dice medir mas que la etiqueta: se para, sin leer fuera.
  std::vector<uint8_t> f;
  frame(f, "TIT2", txt(0, bytes("Bien")), 3);
  size_t at = f.size();
  frame(f, "TPE1", txt(0, bytes("Mal")), 3);
  b32(&f[at + 4], 0x7FFFFFF0u);
  Mem m; m.b = tag(3, 0, f);
  CHECK(rd(m, &i) == FLEXID3_OK && !std::strcmp(i.title, "Bien") && i.artist[0] == 0, "marco gigante: se para");
  // Etiqueta que dice medir mas que el archivo.
  Mem t; t.b = tag(3, 0, f); ss(&t.b[6], 0x0FFFFFFF); t.b.resize(200);
  CHECK(rd(t, &i) == FLEXID3_OK && i.tagBytes > t.b.size(), "etiqueta mayor que el archivo: acotada");
  // Cabecera extendida (v2.3: 6 bytes + 4 de tamano).
  std::vector<uint8_t> ext{0, 0, 0, 6, 0, 0, 0, 0, 0, 0};
  std::vector<uint8_t> f2 = ext;
  frame(f2, "TIT2", txt(0, bytes("Tras la extendida")), 3);
  Mem e; e.b = tag(3, 0x40, f2);
  CHECK(rd(e, &i) == FLEXID3_OK && !std::strcmp(i.title, "Tras la extendida"), "extendida saltada: '%s'", i.title);
  // Extendida que se sale: se deja de leer, sin error.
  std::vector<uint8_t> bad{0x7F, 0xFF, 0xFF, 0xFF};
  Mem eb; eb.b = tag(3, 0x40, bad);
  CHECK(rd(eb, &i) == FLEXID3_OK && i.title[0] == 0, "extendida rota");
  // Desincronizacion de la etiqueta: textos si, portada no.
  std::vector<uint8_t> f3;
  frame(f3, "TIT2", txt(0, {'A', 0xFF, 0x00, 'B'}), 3);
  std::vector<uint8_t> ap{0}; for(char c : std::string("image/jpeg")) ap.push_back((uint8_t)c);
  ap.push_back(0); ap.push_back(3); ap.push_back(0); ap.push_back(0xFF); ap.push_back(0x00); ap.push_back(0xD8);
  frame(f3, "APIC", ap, 3);
  Mem u; u.b = tag(3, 0x80, f3);
  CHECK(rd(u, &i) == FLEXID3_OK && !std::strcmp(i.title, "A\xC3\xBF" "B"), "texto desincronizado: '%s'", i.title);
  CHECK(i.picKind == FLEXID3_PIC_NONE, "portada desincronizada: ignorada");
  // APIC sin terminador de descripcion dentro de lo leido.
  std::vector<uint8_t> f4;
  std::vector<uint8_t> ap2{0}; for(char c : std::string("image/jpeg")) ap2.push_back((uint8_t)c);
  ap2.push_back(0); ap2.push_back(3); for(int k = 0; k < 400; k++) ap2.push_back('d');
  frame(f4, "APIC", ap2, 3);
  Mem nt; nt.b = tag(3, 0, f4);
  CHECK(rd(nt, &i) == FLEXID3_OK && i.picKind == FLEXID3_PIC_NONE, "descripcion sin fin: sin portada");
  // Version 5 o 0xFF: no es una etiqueta que se entienda.
  Mem v5; v5.b = tag(3, 0, f); v5.b[3] = 5;
  CHECK(rd(v5, &i) == FLEXID3_NONE, "v2.5: no");
}

static void testText(){
  std::printf("-- texto: recorte sin partir caracteres --\n");
  char o[8];
  const uint8_t s[] = {'a','b','c','d','e',0xC3,0xB1,'x'};
  flexId3Text(3, s, sizeof(s), o, sizeof(o));
  CHECK(!std::strcmp(o, "abcde") || !std::strcmp(o, "abcde\xC3\xB1"), "recorte: '%s'", o);
  CHECK(utf8ok(o), "UTF-8 valido tras recortar");
  const uint8_t bad[] = {'a', 0xC3, 'b', 0xFF, 0xED, 0xA0, 0x80, 'c'};   // cortado, invalido, sustituto en UTF-8
  char o2[32];
  flexId3Text(3, bad, sizeof(bad), o2, sizeof(o2));
  CHECK(utf8ok(o2) && o2[0] == 'a', "UTF-8 roto -> '?': '%s'", o2);
  const uint8_t lone[] = {0xFF, 0xFE, 0x00, 0xD8, 'z', 0};                // sustituto suelto
  flexId3Text(1, lone, sizeof(lone), o2, sizeof(o2));
  CHECK(utf8ok(o2), "sustituto suelto: '%s'", o2);
  const uint8_t ctl[] = {'a', 0x01, 'b', ' ', ' '};
  flexId3Text(0, ctl, sizeof(ctl), o2, sizeof(o2));
  CHECK(!std::strcmp(o2, "a b"), "controles a espacio, sin espacios al final: '%s'", o2);
  CHECK(flexId3Text(9, ctl, sizeof(ctl), o2, sizeof(o2)) == 0 && o2[0] == 0, "codificacion desconocida: vacio");
}

static void testFuzz(){
  std::printf("-- 20000 etiquetas mutadas --\n");
  std::vector<uint8_t> f;
  frame(f, "TIT2", txt(1, {0xFF,0xFE,'a',0,'b',0}), 3);
  frame(f, "TPE1", txt(3, bytes("x")), 3);
  std::vector<uint8_t> ap{0}; for(char c : std::string("image/jpeg")) ap.push_back((uint8_t)c);
  ap.push_back(0); ap.push_back(3); ap.push_back(0); for(int k = 0; k < 300; k++) ap.push_back((uint8_t)k);
  frame(f, "APIC", ap, 3);
  const std::vector<uint8_t> base = tag(3, 0, f, 8);
  uint32_t s = 7;
  for(int it = 0; it < 20000; it++){
    Mem m; m.b = base;
    int muts = 1 + (int)(s % 6);
    for(int k = 0; k < muts; k++){
      s = s * 1103515245u + 12345u;
      size_t at = (s >> 8) % std::min<size_t>(m.b.size(), 600);
      m.b[at] = (uint8_t)(s >> 16);
    }
    if(it % 5 == 0) m.b.resize((s >> 4) % m.b.size());
    if(it % 7 == 0 && m.b.size() > 4) m.b[3] = (uint8_t)(2 + it % 3);
    FlexId3Info i;
    int r = rd(m, &i);
    if(r == FLEXID3_OK){
      CHECK(utf8ok(i.title) && utf8ok(i.artist) && utf8ok(i.album), "texto valido (%d)", it);
      if(i.picKind) CHECK((uint64_t)i.picOffset + i.picBytes <= m.b.size(), "portada dentro del archivo (%d)", it);
    }
  }
}

int main(){
  std::printf("=== Flex OS Watch Ultra · etiquetas ID3v2 ===\n");
  testV23();
  testV24();
  testV22();
  testBroken();
  testText();
  testFuzz();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
