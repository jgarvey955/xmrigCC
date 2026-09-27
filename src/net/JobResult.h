/* XMRig
 * Copyright 2010      Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2012-2014 pooler      <pooler@litecoinpool.org>
 * Copyright 2014      Lucas Jones <https://github.com/lucasjones>
 * Copyright 2014-2016 Wolf9466    <https://github.com/OhGodAPet>
 * Copyright 2016      Jay D Dee   <jayddee246@gmail.com>
 * Copyright 2017-2018 XMR-Stak    <https://github.com/fireice-uk>, <https://github.com/psychocrypt>
 * Copyright 2018      Lee Clagett <https://github.com/vtnerd>
 * Copyright 2018-2020 SChernykh   <https://github.com/SChernykh>
 * Copyright 2016-2020 XMRig       <https://github.com/xmrig>, <support@xmrig.com>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef XMRIG_JOBRESULT_H
#define XMRIG_JOBRESULT_H


#include <cstring>
#include <cstdint>


#include "base/tools/String.h"
#include "base/net/stratum/Job.h"

#ifdef XMRIG_PROXY_PROJECT
#   include "base/tools/Cvt.h"
#endif


namespace xmrig {


class JobResult
{
public:
#ifdef XMRIG_PROXY_PROJECT
    static constexpr uint32_t backend = 0;

    JobResult() = default;

    inline JobResult(int64_t id, const char *jobId, const char *nonce, const char *result, const Algorithm &algorithm, const char *sig, const char *sig_data, const char *commitment, uint8_t view_tag, int64_t extra_nonce) :
        algorithm(algorithm),
        nonce(nonce),
        result(result),
        sig(sig),
        sig_data(sig_data),
        commitment(commitment),
        view_tag(view_tag),
        id(id),
        extra_nonce(extra_nonce),
        jobId(jobId)
    {
        if (result && strlen(result) == 64) {
            uint64_t target = 0;
            Cvt::fromHex(reinterpret_cast<uint8_t *>(&target), sizeof(target), result + 48, 16);

            if (target > 0) {
                m_actualDiff = Job::toDiff(target);
            }
        }
    }

    inline bool isCompatible(uint8_t fixedByte) const
    {
        uint8_t n[4];
        if (!Cvt::fromHex(n, sizeof(n), nonce, 8)) {
            return false;
        }

        return n[3] == fixedByte;
    }

    inline bool isValid() const
    {
        if (!nonce || m_actualDiff == 0) {
            return false;
        }

        return strlen(nonce) == 8 && !jobId.isNull();
    }

    inline uint64_t actualDiff() const { return m_actualDiff; }

    Algorithm algorithm;
    const char *nonce         = nullptr;
    const char *result        = nullptr;
    const char *sig           = nullptr;
    const char *sig_data      = nullptr;
    const char *commitment    = nullptr;
    const uint8_t view_tag    = 0;
    const int64_t id          = 0;
    const int64_t extra_nonce = -1;
    String jobId;
    uint64_t diff             = 0;

private:
    uint64_t m_actualDiff     = 0;
#else
    JobResult() = delete;

    inline JobResult(const Job &job, uint64_t nonce, const uint8_t *result, const uint8_t* header_hash = nullptr, const uint8_t *mix_hash = nullptr, const uint8_t* miner_signature = nullptr) :
        algorithm(job.algorithm()),
        index(job.index()),
        clientId(job.clientId()),
        jobId(job.id()),
        backend(job.backend()),
        nonce(nonce),
        diff(job.diff())
    {
        memcpy(m_result, result, sizeof(m_result));

        if (header_hash) {
            memcpy(m_headerHash, header_hash, sizeof(m_headerHash));
        }

        if (mix_hash) {
            memcpy(m_mixHash, mix_hash, sizeof(m_mixHash));
        }

        if (miner_signature) {
            m_hasMinerSignature = true;
            memcpy(m_minerSignature, miner_signature, sizeof(m_minerSignature));
        }
    }

    inline JobResult(const Job &job) :
        algorithm(job.algorithm()),
        index(job.index()),
        clientId(job.clientId()),
        jobId(job.id()),
        backend(job.backend()),
        nonce(0),
        diff(0)
    {
    }

    inline const uint8_t *result() const     { return m_result; }
    inline uint64_t actualDiff() const       { return Job::toDiff(reinterpret_cast<const uint64_t*>(m_result)[3]); }
    inline uint8_t *result()                 { return m_result; }
    inline const uint8_t *headerHash() const { return m_headerHash; }
    inline const uint8_t *mixHash() const    { return m_mixHash; }

    inline const uint8_t *minerSignature() const { return m_hasMinerSignature ? m_minerSignature : nullptr; }

    const Algorithm algorithm;
    const uint8_t index;
    const String clientId;
    const String jobId;
    const uint32_t backend;
    const uint64_t nonce;
    const uint64_t diff;

private:
    uint8_t m_result[32]     = { 0 };
    uint8_t m_headerHash[32] = { 0 };
    uint8_t m_mixHash[32]    = { 0 };

    uint8_t m_minerSignature[64] = { 0 };
    bool m_hasMinerSignature = false;
#endif
};


} /* namespace xmrig */


#endif /* XMRIG_JOBRESULT_H */
