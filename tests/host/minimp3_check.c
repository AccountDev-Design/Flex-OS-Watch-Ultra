/*
 * minimp3_check — decodifica un MP3 POR TROZOS, como el reproductor del reloj
 * (búfer de entrada acotado que se rellena con lecturas de 4 KB), y escribe el
 * PCM de 16 bits. Se compila dos veces: con el minimp3 del reloj
 * (MINIMP3_EXTERNAL_SCRATCH, área de trabajo en el heap) y con el original
 * (área en la pila). minimp3_vectors.sh comprueba que las dos salidas son
 * idénticas bit a bit en los vectores de conformidad ISO de minimp3 y mide la
 * relación señal/ruido contra el PCM de referencia.
 *
 *   minimp3_check entrada.mp3 salida.pcm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#define IN_CAP (16 * 1024)
#define RD 4096

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "uso: %s entrada.mp3 salida.pcm\n", argv[0]);
        return 2;
    }
    FILE *in = fopen(argv[1], "rb");
    FILE *out = fopen(argv[2], "wb");
    if (!in || !out) {
        perror("abrir");
        return 2;
    }
    static mp3dec_t dec;
    mp3dec_init(&dec);
#ifdef MINIMP3_EXTERNAL_SCRATCH
    dec.flex_scratch = malloc(mp3dec_scratch_bytes());
    if (!dec.flex_scratch) {
        return 2;
    }
#endif
    static uint8_t buf[IN_CAP];
    static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    size_t have = 0;
    int eof = 0;
    long frames = 0, samples = 0;
    int hz = 0, ch = 0;
    for (;;) {
        while (!eof && have + RD <= IN_CAP) {
            const size_t r = fread(buf + have, 1, RD, in);
            have += r;
            if (r < RD) {
                eof = 1;
            }
        }
        if (have == 0) {
            break;
        }
        mp3dec_frame_info_t info;
        const int n = mp3dec_decode_frame(&dec, buf, (int)have, pcm, &info);
        if (info.frame_bytes == 0) {
            if (eof) {
                break;
            }
            have = 0; /* nada decodificable en todo el búfer: se descarta */
            continue;
        }
        if (n > 0) {
            fwrite(pcm, sizeof(mp3d_sample_t), (size_t)n * info.channels, out);
            frames++;
            samples += n;
            hz = info.hz;
            ch = info.channels;
        }
        memmove(buf, buf + info.frame_bytes, have - (size_t)info.frame_bytes);
        have -= (size_t)info.frame_bytes;
    }
    printf("%ld tramas, %ld muestras por canal, %d Hz, %d canales\n", frames, samples, hz, ch);
#ifdef MINIMP3_EXTERNAL_SCRATCH
    free(dec.flex_scratch);
#endif
    fclose(in);
    fclose(out);
    return 0;
}
