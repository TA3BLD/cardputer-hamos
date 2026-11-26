#pragma once
#include <openssl/sha.h>
typedef SHA256_CTX mbedtls_sha256_context;
inline void mbedtls_sha256_init(mbedtls_sha256_context* c) { SHA256_Init(c); }
inline int mbedtls_sha256_starts(mbedtls_sha256_context* c, int) { SHA256_Init(c); return 0; }
inline int mbedtls_sha256_update(mbedtls_sha256_context* c, const unsigned char* d, size_t n) { SHA256_Update(c, d, n); return 0; }
inline int mbedtls_sha256_finish(mbedtls_sha256_context* c, unsigned char out[32]) { SHA256_Final(out, c); return 0; }
inline void mbedtls_sha256_free(mbedtls_sha256_context*) {}
