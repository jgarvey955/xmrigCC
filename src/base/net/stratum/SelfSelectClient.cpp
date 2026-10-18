/* XMRig
 * Copyright (c) 2019       jtgrassie       <https://github.com/jtgrassie>
 * Copyright (c) 2021       Hansie Odendaal <https://github.com/hansieodendaal>
 * Copyright (c) 2018-2021  SChernykh       <https://github.com/SChernykh>
 * Copyright (c) 2016-2021  XMRig           <https://github.com/xmrig>, <support@xmrig.com>
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


#include "base/net/stratum/SelfSelectClient.h"
#include "3rdparty/rapidjson/document.h"
#include "3rdparty/rapidjson/error/en.h"
#include "base/io/json/Json.h"
#include "base/io/json/JsonRequest.h"
#include "base/io/log/Log.h"
#include "base/io/log/Tags.h"
#include "base/net/http/Fetch.h"
#include "base/net/http/HttpData.h"
#include "base/net/stratum/Client.h"
#include "net/JobResult.h"
#include "base/tools/Cvt.h"
#include <cstring>


namespace xmrig {

static const char *kBlob                = "blob";
static const char *kBlockhashingBlob    = "blockhashing_blob";
static const char *kBlocktemplateBlob   = "blocktemplate_blob";
static const char *kDifficulty          = "difficulty";
static const char *kHeight              = "height";
static const char *kId                  = "id";
static const char *kJobId               = "job_id";
static const char *kNextSeedHash        = "next_seed_hash";
static const char *kPrevHash            = "prev_hash";
static const char *kSeedHash            = "seed_hash";

static const char * const required_fields[] = { kBlocktemplateBlob, kBlockhashingBlob, kHeight, kDifficulty, kPrevHash };

} /* namespace xmrig */


xmrig::SelfSelectClient::SelfSelectClient(int id, const char *agent, IClientListener *listener, bool submitToOrigin) :
    m_submitToOrigin(submitToOrigin),
    m_listener(listener)
{
    m_httpListener  = std::make_shared<HttpListener>(this);
    m_client        = new Client(id, agent, this);
}


xmrig::SelfSelectClient::~SelfSelectClient()
{
    delete m_client;
}


int64_t xmrig::SelfSelectClient::submit(const JobResult &result)
{
    if (m_submitToOrigin) {
        submitOriginDaemon(result);
    }

    const int64_t submit_result = m_client->submit(result);

    if (m_submitToOrigin) {
        // Ensure that the latest block template is available after block submission
        getBlockTemplate();
    }

    return submit_result;
}


bool xmrig::SelfSelectClient::disconnect()
{
    m_templateRequest = 0;
    failOriginSubmissions("origin daemon disconnected; block acceptance unknown");
    return m_client->disconnect();
}


void xmrig::SelfSelectClient::onClose(IClient *, int failures)
{
    m_templateRequest = 0;
    failOriginSubmissions("pool connection lost; origin block acceptance unknown");
    m_listener->onClose(this, failures);
    setState(IdleState);
    m_active = false;
}


void xmrig::SelfSelectClient::tick(uint64_t now)
{
    m_client->tick(now);

    if (m_state == RetryState) {
        if (Chrono::steadyMSecs() - m_timestamp < m_retryPause) {
            return;
        }

        getBlockTemplate();
    }
}


void xmrig::SelfSelectClient::onJobReceived(IClient *, const Job &job, const rapidjson::Value &)
{
    m_job = job;

    getBlockTemplate();
}


void xmrig::SelfSelectClient::onLogin(IClient *, rapidjson::Document &doc, rapidjson::Value &params)
{
    params.AddMember("mode", "self-select", doc.GetAllocator());

    m_listener->onLogin(this, doc, params);
}


bool xmrig::SelfSelectClient::parseTemplateResponse(int64_t id, rapidjson::Value &result, const rapidjson::Value &error)
{
    if (id == -1) {
        return false;
    }

    if (error.IsObject()) {
        LOG_ERR("[%s] error: " RED_BOLD("\"%s\"") RED_S ", code: %d", pool().daemon().url().data(), Json::getString(error, "message"), Json::getInt(error, "code"));

        return false;
    }

    if (!result.IsObject()) {
        return false;
    }

    for (auto field : required_fields) {
        if (!result.HasMember(field)) {
            LOG_ERR("[%s] required field " RED_BOLD("\"%s\"") RED_S " not found", pool().daemon().url().data(), field);

            return false;
        }
    }

    if (!Json::getValue(result, kBlocktemplateBlob).IsString() ||
        !Json::getValue(result, kBlockhashingBlob).IsString() ||
        !Json::getValue(result, kPrevHash).IsString() ||
        !Json::getValue(result, kHeight).IsUint64() ||
        !Json::getValue(result, kDifficulty).IsUint64()) {
        LOG_ERR("[%s] invalid block template field types", pool().daemon().url().data());
        return false;
    }

    if (m_job.algorithm().family() == Algorithm::RANDOM_X && !Json::getValue(result, kSeedHash).IsString()) {
        LOG_ERR("[%s] RandomX block template is missing a valid seed hash", pool().daemon().url().data());
        return false;
    }

    const char *blobData = Json::getString(result, kBlockhashingBlob);
    if (pool().coin().isValid()) {
        uint8_t blobVersion = 0;
        if (blobData) {
            Cvt::fromHex(&blobVersion, 1, blobData, 2);
        }
        m_job.setAlgorithm(pool().coin().algorithm(blobVersion));
    }

    if (!m_job.setBlob(blobData)) {
        return false;
    }

    m_job.setHeight(Json::getUint64(result, kHeight));
    m_job.setSeedHash(Json::getString(result, kSeedHash));

    submitBlockTemplate(result);

    return true;
}


