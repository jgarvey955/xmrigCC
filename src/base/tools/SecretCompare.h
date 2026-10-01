/* XMRigCC. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef XMRIG_SECRET_COMPARE_H
#define XMRIG_SECRET_COMPARE_H
#include <cstddef>
#ifdef XMRIG_FEATURE_TLS
#include <openssl/crypto.h>
#endif
namespace xmrig {
inline bool secretEquals(const char *left, const char *right, size_t size)
{
#ifdef XMRIG_FEATURE_TLS
    return CRYPTO_memcmp(left, right, size) == 0;
#else
    volatile unsigned char difference = 0;
    for (size_t i = 0; i < size; ++i) {
        difference = static_cast<unsigned char>(difference | (left[i] ^ right[i]));
    }
    return difference == 0;
#endif
}
}
#endif
