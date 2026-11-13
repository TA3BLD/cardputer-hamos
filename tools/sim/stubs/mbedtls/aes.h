// mbedtls AES calls mapped onto OpenSSL for the simulator.
#pragma once
#include <openssl/aes.h>
#define MBEDTLS_AES_ENCRYPT 1
struct mbedtls_aes_context { AES_KEY key; };
inline void mbedtls_aes_init(mbedtls_aes_context*) {}
inline void mbedtls_aes_free(mbedtls_aes_context*) {}
inline int mbedtls_aes_setkey_enc(mbedtls_aes_context* c, const unsigned char* k, unsigned bits) { return AES_set_encrypt_key(k, (int)bits, &c->key); }
inline int mbedtls_aes_crypt_ecb(mbedtls_aes_context* c, int, const unsigned char in[16], unsigned char out[16]) { AES_encrypt(in, out, &c->key); return 0; }