void xmrig::SelfSelectClient::getBlockTemplate()
{
    if (m_templateRequest != 0) {
        return;
    }

    setState(WaitState);

    using namespace rapidjson;
    Document doc(kObjectType);
    auto &allocator = doc.GetAllocator();

    Value params(kObjectType);
    params.AddMember("wallet_address",  m_job.poolWallet().toJSON(), allocator);
    params.AddMember("extra_nonce",     m_job.extraNonce().toJSON(), allocator);

    const int64_t id = m_sequence++;
    JsonRequest::create(doc, id, "getblocktemplate", params);

    FetchRequest req(HTTP_POST, pool().daemon().host(), pool().daemon().port(), "/json_rpc", doc, pool().daemon().isTLS(), isQuiet());
    req.fingerprint = pool().fingerprint();
    req.timeout = 10000;
    m_templateRequest = id;
    fetch(tag(), std::move(req), m_httpListener, TemplateRequest, static_cast<uint64_t>(id));
}


void xmrig::SelfSelectClient::retry()
{
    setState(RetryState);
}


void xmrig::SelfSelectClient::setState(State state)
{
    if (m_state == state) {
        return;
    }

    switch (state) {
    case IdleState:
        m_timestamp = 0;
        m_failures  = 0;
        break;

    case WaitState:
        m_timestamp = Chrono::steadyMSecs();
        break;

    case RetryState:
        m_timestamp = Chrono::steadyMSecs();

        if (m_failures > m_retries) {
            m_listener->onClose(this, static_cast<int>(m_failures));
        }

        m_failures++;
        break;
    }

    m_state = state;
}


void xmrig::SelfSelectClient::submitBlockTemplate(rapidjson::Value &result)
{
    using namespace rapidjson;
    Document doc(kObjectType);
    auto &allocator = doc.GetAllocator();

    m_blocktemplate = Json::getString(result,kBlocktemplateBlob);
    m_blockDiff     = Json::getUint64(result, kDifficulty);

    Value params(kObjectType);
    params.AddMember(StringRef(kId),            m_job.clientId().toJSON(), allocator);
    params.AddMember(StringRef(kJobId),         m_job.id().toJSON(), allocator);
    Value blob;
    blob.CopyFrom(Json::getValue(result, kBlocktemplateBlob), allocator);
    Value difficulty;
    difficulty.CopyFrom(Json::getValue(result, kDifficulty), allocator);
    Value prevHash;
    prevHash.CopyFrom(Json::getValue(result, kPrevHash), allocator);
    Value seedHash;
    seedHash.CopyFrom(Json::getValue(result, kSeedHash), allocator);
    Value nextSeedHash;
    nextSeedHash.CopyFrom(Json::getValue(result, kNextSeedHash), allocator);

    params.AddMember(StringRef(kBlob),          blob, allocator);
    params.AddMember(StringRef(kHeight),        m_job.height(), allocator);
    params.AddMember(StringRef(kDifficulty),    difficulty, allocator);
    params.AddMember(StringRef(kPrevHash),      prevHash, allocator);
    params.AddMember(StringRef(kSeedHash),      seedHash, allocator);
    params.AddMember(StringRef(kNextSeedHash),  nextSeedHash, allocator);

    JsonRequest::create(doc, sequence(), "block_template", params);

    send(doc, [this](const rapidjson::Value &result, bool success, uint64_t) {
        if (!success) {
            if (!isQuiet()) {
                LOG_ERR("[%s] error: " RED_BOLD("\"%s\"") RED_S ", code: %d", pool().daemon().url().data(), Json::getString(result, "message"), Json::getInt(result, "code"));
            }

            return retry();
        }

        if (!m_active) {
            return;
        }

        if (m_failures > m_retries) {
            m_listener->onLoginSuccess(this);
        }

        setState(IdleState);
        m_listener->onJobReceived(this, m_job, rapidjson::Value{});
    });
}


