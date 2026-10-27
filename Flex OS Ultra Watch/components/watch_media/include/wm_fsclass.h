/*
 * wm_fsclass — lógica PURA (se prueba en el PC): qué hay en una microSD que
 * no se pudo montar, para decírselo a la persona con precisión, y la
 * capacidad en 64 bits.
 *
 * Solo LEE sectores (el controlador se los pasa). Nunca escribe ni repara.
 *
 * Límites de ESP-IDF 5.5.5 (comprobados en sus fuentes, docs/MULTIMEDIA.md §3):
 *  - FatFs con FF_FS_EXFAT = 0 y FF_LBA64 = 0: FAT12/16/32 en MBR o sin
 *    tabla, direcciones de sector de 32 bits. Una SDXC de "2 TB" (C_SIZE
 *    máximo 0x3FFEFF -> 4 294 705 152 sectores) cabe en 32 bits: con FAT32
 *    se usa entera. exFAT, NTFS y GPT no se montan;
 *  - sdmmc_card_t.csd.capacity es `int`: por encima de 1 TB se lee como
 *    uint32_t (wm_card_bytes).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WFS_NONE = 0,  /**< sin firma 55AA ni sector de arranque reconocible */
    WFS_FAT,       /**< FAT12/16/32 (FatFs la monta) */
    WFS_EXFAT,
    WFS_NTFS,
    WFS_GPT,
    WFS_OTHER,     /**< partición de otro tipo (Linux, etc.) */
    WFS_PARTITION, /**< MBR: hay que mirar el sector de arranque de la partición (lba) */
} wfs_kind_t;

typedef struct {
    wfs_kind_t kind;
    uint32_t lba;     /**< WFS_PARTITION: primer sector de la partición */
    uint8_t ptype;    /**< tipo de la partición MBR (0 = sin tabla) */
    uint8_t fat_bits; /**< WFS_FAT: 12, 16 o 32 (0 = no se sabe) */
} wfs_class_t;

/** Clasifica un sector de 512 bytes: el 0 de la tarjeta o el primero de una partición. */
wfs_class_t wfs_classify(const uint8_t sec[512], bool is_partition);

/** Capacidad en bytes con el campo `int` de sdmmc (sectores) leído sin signo. */
uint64_t wm_card_bytes(int csd_capacity, int sector_size);

/** Tamaño del clúster (bytes) para formatear en FAT según la capacidad: el de
 * la SD Association para SDHC (32 KB) y 64 KB (máximo de FatFs en FAT32) para
 * SDXC; una FAT32 de 2 TB con 64 KB ocupa 2 x 117 MB de tablas. */
uint32_t wm_format_cluster(uint64_t card_bytes);

/** "29,7 GB", "512 MB" (base 1000, como las etiquetas de las tarjetas). */
void wm_fmt_bytes(uint64_t b, char *out, size_t n);

#ifdef __cplusplus
}
#endif
