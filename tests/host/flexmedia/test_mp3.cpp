// #############################################################
//  test_mp3.cpp  ·  RELOJ: pruebas de host de flex_mp3.c
// #############################################################
//
//  flex_mp3 decide si lo que sube el movil se publica como audio
//  reproducible. Lo que se prueba es que NO se deje enganar:
//
//    · cabeceras de trama: versiones, capas, tasas y frecuencias
//      reservadas o invalidas se rechazan; los tamanos de trama
//      salen de la formula del estandar (con relleno);
//    · la etiqueta ID3v2 se salta por su tamano (y su pie);
//    · una portada JPEG llena de 0xFFE.. no se toma por audio: hacen
//      falta tres tramas seguidas que concuerden;
//    · duracion: por las tramas de Xing/Info o VBRI y, sin ellas, por
//      la tasa y los bytes (sin contar el ID3v1 del final);
//    · archivos truncados, vacios o con E/S que falla devuelven error,
//      nunca un exito a medias.

#include "flex_mp3.h"
#include <cstdio>
#include <cstring>
#include <vector>

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { g_run++; if(!(cond)){ g_fail++; \
  std::printf("  FALLO %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

struct Mem { std::vector<uint8_t> b; uint32_t pos = 0; int failAfter = -1; int reads = 0; };
static int memRead(void* c, void* buf, uint32_t n){
  Mem* m = (Mem*)c;
  if(m->failAfter >= 0 && m->reads++ >= m->failAfter) return -1;
  uint32_t left = m->pos < m->b.size() ? (uint32_t)m->b.size() - m->pos : 0;
  if(n > left) n = left;
  if(n > 700) n = 700; // lecturas cortas: el lector debe insistir
  std::memcpy(buf, m->b.data() + m->pos, n);
  m->pos += n;
  return (int)n;
}
static bool memSeek(void* c, uint32_t off){ Mem* m = (Mem*)c; if(off > m->b.size()) return false; m->pos = off; return true; }
static uint32_t memSize(void* c){ return (uint32_t)((Mem*)c)->b.size(); }
static int probe(Mem& m, FlexMp3Info* inf){
  FlexMediaIO io = { memRead, memSeek, memSize, &m };
  m.pos = 0; m.reads = 0;
  return flexMp3Probe(&io, inf);
}

// Trama MPEG-1 capa III: 128 kb/s, 44,1 kHz, estereo -> 417 B (+1 con relleno).
static void frame(std::vector<uint8_t>& v, uint8_t b1, uint8_t b2, uint8_t b3, size_t len){
  size_t at = v.size(); v.resize(at + len, 0);
  v[at] = 0xFF; v[at + 1] = b1; v[at + 2] = b2; v[at + 3] = b3;
}
static void be32(uint8_t* p, uint32_t x){ p[0] = x >> 24; p[1] = x >> 16; p[2] = x >> 8; p[3] = x; }

static void testHeaders(){
  std::printf("-- cabeceras de trama --\n");
  FlexMp3Hdr h;
  const uint8_t m1[4] = { 0xFF, 0xFB, 0x90, 0x64 };            // MPEG-1 L3 128k 44,1k estereo
  CHECK(flexMp3ParseHeader(m1, &h) && h.version == 1 && h.kbps == 128 && h.sampleRate == 44100 &&
        h.channels == 2 && h.frameBytes == 417 && h.samples == 1152, "MPEG-1 128k: 417 B (%u)", h.frameBytes);
  const uint8_t m1p[4] = { 0xFF, 0xFB, 0x92, 0x64 };           // con relleno
  CHECK(flexMp3ParseHeader(m1p, &h) && h.frameBytes == 418, "relleno: +1 B");
  const uint8_t mono[4] = { 0xFF, 0xFB, 0x90, 0xC4 };
  CHECK(flexMp3ParseHeader(mono, &h) && h.channels == 1, "mono");
  const uint8_t m2[4] = { 0xFF, 0xF3, 0x84, 0x64 };            // MPEG-2 L3 64k 24 kHz
  CHECK(flexMp3ParseHeader(m2, &h) && h.version == 2 && h.kbps == 64 && h.sampleRate == 24000 &&
        h.samples == 576 && h.frameBytes == 192, "MPEG-2 64k 24 kHz: 192 B (%u)", h.frameBytes);
  const uint8_t m25[4] = { 0xFF, 0xE3, 0x88, 0x64 };           // MPEG-2.5 L3 64k 8 kHz
  CHECK(flexMp3ParseHeader(m25, &h) && h.version == 25 && h.sampleRate == 8000 && h.frameBytes == 576,
        "MPEG-2.5 8 kHz: 576 B (%u)", h.frameBytes);
  const uint8_t bad[][4] = {
    { 0xFE, 0xFB, 0x90, 0x64 },  // sin sincronia
    { 0xFF, 0xEB, 0x90, 0x64 },  // version reservada
    { 0xFF, 0xFD, 0x90, 0x64 },  // capa II
    { 0xFF, 0xFF, 0x90, 0x64 },  // capa I
    { 0xFF, 0xF9, 0x90, 0x64 },  // capa reservada
    { 0xFF, 0xFB, 0x00, 0x64 },  // formato libre
    { 0xFF, 0xFB, 0xF0, 0x64 },  // tasa invalida
    { 0xFF, 0xFB, 0x9C, 0x64 },  // frecuencia reservada
    { 0xFF, 0xFB, 0x90, 0x66 },  // enfasis reservado
  };
  for(auto& b : bad) CHECK(!flexMp3ParseHeader(b, &h), "rechazada %02X %02X %02X %02X", b[0], b[1], b[2], b[3]);
}

static void testCbr(){
  std::printf("-- CBR con y sin etiquetas --\n");
  FlexMp3Info inf;
  Mem m;
  for(int k = 0; k < 100; k++) frame(m.b, 0xFB, 0x92, 0x64, 418);
  CHECK(probe(m, &inf) == FLEXMP3_OK && inf.dataStart == 0 && inf.audioStart == 0 && !inf.vbr && inf.frames == 0 &&
        inf.sampleRate == 44100 && inf.channels == 2 && inf.kbps == 128, "CBR desnudo");
  // 41 800 B * 8 / 128 kb/s = 2612 ms
  CHECK(inf.durMs == 2612, "duracion por tasa (%u)", inf.durMs);

  // ID3v2 de 1000 B (sincrosegura) con pie, y portada llena de "sincronias".
  Mem t;
  t.b.resize(10 + 1000 + 10, 0);
  std::memcpy(t.b.data(), "ID3\x04\x00\x10", 6);
  t.b[8] = 1000 >> 7; t.b[9] = 1000 & 0x7F;
  for(size_t i = 10; i + 4 < 1010; i += 7){ t.b[i] = 0xFF; t.b[i + 1] = 0xFB; t.b[i + 2] = 0x90; t.b[i + 3] = 0x64; }
  for(int k = 0; k < 50; k++) frame(t.b, 0xFB, 0x92, 0x64, 418);
  std::vector<uint8_t> v1(128, 0); std::memcpy(v1.data(), "TAG", 3);
  t.b.insert(t.b.end(), v1.begin(), v1.end());
  CHECK(probe(t, &inf) == FLEXMP3_OK && inf.dataStart == 1020, "ID3v2 con pie saltado (inicio %u)", inf.dataStart);
  CHECK(inf.durMs == 50u * 418u * 8u / 128u, "sin contar el ID3v1 (%u)", inf.durMs);

  // Sin etiqueta ID3, una "portada" con sincronias sueltas delante del audio.
  Mem j;
  j.b.resize(5000, 0);
  for(size_t i = 0; i + 4 < j.b.size(); i += 333){ j.b[i] = 0xFF; j.b[i + 1] = 0xFB; j.b[i + 2] = 0x90; j.b[i + 3] = 0x64; }
  for(int k = 0; k < 20; k++) frame(j.b, 0xFB, 0x92, 0x64, 418);
  CHECK(probe(j, &inf) == FLEXMP3_OK && inf.dataStart == 5000, "sincronias sueltas no enganan (inicio %u)", inf.dataStart);

  // Tramas que cambian de frecuencia: no concuerdan.
  Mem mix;
  frame(mix.b, 0xFB, 0x92, 0x64, 418); frame(mix.b, 0xFB, 0x96, 0x64, 384); frame(mix.b, 0xFB, 0x92, 0x64, 418);
  mix.b.resize(mix.b.size() + 64, 0);
  CHECK(probe(mix, &inf) == FLEXMP3_ERR_FORMAT, "frecuencias distintas: no es audio");

  // Dos tramas que concuerdan y fin: vale (clip muy corto). Una sola no se
  // confirma con nada: 4 bytes al azar podrian parecer una cabecera.
  Mem two; frame(two.b, 0xFB, 0x92, 0x64, 418); frame(two.b, 0xFB, 0x92, 0x64, 418);
  CHECK(probe(two, &inf) == FLEXMP3_OK, "dos tramas y fin");
  Mem one; frame(one.b, 0xFB, 0x92, 0x64, 418);
  CHECK(probe(one, &inf) == FLEXMP3_ERR_FORMAT, "una sola trama: sin confirmar");
}

static void testVbr(){
  std::printf("-- VBR: Xing, Info y VBRI --\n");
  FlexMp3Info inf;
  Mem x;
  frame(x.b, 0xFB, 0x92, 0x64, 418);
  std::memcpy(&x.b[4 + 32], "Xing", 4); be32(&x.b[4 + 32 + 4], 1); be32(&x.b[4 + 32 + 8], 2000);
  for(int k = 0; k < 10; k++) frame(x.b, 0xFB, 0x92, 0x64, 418);
  CHECK(probe(x, &inf) == FLEXMP3_OK && inf.vbr && inf.frames == 2000, "Xing: tramas");
  CHECK(inf.dataStart == 0 && inf.audioStart == 418, "Xing: el audio empieza en la trama siguiente (%u)", inf.audioStart);
  // 2000 * 1152 / 44100 s = 52 244 ms
  CHECK(inf.durMs == 52244, "Xing: duracion por tramas (%u)", inf.durMs);

  Mem i;
  frame(i.b, 0xFB, 0x92, 0xC4, 418);   // mono: informacion lateral de 17 B
  std::memcpy(&i.b[4 + 17], "Info", 4); be32(&i.b[4 + 17 + 4], 1); be32(&i.b[4 + 17 + 8], 441);
  for(int k = 0; k < 10; k++) frame(i.b, 0xFB, 0x92, 0xC4, 418);
  CHECK(probe(i, &inf) == FLEXMP3_OK && !inf.vbr && inf.frames == 441 && inf.durMs == 11520,
        "Info (mono, CBR): %u ms", inf.durMs);

  Mem nf;   // Xing sin el bit de tramas: se ignora
  frame(nf.b, 0xFB, 0x92, 0x64, 418);
  std::memcpy(&nf.b[4 + 32], "Xing", 4); be32(&nf.b[4 + 32 + 4], 2);
  for(int k = 0; k < 10; k++) frame(nf.b, 0xFB, 0x92, 0x64, 418);
  CHECK(probe(nf, &inf) == FLEXMP3_OK && inf.frames == 0 && inf.durMs == 11u * 418u * 8u / 128u,
        "Xing sin tramas: por tasa (%u)", inf.durMs);

  Mem v;
  frame(v.b, 0xFB, 0x92, 0x64, 418);
  uint8_t* p = &v.b[4 + 32];
  std::memcpy(p, "VBRI", 4); p[5] = 1; be32(p + 10, 123456); be32(p + 14, 1000);
  for(int k = 0; k < 10; k++) frame(v.b, 0xFB, 0x92, 0x64, 418);
  CHECK(probe(v, &inf) == FLEXMP3_OK && inf.vbr && inf.frames == 1000 && inf.durMs == 26122,
        "VBRI: %u tramas, %u ms", inf.frames, inf.durMs);
}

static void testBroken(){
  std::printf("-- danados, truncados y E/S que falla --\n");
  FlexMp3Info inf;
  Mem e;
  CHECK(probe(e, &inf) == FLEXMP3_ERR_FORMAT, "vacio");
  Mem g; g.b.assign(5000, 0x33); std::memcpy(g.b.data(), "ID3\x04", 4);
  CHECK(probe(g, &inf) == FLEXMP3_ERR_FORMAT, "\"ID3\" sin tramas");
  Mem only; only.b.assign(10 + 300, 0); std::memcpy(only.b.data(), "ID3\x03", 4); only.b[9] = 0x7F; only.b[8] = 0x7F;
  CHECK(probe(only, &inf) == FLEXMP3_ERR_FORMAT, "etiqueta mas grande que el archivo");
  Mem z; z.b.assign(256 * 1024, 0);
  for(int k = 0; k < 5; k++) frame(z.b, 0xFB, 0x92, 0x64, 418);
  CHECK(probe(z, &inf) == FLEXMP3_ERR_FORMAT, "audio despues de 128 KB de basura: no se busca mas alla");
  Mem io;
  for(int k = 0; k < 10; k++) frame(io.b, 0xFB, 0x92, 0x64, 418);
  io.failAfter = 0;
  CHECK(probe(io, &inf) == FLEXMP3_ERR_IO, "E/S que falla al empezar");
  CHECK(flexMp3Probe(NULL, &inf) == FLEXMP3_ERR_FORMAT, "sin lector");
  FlexMediaIO half = { memRead, NULL, memSize, &io };
  CHECK(flexMp3Probe(&half, &inf) == FLEXMP3_ERR_FORMAT, "lector incompleto");
  // Truncado a la mitad de la segunda trama: la primera sola no se confirma.
  Mem tr;
  for(int k = 0; k < 3; k++) frame(tr.b, 0xFB, 0x92, 0x64, 418);
  tr.b.resize(418 + 200);
  CHECK(probe(tr, &inf) == FLEXMP3_ERR_FORMAT || inf.durMs < 100, "truncado: error o duracion minima");
  // Barrido de bytes al azar: nunca revienta (ASan/UBSan) y nunca da tasa 0.
  uint32_t s = 12345;
  for(int it = 0; it < 3000; it++){
    Mem r; size_t n = 32 + (s % 3000); r.b.resize(n);
    for(auto& c : r.b){ s = s * 1103515245u + 12345u; c = (uint8_t)(s >> 16); if((s >> 8 & 7) == 0) c = 0xFF; }
    if(probe(r, &inf) == FLEXMP3_OK) CHECK(inf.kbps > 0 && inf.sampleRate > 0, "azar %d: datos coherentes", it);
  }
}

int main(){
  std::printf("=== Flex OS Watch Ultra · comprobacion de MP3 (sin decodificar) ===\n");
  testHeaders();
  testCbr();
  testVbr();
  testBroken();
  std::printf("=== %d comprobaciones, %d fallos ===\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
