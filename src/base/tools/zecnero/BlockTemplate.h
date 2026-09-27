/* Zecnero direct solo mining. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef XMRIG_ZECNERO_BLOCK_TEMPLATE_H
#define XMRIG_ZECNERO_BLOCK_TEMPLATE_H

#include "3rdparty/rapidjson/fwd.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace xmrig {

// The node owns transaction selection, coinbase outputs, and commitments.
// Miners may only change the 32-byte header nonce.
class ZecneroBlockTemplate
{
public:
    static constexpr size_t kHeaderSize = 140;
    static constexpr size_t kNonceOffset = 108;
    static constexpr size_t kMaxBlockSize = 2000000;

    bool parse(const rapidjson::Value &value, std::string &error);
    void setExtraNonce(const uint8_t *bytes); // 28 bytes after the worker's 4-byte nonce
    std::vector<uint8_t> block(uint32_t nonce) const;
    bool sameWork(const ZecneroBlockTemplate &other) const;

    std::array<uint8_t, kHeaderSize> header{};
    std::array<uint8_t, 32> seed{}; // RPC seedhash is already in internal byte order
    std::array<uint8_t, 32> target{}; // big-endian RPC target
    uint32_t height = 0;
    uint32_t powVersion = 1;

private:
    std::vector<uint8_t> m_transactions; // CompactSize count followed by the unmodified txs
};

} // namespace xmrig
#endif