void xmrig::SelfSelectClient::submitOriginDaemon(const JobResult& result)
{
    if (result.diff == 0 || m_blockDiff == 0) {
        return;
    }

    if (result.actualDiff() < m_blockDiff) {
        m_originNotSubmitted++;
        LOG_DEBUG("%s " RED_BOLD("not submitted to origin daemon, difficulty too low") " (%" PRId64 "/%" PRId64 ") "
            BLACK_BOLD(" diff ") BLACK_BOLD("%" PRIu64) BLACK_BOLD(" vs. ") BLACK_BOLD("%" PRIu64),
            Tags::origin(), m_originSubmitted, m_originNotSubmitted, m_blockDiff, result.actualDiff());
        return;
    }

    char *data = m_blocktemplate.data();
    Cvt::toHex(data + 78, 8, reinterpret_cast<const uint8_t*>(&result.nonce), 4);

    using namespace rapidjson;
    Document doc(kObjectType);

    Value params(kArrayType);
    params.PushBack(m_blocktemplate.toJSON(), doc.GetAllocator());

    const int64_t id = m_sequence++;
    JsonRequest::create(doc, id, "submitblock", params);
    m_results[id] = SubmitResult(id, result.diff, result.actualDiff(), 0, result.backend, result.algorithm, pool().coin());

    FetchRequest req(HTTP_POST, pool().daemon().host(), pool().daemon().port(), "/json_rpc", doc, pool().daemon().isTLS(), isQuiet());
    req.fingerprint = pool().fingerprint();
    req.timeout = 10000;
    fetch(tag(), std::move(req), m_httpListener, OriginSubmitRequest, static_cast<uint64_t>(id));

    m_originSubmitted++;
    LOG_INFO("%s " GREEN_BOLD("submitted to origin daemon") " (%" PRId64 "/%" PRId64 ") "
        " diff " WHITE("%" PRIu64) " vs. " WHITE("%" PRIu64),
        Tags::origin(), m_originSubmitted, m_originNotSubmitted, m_blockDiff, result.actualDiff(), result.diff);
}

void xmrig::SelfSelectClient::onHttpData(const HttpData &data)
{
    const bool originSubmit = data.userType == OriginSubmitRequest;
    const int64_t requestId = static_cast<int64_t>(data.rpcId);

    if (originSubmit && m_results.count(requestId) == 0) {
        return;
    }

    if (!originSubmit && requestId != m_templateRequest) {
        return;
    }

    if (!originSubmit) {
        m_templateRequest = 0;
    }

    if (data.status != 200) {
        if (originSubmit) {
            const std::string message = "origin daemon HTTP " + std::to_string(data.status) + "; block acceptance unknown";
            completeOriginSubmit(requestId, message.c_str(), true);
            return;
        }
        return retry();
    }

    rapidjson::Document doc;
    if (doc.Parse(data.body.c_str()).HasParseError() || !doc.IsObject()) {
        if (!isQuiet()) {
            LOG_ERR("[%s] JSON decode failed: \"%s\"",  pool().daemon().url().data(), rapidjson::GetParseError_En(doc.GetParseError()));
        }

        if (originSubmit) {
            completeOriginSubmit(requestId, "invalid origin daemon response; block acceptance unknown", true);
            return;
        }
        return retry();
    }

    const int64_t id = Json::getInt64(doc, "id", -1);
    if (id != requestId) {
        if (originSubmit) {
            completeOriginSubmit(requestId, "mismatched origin daemon response id; block acceptance unknown", true);
        }
        else {
            retry();
        }
        return;
    }

    auto &result = const_cast<rapidjson::Value &>(Json::getValue(doc, "result"));
    const auto &error = Json::getValue(doc, "error");
    if (originSubmit) {
        if (error.IsObject()) {
            completeOriginSubmit(id, Json::getString(error, "message", "origin daemon rejected block"));
        }
        else if (!result.IsObject()) {
            completeOriginSubmit(id, "invalid origin daemon submit result; block acceptance unknown", true);
        }
        else {
            const char *status = Json::getString(result, "status");
            completeOriginSubmit(id, status && strcmp(status, "OK") != 0 ? status : nullptr);
        }
        return;
    }

    if (!parseTemplateResponse(id, result, error)) {
        retry();
    }
}


bool xmrig::SelfSelectClient::completeOriginSubmit(int64_t id, const char *error, bool outcomeUnknown)
{
    auto it = m_results.find(id);
    if (it == m_results.end()) {
        return false;
    }

    SubmitResult result = it->second;
    m_results.erase(it);
    result.done();
    result.setOutcomeUnknown(outcomeUnknown);
    if (error) {
        LOG_WARN("%s %s after %" PRIu64 " ms: %s", Tags::origin(),
                 outcomeUnknown ? "submission outcome unknown" : "block rejected",
                 result.elapsed, error);
    }
    else {
        LOG_NOTICE("%s block accepted after %" PRIu64 " ms", Tags::origin(), result.elapsed);
    }
    return true;
}


void xmrig::SelfSelectClient::failOriginSubmissions(const char *error)
{
    while (!m_results.empty()) {
        completeOriginSubmit(m_results.begin()->first, error, true);
    }
}
