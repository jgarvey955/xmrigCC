/* XMRig
 * Copyright (c) 2018-2025 SChernykh   <https://github.com/SChernykh>
 * Copyright (c) 2016-2025 XMRig       <https://github.com/xmrig>, <support@xmrig.com>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 */

#ifndef XMRIG_DISCORDNOTIFIER_H
#define XMRIG_DISCORDNOTIFIER_H


#include "3rdparty/rapidjson/fwd.h"
#include "base/kernel/interfaces/IHttpListener.h"
#include "base/tools/Object.h"
#include "base/tools/String.h"


#include <memory>
#include <string>


namespace xmrig {


class Config;
class IClient;
class NetworkState;
class SubmitResult;


class DiscordConfig
{
public:
    static const char *kField;

    bool isEnabled() const;
    bool read(const rapidjson::Value &value);
    rapidjson::Value toJSON(rapidjson::Document &doc) const;

    bool enabled              = false;
    bool notifyAccepted       = true;
    bool notifyRejected       = false;
    bool verbose              = false;
    bool includeWorker        = true;
    bool includeTotals        = true;
    bool quiet                = true;
    uint64_t acceptedInterval = 0;
    uint64_t minDiff          = 0;
    String webhook;
    String username;
    String avatarUrl;
    String mention;
};


class DiscordNotifier : public IHttpListener
{
public:
    XMRIG_DISABLE_COPY_MOVE_DEFAULT(DiscordNotifier)

    DiscordNotifier(Config *config, const NetworkState *state);
    ~DiscordNotifier() override;

    void notify(IClient *client, const SubmitResult &result, const char *error);
    void reset();
    void setConfig(Config *config);

protected:
    void onHttpData(const HttpData &data) override;

private:
    struct WebhookTarget
    {
        bool tls = true;
        uint16_t port = 443;
        String host;
        String path;
    };

    bool parseWebhook(WebhookTarget &target) const;
    const char *workerName(IClient *client) const;
    std::string acceptedMessage(IClient *client, const SubmitResult &result) const;
    std::string rejectedMessage(IClient *client, const SubmitResult &result, const char *error) const;
    std::string summaryMessage(IClient *client, uint64_t count, uint64_t seconds) const;
    void accept(IClient *client, const SubmitResult &result);
    void flushSummary(IClient *client, uint64_t now);
    void reject(IClient *client, const SubmitResult &result, const char *error);
    void send(const std::string &content) const;

    Config *m_config;
    std::shared_ptr<IHttpListener> m_httpListener;
    const NetworkState *m_state;
    uint64_t m_windowAccepted = 0;
    uint64_t m_windowDiff = 0;
    uint64_t m_windowStart = 0;
};


} /* namespace xmrig */


#endif /* XMRIG_DISCORDNOTIFIER_H */
