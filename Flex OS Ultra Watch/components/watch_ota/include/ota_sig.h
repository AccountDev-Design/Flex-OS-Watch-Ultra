/*
 * ota_sig — SHA-256, MD5 y verificación ECDSA P-256 con mbedTLS. El mismo
 * código en el reloj y en las pruebas de PC (enlazadas con el mbedTLS de
 * ESP-IDF compilado para el PC).
 *
 * Firma: r||s de 64 B (ieee-p1363) sobre SHA-256 del mensaje canónico de
 * ota_sign_message(); clave pública SEC1 sin comprimir de 65 B, como
 * verifyP256 de FlexOS_PkgCore.cpp en el resto del ecosistema.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ota_hash ota_hash_t;

/** Reserva y empieza los resúmenes (md5 solo si with_md5). NULL sin memoria. */
ota_hash_t *ota_hash_new(bool with_md5);
void ota_hash_update(ota_hash_t *h, const uint8_t *d, size_t n);
/** Termina, copia los resúmenes (md5 puede ser NULL) y libera. */
void ota_hash_finish(ota_hash_t *h, uint8_t sha[32], uint8_t md5[16]);
void ota_hash_free(ota_hash_t *h);

void ota_sha256(const uint8_t *d, size_t n, uint8_t out[32]);

/** true si sig (64 B) es una firma válida de msg con pub (65 B). */
bool ota_p256_verify(const uint8_t pub[65], const uint8_t *msg, size_t n, const uint8_t sig[64]);

#ifdef __cplusplus
}
#endif
