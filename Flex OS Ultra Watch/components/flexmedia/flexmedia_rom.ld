/*
 * TJpgDec de la ROM del ESP32-S3 (R0.01, con escala) con otro nombre para
 * flex_jpeg_tjpgd.c: LVGL enlaza su propio TJpgDec como jd_prepare/jd_decomp.
 * Direcciones de components/esp_rom/esp32s3/ld/esp32s3.rom.ld de ESP-IDF
 * 5.5.5 (las mismas que watch_gallery/gal_rom.ld).
 */
PROVIDE ( flexmedia_rom_jd_prepare = 0x40000858 );
PROVIDE ( flexmedia_rom_jd_decomp = 0x40000864 );
