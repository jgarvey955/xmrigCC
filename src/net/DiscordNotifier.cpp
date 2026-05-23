/* XMRig
 * Copyright (c) 2018-2025 SChernykh   <https://github.com/SChernykh>
 * Copyright (c) 2016-2025 XMRig       <https://github.com/xmrig>, <support@xmrig.com>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 */

#include "net/DiscordNotifier.h"
#include "3rdparty/rapidjson/document.h"
#include "base/io/json/Json.h"
#include "base/io/log/Log.h"
#include "base/io/log/Tags.h"
#include "base/kernel/interfaces/IClient.h"
#include "base/net/http/Fetch.h"
#include "base/net/http/HttpData.h"
#include "base/net/stratum/NetworkState.h"
#include "base/net/stratum/Pool.h"
#include "base/net/stratum/SubmitResult.h"
#include "base/tools/Chrono.h"
#include "core/config/Config.h"

#include "3rdparty/rapidjson/stringbuffer.h"
#include "3rdparty/rapidjson/writer.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>


namespace xmrig {


const char *DiscordConfig::kField = "discord";



static void appendLine(std::string &out, const char *key, const char *value)
{
    if (value && strlen(value) > 0) {
        out += "\n";
        out += key;
        out += ": ";
        out += value;
    }
}


} // namespace xmrig


bool xmrig::DiscordConfig::isEnabled() const
{
    return enabled && !webhook.isEmpty();
}


bool xmrig::DiscordConfig::read(const rapidjson::Value &value)
{
    if (!value.IsObject()) {
        return true;
    }

    enabled          = Json::getBool(value, "enabled", enabled);
    notifyAccepted   = Json::getBool(value, "notify-accepted", notifyAccepted);
    notifyRejected   = Json::getBool(value, "notify-rejected", notifyRejected);
    verbose          = Json::getBool(value, "verbose", verbose);
    includeWorker    = Json::getBool(value, "include-worker", includeWorker);
    includeTotals    = Json::getBool(value, "include-totals", includeTotals);
    quiet            = Json::getBool(value, "quiet", quiet);
    acceptedInterval = Json::getUint64(value, "accepted-interval", acceptedInterval);
    minDiff          = Json::getUint64(value, "min-diff", minDiff);
    webhook          = Json::getString(value, "webhook");
    username         = Json::getString(value, "username");
    avatarUrl        = Json::getString(value, "avatar-url");
    mention          = Json::getString(value, "mention");

    return true;
}


rapidjson::Value xmrig::DiscordConfig::toJSON(rapidjson::Document &doc) const
{
    using namespace rapidjson;
    auto &allocator = doc.GetAllocator();

    Value out(kObjectType);
    out.AddMember("enabled", enabled, allocator);
    out.AddMember("webhook", webhook.toJSON(), allocator);
    out.AddMember("notify-accepted", notifyAccepted, allocator);
    out.AddMember("accepted-interval", acceptedInterval, allocator);
    out.AddMember("notify-rejected", notifyRejected, allocator);
    out.AddMember("verbose", verbose, allocator);
    out.AddMember("include-worker", includeWorker, allocator);
    out.AddMember("include-totals", includeTotals, allocator);
    out.AddMember("min-diff", minDiff, allocator);
    out.AddMember("username", username.toJSON(), allocator);
    out.AddMember("avatar-url", avatarUrl.toJSON(), allocator);
    out.AddMember("mention", mention.toJSON(), allocator);
    out.AddMember("quiet", quiet, allocator);

    return out;
}


xmrig::DiscordNotifier::DiscordNotifier(Config *config, const NetworkState *state) :
    m_config(config),
    m_state(state)
{
    m_httpListener = std::shared_ptr<IHttpListener>(this, [](IHttpListener *) {});
}


xmrig::DiscordNotifier::~DiscordNotifier() = default;


void xmrig::DiscordNotifier::notify(IClient *client, const SubmitResult &result, const char *error)
{
    if (error) {
        reject(client, result, error);
    }
    else {
        accept(client, result);
    }
}


void xmrig::DiscordNotifier::reset()
{
    m_windowAccepted = 0;
    m_windowDiff     = 0;
    m_windowStart    = 0;
}


void xmrig::DiscordNotifier::setConfig(Config *config)
{
    m_config = config;
}


void xmrig::DiscordNotifier::onHttpData(const HttpData &data)
{
    if (!m_config->discord().quiet && data.status >= 400) {
        LOG_WARN("%s Discord webhook returned HTTP %d", Tags::network(), data.status);
    }
}


bool xmrig::DiscordNotifier::parseWebhook(WebhookTarget &target) const
{
    const DiscordConfig &config = m_config->discord();
    const char *url = config.webhook.data();

    if (!url) {
        return false;
    }

    const char *base = nullptr;
    if (strncmp(url, "https://", 8) == 0) {
        target.tls  = true;
        target.port = 443;
        base = url + 8;
    }
    else if (strncmp(url, "http://", 7) == 0) {
        target.tls  = false;
        target.port = 80;
        base = url + 7;
    }
    else {
        return false;
    }

    const char *path = strchr(base, '/');
    if (!path || path == base) {
        return false;
    }

    const char *port = static_cast<const char *>(memchr(base, ':', static_cast<size_t>(path - base)));
    if (port) {
        target.host = String(base, static_cast<size_t>(port - base));
        target.port = static_cast<uint16_t>(strtol(port + 1, nullptr, 10));
    }
    else {
        target.host = String(base, static_cast<size_t>(path - base));
    }

    target.path = path;

    return !target.host.isEmpty() && !target.path.isEmpty() && target.port > 0;
}


