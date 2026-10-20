/*
 * ota_sig — ver include/ota_sig.h.
 */
#include "ota_sig.h"

#include <stdlib.h>
#include <string.h>
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/md5.h"
#include "mbedtls/sha256.h"

struct ota_hash {
    mbedtls_sha256_context sha;
    mbedtls_md5_context md5;
    bool with_md5;
};

ota_hash_t *ota_hash_new(bool with_md5)
{
    ota_hash_t *h = calloc(1, sizeof(*h));
    if (!h) {
        return NULL;
    }
    h->with_md5 = with_md5;
    mbedtls_sha256_init(&h->sha);
    mbedtls_sha256_starts(&h->sha, 0);
    if (with_md5) {
        mbedtls_md5_init(&h->md5);
        mbedtls_md5_starts(&h->md5);
    }
    return h;
}

void ota_hash_update(ota_hash_t *h, const uint8_t *d, size_t n)
{
    mbedtls_sha256_update(&h->sha, d, n);
    if (h->with_md5) {
        mbedtls_md5_update(&h->md5, d, n);
    }
}

void ota_hash_free(ota_hash_t *h)
{
    if (!h) {
        return;
    }
    mbedtls_sha256_free(&h->sha);
    if (h->with_md5) {
        mbedtls_md5_free(&h->md5);
    }
    free(h);
}

void ota_hash_finish(ota_hash_t *h, uint8_t sha[32], uint8_t md5[16])
{
    mbedtls_sha256_finish(&h->sha, sha);
    if (h->with_md5 && md5) {
        mbedtls_md5_finish(&h->md5, md5);
    }
    ota_hash_free(h);
}

void ota_sha256(const uint8_t *d, size_t n, uint8_t out[32])
{
    mbedtls_sha256(d, n, out, 0);
}

/* verifyP256 de FlexOS_PkgCore.cpp: clave SEC1 de 65 B, firma r||s. */
bool ota_p256_verify(const uint8_t pub[65], const uint8_t *msg, size_t n, const uint8_t sig[64])
{
    uint8_t hash[32];
    ota_sha256(msg, n, hash);
    mbedtls_ecp_group grp;
    mbedtls_ecp_point q;
    mbedtls_mpi r, s;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&q);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    int rc = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
    if (rc == 0) {
        rc = mbedtls_ecp_point_read_binary(&grp, &q, pub, 65);
    }
    if (rc == 0) {
        rc = mbedtls_ecp_check_pubkey(&grp, &q);
    }
    if (rc == 0) {
        rc = mbedtls_mpi_read_binary(&r, sig, 32);
    }
    if (rc == 0) {
        rc = mbedtls_mpi_read_binary(&s, sig + 32, 32);
    }
    if (rc == 0) {
        rc = mbedtls_ecdsa_verify(&grp, hash, sizeof(hash), &q, &r, &s);
    }
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&q);
    mbedtls_ecp_group_free(&grp);
    return rc == 0;
}
