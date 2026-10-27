/*
 * wm_fsclass — ver include/wm_fsclass.h
 */
#include "wm_fsclass.h"

#include <stdio.h>
#include <string.h>

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool pow2(uint32_t x)
{
    return x && !(x & (x - 1));
}

/* BPB de FAT coherente (lo mismo que mira FatFs en check_fs). */
static bool fat_bpb(const uint8_t *s, uint8_t *bits)
{
    if (s[0] != 0xEB && s[0] != 0xE9 && s[0] != 0xE8) {
        return false;
    }
    const uint16_t bps = le16(s + 11);
    const uint8_t spc = s[13];
    const uint16_t rsv = le16(s + 14);
    const uint8_t nfat = s[16];
    if (bps < 512 || bps > 4096 || !pow2(bps) || !pow2(spc) || rsv == 0 || nfat < 1 || nfat > 2) {
        return false;
    }
    const uint16_t fat16sz = le16(s + 22);
    if (fat16sz == 0 && le32(s + 36) != 0 && !memcmp(s + 82, "FAT32", 5)) {
        *bits = 32;
        return true;
    }
    if (fat16sz != 0) {
        *bits = !memcmp(s + 54, "FAT12", 5) ? 12 : !memcmp(s + 54, "FAT16", 5) ? 16 : 0;
        return true;
    }
    *bits = 0;
    return true;
}

wfs_class_t wfs_classify(const uint8_t sec[512], bool is_partition)
{
    wfs_class_t c = {0};
    if (!memcmp(sec + 3, "EXFAT   ", 8)) {
        c.kind = WFS_EXFAT;
        return c;
    }
    if (!memcmp(sec + 3, "NTFS    ", 8)) {
        c.kind = WFS_NTFS;
        return c;
    }
    if (sec[510] != 0x55 || sec[511] != 0xAA) {
        c.kind = WFS_NONE;
        return c;
    }
    if (fat_bpb(sec, &c.fat_bits)) {
        c.kind = WFS_FAT; /* sin tabla ("superfloppy") o la partición */
        return c;
    }
    if (is_partition) {
        c.kind = WFS_OTHER;
        return c;
    }
    /* MBR: la primera partición con tipo. */
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = sec + 446 + 16 * i;
        const uint8_t t = e[4];
        if (t == 0) {
            continue;
        }
        if (t == 0xEE) {
            c.kind = WFS_GPT;
            return c;
        }
        c.ptype = t;
        c.lba = le32(e + 8);
        if (c.lba == 0 || le32(e + 12) == 0) {
            continue; /* entrada vacía o rota */
        }
        switch (t) {
        case 0x01: case 0x04: case 0x06: case 0x0B: case 0x0C: case 0x0E: /* FAT */
        case 0x07: /* exFAT o NTFS: lo dice su sector de arranque */
            c.kind = WFS_PARTITION;
            return c;
        default:
            c.kind = WFS_OTHER;
            return c;
        }
    }
    c.kind = WFS_NONE;
    c.ptype = 0;
    c.lba = 0;
    return c;
}

uint64_t wm_card_bytes(int csd_capacity, int sector_size)
{
    return (uint64_t)(uint32_t)csd_capacity * (uint64_t)(uint32_t)(sector_size > 0 ? sector_size : 512);
}

uint32_t wm_format_cluster(uint64_t card_bytes)
{
    if (card_bytes <= 32ull * 1000 * 1000 * 1000) {
        return 32u * 1024u; /* SDHC (y SDSC): 32 KB */
    }
    return 64u * 1024u;     /* SDXC: el máximo de FatFs en FAT32 */
}

void wm_fmt_bytes(uint64_t b, char *out, size_t n)
{
    static const char *const unit[] = {"B", "KB", "MB", "GB", "TB"};
    int u = 0;
    uint64_t whole = b, frac = 0;
    while (whole >= 1000 && u < 4) {
        frac = whole % 1000;
        whole /= 1000;
        u++;
    }
    if (u == 0 || whole >= 100) {
        snprintf(out, n, "%llu %s", (unsigned long long)whole, unit[u]);
    } else {
        snprintf(out, n, "%llu,%llu %s", (unsigned long long)whole, (unsigned long long)(frac / 100), unit[u]);
    }
}
