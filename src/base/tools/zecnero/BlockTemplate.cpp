/* Zecnero direct solo mining. SPDX-License-Identifier: GPL-3.0-or-later */
#include "base/tools/zecnero/BlockTemplate.h"
#include "3rdparty/rapidjson/document.h"

#include <algorithm>
#include <cstring>

namespace {
const rapidjson::Value &field(const rapidjson::Value &value, const char *name)
{
    static const rapidjson::Value missing;
    if (!value.IsObject()) { return missing; }
    const auto i = value.FindMember(name);
    return i == value.MemberEnd() ? missing : i->value;
}

int nibble(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

bool hex(const rapidjson::Value &value, uint8_t *out, size_t size, bool reverse = false)
{
    if (!value.IsString() || value.GetStringLength() != size * 2) { return false; }
    const char *s = value.GetString();
    for (size_t i = 0; i < size; ++i) {
        const int a = nibble(s[i * 2]), b = nibble(s[i * 2 + 1]);
        if (a < 0 || b < 0) { return false; }
        out[reverse ? size - i - 1 : i] = static_cast<uint8_t>((a << 4) | b);
    }
    return true;
}

void le32(uint8_t *out, uint32_t n)
{
    for (unsigned i = 0; i < 4; ++i) { out[i] = static_cast<uint8_t>(n >> (8 * i)); }
}

void compactSize(std::vector<uint8_t> &out, uint32_t n)
{
    if (n < 253) { out.push_back(static_cast<uint8_t>(n)); return; }
    out.push_back(n <= 65535 ? 253 : 254);
    const unsigned bytes = n <= 65535 ? 2 : 4;
    for (unsigned i = 0; i < bytes; ++i) { out.push_back(static_cast<uint8_t>(n >> (8 * i))); }
}

bool expandTarget(uint32_t bits, std::array<uint8_t, 32> &out)
{
    const unsigned exponent = bits >> 24;
    uint32_t mantissa = bits & 0x007fffff;
    if (!mantissa || (bits & 0x00800000) || exponent > 34) { return false; }
    if (exponent <= 3) { mantissa >>= 8 * (3 - exponent); }
    const unsigned shift = exponent > 3 ? exponent - 3 : 0;
    for (unsigned i = 0; i < 3; ++i) {
        const auto byte = static_cast<uint8_t>(mantissa >> (8 * i));
        if (shift + i >= 32) { if (byte) { return false; } }
        else { out[31 - shift - i] = byte; }
    }
    return std::any_of(out.begin(), out.end(), [](uint8_t b) { return b != 0; });
}
} // namespace

bool xmrig::ZecneroBlockTemplate::parse(const rapidjson::Value &value, std::string &error)
{
    ZecneroBlockTemplate next;
    const auto fail = [&error](const char *message) { error = message; return false; };
    if (!value.IsObject()) { return fail("getblocktemplate result must be an object"); }
    const auto &pow = field(value, "powversion");
    if (!pow.IsNull() && (!pow.IsUint() || (pow.GetUint() != 1 && pow.GetUint() != 2))) {
        return fail("unsupported Zecnero PoW version; this miner supports RandomX v1 and v2");
    }
    next.powVersion = pow.IsNull() ? 1 : pow.GetUint();
    const auto &algo = field(value, "algo");
    if (!algo.IsNull() && (!algo.IsString() || std::string(algo.GetString(), algo.GetStringLength()) != (next.powVersion == 2 ? "rx/zecnero2" : "rx/zecnero"))) {
        return fail("unsupported Zecnero algorithm");
    }
    const auto &rules = field(value, "rules");
    if (!rules.IsNull()) {
        if (!rules.IsArray()) { return fail("invalid template rules"); }
        for (const auto &rule : rules.GetArray()) {
            if (!rule.IsString() || (rule.GetStringLength() && rule.GetString()[0] == '!')) {
                return fail("unsupported mandatory template rule");
            }
        }
    }
    if (!field(value, "version").IsUint() || field(value, "version").GetUint() != 4) {
        return fail("unsupported Zecnero block header version");
    }
    le32(next.header.data(), 4);
    if (!field(value, "height").IsUint() || field(value, "height").GetUint() == 0) {
        return fail("invalid template height");
    }
    next.height = field(value, "height").GetUint();
    const uint32_t seedHeight = next.height <= 2112 ? 0 : ((next.height - 65) / 2048) * 2048;
    if (!field(value, "seedheight").IsUint() || field(value, "seedheight").GetUint() != seedHeight ||
        !hex(field(value, "seedhash"), next.seed.data(), 32)) {
        return fail("invalid Zecnero seedhash or seedheight");
    }
    if (!hex(field(value, "previousblockhash"), next.header.data() + 4, 32, true) ||
        !hex(field(field(value, "defaultroots"), "merkleroot"), next.header.data() + 36, 32, true) ||
        !hex(field(value, "blockcommitmentshash"), next.header.data() + 68, 32, true)) {
        return fail("invalid previous hash or default block roots");
    }
    const auto &nestedCommitment = field(field(value, "defaultroots"), "blockcommitmentshash");
    if (!nestedCommitment.IsNull()) {
        std::array<uint8_t, 32> commitment{};
        if (!hex(nestedCommitment, commitment.data(), 32, true) ||
            !std::equal(commitment.begin(), commitment.end(), next.header.begin() + 68)) {
            return fail("inconsistent block commitments");
        }
    }
    for (const char *name : {"curtime", "mintime", "maxtime"}) {
        if (!field(value, name).IsUint()) { return fail("invalid template time"); }
    }
    const uint32_t time = field(value, "curtime").GetUint();
    if (time < field(value, "mintime").GetUint() || time > field(value, "maxtime").GetUint()) {
        return fail("template time is out of range");
    }
    le32(next.header.data() + 100, time);
    if (!hex(field(value, "bits"), next.header.data() + 104, 4, true) ||
        !hex(field(value, "target"), next.target.data(), 32)) {
        return fail("invalid bits or target");
    }
    uint32_t bits = 0;
    for (unsigned i = 0; i < 4; ++i) { bits |= uint32_t(next.header[104 + i]) << (8 * i); }
    std::array<uint8_t, 32> expanded{};
    if (!expandTarget(bits, expanded) || expanded != next.target) {
        return fail("target does not match compact bits");
    }
    const auto &nonceRange = field(value, "noncerange");
    if (!nonceRange.IsNull() && (!nonceRange.IsString() ||
        std::string(nonceRange.GetString(), nonceRange.GetStringLength()) != "00000000ffffffff")) {
        return fail("unsupported nonce range");
    }
    if (!field(value, "sizelimit").IsUint() || field(value, "sizelimit").GetUint() <= kHeaderSize + 1) {
        return fail("invalid block size limit");
    }
    const size_t limit = std::min<size_t>(field(value, "sizelimit").GetUint(), kMaxBlockSize) - kHeaderSize - 1;
    const auto &transactions = field(value, "transactions");
    if (!transactions.IsArray() || transactions.Size() >= limit) { return fail("invalid transactions"); }
    compactSize(next.m_transactions, transactions.Size() + 1);
    const auto append = [&next, limit](const rapidjson::Value &tx) {
        const auto &data = field(tx, "data");
        if (!data.IsString() || !data.GetStringLength() || data.GetStringLength() % 2) { return false; }
        const size_t size = data.GetStringLength() / 2, offset = next.m_transactions.size();
        if (offset > limit || size > limit - offset) { return false; }
        next.m_transactions.resize(offset + size);
        return hex(data, next.m_transactions.data() + offset, size);
    };
    if (!append(field(value, "coinbasetxn"))) { return fail("invalid or oversized coinbase transaction"); }
    for (const auto &tx : transactions.GetArray()) {
        if (!append(tx)) { return fail("invalid or oversized transaction"); }
    }
    *this = std::move(next);
    error.clear();
    return true;
}

void xmrig::ZecneroBlockTemplate::setExtraNonce(const uint8_t *bytes)
{
    std::copy(bytes, bytes + 28, header.begin() + kNonceOffset + 4);
}

std::vector<uint8_t> xmrig::ZecneroBlockTemplate::block(uint32_t nonce) const
{
    std::vector<uint8_t> out(header.begin(), header.end());
    le32(out.data() + kNonceOffset, nonce);
    out.push_back(0); // CompactSize(0): Zecnero's empty Equihash solution
    out.insert(out.end(), m_transactions.begin(), m_transactions.end());
    return out;
}

bool xmrig::ZecneroBlockTemplate::sameWork(const ZecneroBlockTemplate &other) const
{
    return powVersion == other.powVersion && height == other.height && seed == other.seed && target == other.target &&
        std::equal(header.begin(), header.begin() + kNonceOffset, other.header.begin()) &&
        m_transactions == other.m_transactions;
}
