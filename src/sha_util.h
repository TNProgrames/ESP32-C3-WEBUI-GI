// SHA-256 بنداء واحد، يعمل على mbedtls 2.x (IDF 4) و3.x (IDF 5) و4.x / PSA Crypto (IDF 6)
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <mbedtls/version.h>
#if defined(MBEDTLS_VERSION_MAJOR) && MBEDTLS_VERSION_MAJOR >= 4
#include <psa/crypto.h>
#else
#include <mbedtls/sha256.h>
#endif

static inline bool sha256_once(const uint8_t* data, size_t len, uint8_t out[32]) {
#if defined(MBEDTLS_VERSION_MAJOR) && MBEDTLS_VERSION_MAJOR >= 4
  if (psa_crypto_init() != PSA_SUCCESS) return false;
  size_t hlen = 0;
  return psa_hash_compute(PSA_ALG_SHA_256, data, len, out, 32, &hlen) == PSA_SUCCESS && hlen == 32;
#elif defined(MBEDTLS_VERSION_MAJOR) && MBEDTLS_VERSION_MAJOR >= 3
  return mbedtls_sha256(data, len, out, 0) == 0;
#else
  return mbedtls_sha256_ret(data, len, out, 0) == 0;
#endif
}