const char *xmrig::DiscordNotifier::workerName(IClient *client) const
{
    if (!client) {
        return nullptr;
    }

    const Pool &pool = client->pool();
    if (!pool.rigId().isEmpty()) {
        return pool.rigId().data();
    }

    return pool.user().data();
}


std::string xmrig::DiscordNotifier::acceptedMessage(IClient *client, const SubmitResult &result) const
{
    const DiscordConfig &config = m_config->discord();
    char line[512] = { 0 };
    snprintf(line, sizeof(line) - 1, "Accepted block/share: diff %" PRIu64 ", actual diff %" PRIu64 ", elapsed %" PRIu64 " ms",
             result.diff, result.actualDiff, result.elapsed);

    std::string out = line;
    if (!config.mention.isEmpty()) {
        out = std::string(config.mention.data()) + " " + out;
    }

    if (config.includeWorker) {
        appendLine(out, "Worker", workerName(client));
    }

    if (config.verbose && client) {
        appendLine(out, "Pool", client->pool().printableName().c_str());
        appendLine(out, "Pool IP", client->ip().data());
        appendLine(out, "User", client->pool().user().data());
        appendLine(out, "Rig ID", client->pool().rigId().data());
    }

    if (config.includeTotals && m_state) {
        snprintf(line, sizeof(line) - 1, "\nTotals: accepted %" PRIu64 ", rejected %" PRIu64,
                 m_state->accepted(), m_state->rejected());
        out += line;
    }

    return out;
}


std::string xmrig::DiscordNotifier::rejectedMessage(IClient *client, const SubmitResult &result, const char *error) const
{
    char line[512] = { 0 };
    snprintf(line, sizeof(line) - 1, "Rejected block/share: diff %" PRIu64 ", elapsed %" PRIu64 " ms, error: %s",
             result.diff, result.elapsed, error ? error : "unknown");

    std::string out = line;
    if (m_config->discord().includeWorker) {
        appendLine(out, "Worker", workerName(client));
    }

    return out;
}


std::string xmrig::DiscordNotifier::summaryMessage(IClient *client, uint64_t count, uint64_t seconds) const
{
    const DiscordConfig &config = m_config->discord();
    char line[512] = { 0 };
    snprintf(line, sizeof(line) - 1, "%" PRIu64 " accepted block/share event%s in %" PRIu64 " seconds. Total diff %" PRIu64,
             count, count == 1 ? "" : "s", seconds, m_windowDiff);

    std::string out = line;
    if (!config.mention.isEmpty()) {
        out = std::string(config.mention.data()) + " " + out;
    }

    if (config.includeWorker) {
        appendLine(out, "Last worker", workerName(client));
    }

    if (config.includeTotals && m_state) {
        snprintf(line, sizeof(line) - 1, "\nTotals: accepted %" PRIu64 ", rejected %" PRIu64,
                 m_state->accepted(), m_state->rejected());
        out += line;
    }

    return out;
}


void xmrig::DiscordNotifier::accept(IClient *client, const SubmitResult &result)
{
    const DiscordConfig &config = m_config->discord();
    if (!config.isEnabled() || !config.notifyAccepted || result.diff < config.minDiff) {
        return;
    }

    if (config.acceptedInterval == 0) {
        send(acceptedMessage(client, result));
        return;
    }

    const uint64_t now = Chrono::steadyMSecs() / 1000;
    if (m_windowStart == 0) {
        m_windowStart = now;
    }

    m_windowAccepted++;
    m_windowDiff += result.diff;

    if (now - m_windowStart >= config.acceptedInterval) {
        flushSummary(client, now);
    }
}


void xmrig::DiscordNotifier::flushSummary(IClient *client, uint64_t now)
{
    if (m_windowAccepted > 0) {
        send(summaryMessage(client, m_windowAccepted, now - m_windowStart));
    }

    m_windowAccepted = 0;
    m_windowDiff     = 0;
    m_windowStart    = now;
}


void xmrig::DiscordNotifier::reject(IClient *client, const SubmitResult &result, const char *error)
{
    const DiscordConfig &config = m_config->discord();
    if (!config.isEnabled() || !config.notifyRejected) {
        return;
    }

    send(rejectedMessage(client, result, error));
}


void xmrig::DiscordNotifier::send(const std::string &content) const
{
    WebhookTarget target;
    if (!parseWebhook(target)) {
        if (!m_config->discord().quiet) {
            LOG_WARN("%s invalid Discord webhook URL", Tags::network());
        }

        return;
    }

    const DiscordConfig &config = m_config->discord();

    rapidjson::Document doc;
    doc.SetObject();
    auto &allocator = doc.GetAllocator();

    doc.AddMember("content", rapidjson::Value(content.c_str(), allocator), allocator);

    if (!config.username.isEmpty()) {
        doc.AddMember("username", rapidjson::Value(config.username.data(), allocator), allocator);
    }

    if (!config.avatarUrl.isEmpty()) {
        doc.AddMember("avatar_url", rapidjson::Value(config.avatarUrl.data(), allocator), allocator);
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    std::string body = buffer.GetString();

    FetchRequest req(HTTP_POST, target.host, target.port, target.path, target.tls, config.quiet, body.c_str(), body.size(), HttpData::kApplicationJson.c_str());
    req.timeout = 10000;

    fetch(Tags::network(), std::move(req), m_httpListener);
}
