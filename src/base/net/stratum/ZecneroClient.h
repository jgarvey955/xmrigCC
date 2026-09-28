/* Zecnero direct solo mining. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef XMRIG_ZECNERO_CLIENT_H
#define XMRIG_ZECNERO_CLIENT_H

#include "base/kernel/interfaces/IHttpListener.h"
#include "base/kernel/interfaces/ITimerListener.h"
#include "base/net/stratum/BaseClient.h"
#include "base/net/stratum/SubmitResult.h"
#include "base/tools/zecnero/BlockTemplate.h"
#include <deque>
#include <memory>

namespace xmrig {

class ZecneroClient : public BaseClient, public ITimerListener, public IHttpListener
{
public:
    XMRIG_DISABLE_COPY_MOVE_DEFAULT(ZecneroClient)
    ZecneroClient(int id, IClientListener *listener);
    ~ZecneroClient() override;

protected:
    bool disconnect() override;
    bool isTLS() const override { return m_pool.isTLS(); }
    int64_t submit(const JobResult &result) override;
    void connect() override;
    void connect(const Pool &pool) override { setPool(pool); connect(); }
    void onHttpData(const HttpData &data) override;
    void onTimer(const Timer *) override;
    bool hasExtension(Extension) const noexcept override { return false; }
    const char *mode() const override { return "zecnero-solo"; }
    const char *tlsFingerprint() const override { return m_tlsFingerprint; }
    const char *tlsVersion() const override { return m_tlsVersion; }
    int64_t send(const rapidjson::Value &, Callback) override { return -1; }
    int64_t send(const rapidjson::Value &) override { return -1; }
    void deleteLater() override { delete this; }
    void tick(uint64_t) override {}

private:
    struct Work { Job job; ZecneroBlockTemplate block; };
    void getBlockTemplate();
    bool sendRpc(int64_t id, const char *method, rapidjson::Value &params, rapidjson::Document &doc);
    bool authorization(std::string &value, std::string &error) const;
    void fail(const char *message);
    void clearRequests(const char *reason);

    Timer *m_timer;
    std::shared_ptr<IHttpListener> m_httpListener;
    std::deque<Work> m_work;
    int64_t m_templateRequest = 0;
    uint64_t m_jobTime = 0;
    bool m_waitingForSync = false;
    String m_tlsFingerprint;
    String m_tlsVersion;
};

} // namespace xmrig
#endif
