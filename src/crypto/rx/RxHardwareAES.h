/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "crypto/randomx/aes_hash.hpp"
#include <vector>

namespace xmrig {
struct HardwareAES {
    const char *name;
    hashAndFillAes1Rx4_impl *function;
};

// Reference first; other entries require both CPU and OS instruction support.
std::vector<HardwareAES> hardwareAESImplementations();
bool verifyHardwareAES(const HardwareAES &implementation, size_t scratchpadSize);
HardwareAES selectedHardwareAES(size_t scratchpadSize);
// Enable the verified VAES-512 implementation on Zen 5; retain reference elsewhere.
void prepareHardwareAES(size_t scratchpadSize);
}
