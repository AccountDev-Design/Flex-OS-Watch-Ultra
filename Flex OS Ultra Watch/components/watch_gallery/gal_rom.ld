/*
 * TJpgDec de la ROM del ESP32-S3 (R0.01, con escala 1/2, 1/4 y 1/8) con otro
 * nombre: LVGL enlaza su propio TJpgDec (sin escala) como jd_prepare/jd_decomp,
 * y esos nombres tapan los PROVIDE de la ROM. Direcciones de
 * components/esp_rom/esp32s3/ld/esp32s3.rom.ld de ESP-IDF 5.5.5.
 */
PROVIDE ( gal_rom_jd_prepare = 0x40000858 );
PROVIDE ( gal_rom_jd_decomp = 0x40000864 );
