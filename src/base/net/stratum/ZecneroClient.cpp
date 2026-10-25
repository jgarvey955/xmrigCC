/* Zecnero direct solo mining. SPDX-License-Identifier: GPL-3.0-or-later */
#include "base/net/stratum/ZecneroClient.h"
#include "3rdparty/rapidjson/document.h"
#include "base/io/log/Log.h"
#include "base/kernel/interfaces/IClientListener.h"
#include "base/net/http/Fetch.h"
#include "base/net/http/HttpData.h"
#include "base/net/http/HttpListener.h"
#include "base/tools/Cvt.h"
#include "base/tools/Timer.h"
#include "net/JobResult.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#ifndef _WIN32
#   include <cerrno>
#   include <fcntl.h>
#   include <sys/stat.h>
#   include <unistd.h>
#endif
#include <uv.h>

namespace {
std::string base64(const std::string &input)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < input.size(); i += 3) {
        const size_t left = input.size() - i;
        const uint32_t n = (uint32_t(uint8_t(input[i])) << 16) |
            (left > 1 ? uint32_t(uint8_t(input[i + 1])) << 8 : 0) |
            (left > 2 ? uint8_t(input[i + 2]) : 0);
        out += alphabet[(n >> 18) & 63];
        out += alphabet[(n >> 12) & 63];
        out += left > 1 ? alphabet[(n >> 6) & 63] : '=';
        out += left > 2 ? alphabet[n & 63] : '=';
    }
    return out;
}

bool readCookie(const char *path, std::string &credentials, std::string &error)
{
#ifndef _WIN32
    const int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        error = std::string("cannot read cookie file '") + path + "' (check path and permissions)";
        return false;
    }
    struct stat metadata {};
    if (fstat(fd, &metadata) != 0) {
        close(fd);
        error = std::string("cannot read cookie file '") + path + "' (check path and permissions)";
        return false;
    }
    if (!S_ISREG(metadata.st_mode) || (metadata.st_mode & 0077) != 0) {
        close(fd);
        error = std::string("cookie file '") + path + "' must be a private regular file (owner-only permissions)";
        return false;
    }
    char buffer[4097];
    size_t used = 0;
    while (used < sizeof(buffer)) {
        const ssize_t count = read(fd, buffer + used, sizeof(buffer) - used);
        if (count > 0) { used += static_cast<size_t>(count); continue; }
        if (count == 0) { break; }
        if (errno == EINTR) { continue; }
        close(fd);
        error = std::string("cannot read cookie file '") + path + "' (check path and permissions)";
        return false;
    }
    close(fd);
    if (used == sizeof(buffer)) {
        error = std::string("unreadable or oversized cookie file '") + path + "'";
        return false;
    }
    credentials.assign(buffer, used);
#else
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = std::string("cannot read cookie file '") + path + "' (check path and permissions)";
        return false;
    }
    char buffer[4097];
    file.read(buffer, sizeof(buffer));
    if (file.bad() || file.gcount() == sizeof(buffer)) {
        error = std::string("unreadable or oversized cookie file '") + path + "'";
        return false;
    }
    credentials.assign(buffer, static_cast<size_t>(file.gcount()));
#endif
    while (!credentials.empty() && (credentials.back() == '\n' || credentials.back() == '\r')) { credentials.pop_back(); }
    const auto colon = credentials.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 == credentials.size() ||
        credentials.find_first_of("\r\n") != std::string::npos || credentials.find('\0') != std::string::npos) {
        error = std::string("invalid username:password cookie file '") + path + "'";
        return false;
    }
    return true;
}

bool writeCookie(const char *path, const std::string &credentials, std::string &error)
{
    // Write an owner-only temporary inode and atomically replace the destination.
    uv_fs_t req;
    const std::string pattern = std::string(path) + ".tmp-XXXXXX";
    const int fd = uv_fs_mkstemp(nullptr, &req, pattern.c_str(), nullptr);
    const std::string temporary = fd >= 0 ? req.path : "";
    uv_fs_req_cleanup(&req);
    int status = fd < 0 ? fd : 0;
    if (fd >= 0) {
        size_t offset = 0;
        while (offset < credentials.size()) {
            auto buffer = uv_buf_init(const_cast<char *>(credentials.data() + offset),
                                      static_cast<unsigned int>(credentials.size() - offset));
            const int written = uv_fs_write(nullptr, &req, fd, &buffer, 1, static_cast<int64_t>(offset), nullptr);
            uv_fs_req_cleanup(&req);
            if (written <= 0) { status = written < 0 ? written : UV_EIO; break; }
            offset += static_cast<size_t>(written);
        }
        if (status == 0) {
            const int synced = uv_fs_fsync(nullptr, &req, fd, nullptr);
            uv_fs_req_cleanup(&req);
            if (synced < 0) { status = synced; }
        }
        const int closed = uv_fs_close(nullptr, &req, fd, nullptr);
        uv_fs_req_cleanup(&req);
        if (status == 0 && closed < 0) { status = closed; }
        if (status == 0) {
            status = uv_fs_rename(nullptr, &req, temporary.c_str(), path, nullptr);
            uv_fs_req_cleanup(&req);
        }
        if (status < 0) {
            uv_fs_unlink(nullptr, &req, temporary.c_str(), nullptr);
            uv_fs_req_cleanup(&req);
        }
    }
    if (status < 0) {
        error = std::string("cannot securely update cookie file '") + path + "': " + uv_strerror(status);
        return false;
    }
    return true;
}

bool validFingerprint(const std::string &value)
{
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}

std::string lower(std::string value)
{
    for (char &c : value) { if (c >= 'A' && c <= 'Z') { c += 'a' - 'A'; } }
    return value;
}

const rapidjson::Value &field(const rapidjson::Value &value, const char *name)
{
    static const rapidjson::Value missing;
    if (!value.IsObject()) { return missing; }
    auto i = value.FindMember(name);
    return i == value.MemberEnd() ? missing : i->value;
}
} // namespace

xmrig::ZecneroClient::ZecneroClient(int id, IClientListener *listener) : BaseClient(id, listener), m_timer(new Timer(this)) {}

xmrig::ZecneroClient::~ZecneroClient()
{
    m_httpListener.reset();
    delete m_timer;
}

void xmrig::ZecneroClient::clearRequests(const char *reason)
{
    m_httpListener.reset(); // outstanding HTTP requests keep only weak references
    m_templateRequest = 0;
    m_cookieRequest = 0;
    m_work.clear();
    m_job.reset();
    while (!m_results.empty()) { handleSubmitResponse(m_results.begin()->first, reason); }
}

bool xmrig::ZecneroClient::disconnect()
{
    m_waitingForSync = false;
    m_state = UnconnectedState;
    m_timer->stop();
    clearRequests("RPC disconnected; block acceptance unknown");
    return true;
}

void xmrig::ZecneroClient::connect()
{
    m_waitingForSync = false;
    m_timer->stop();
    clearRequests("RPC reconnected; block acceptance unknown");
    m_state = ConnectingState;
    m_httpListener = std::make_shared<HttpListener>(this);
    if (remoteCookie()) { retrieveCookie(); }
    else { getBlockTemplate(); }
}

bool xmrig::ZecneroClient::remoteCookie() const
{
    return !m_pool.daemonCookieSource().isEmpty();
}

void xmrig::ZecneroClient::retrieveCookie()
{
#ifndef XMRIG_FEATURE_TLS
    fail("remote miner cookies require a TLS-enabled build");
#else
    if (m_cookieRequest != 0) { return; }
    const std::string source = m_pool.daemonCookieSource().data();
    if (source.compare(0, 8, "https://") != 0 || m_pool.daemonCookieFile().isEmpty() ||
        m_pool.rpcLogin().isEmpty() || !m_pool.isTLS()) {
        fail("remote miner cookies require HTTPS RPC, source, destination and rpc-login");
        return;
    }
    if (source.find_first_of("@#\r\n\t ") != std::string::npos) {
        fail("invalid miner cookie HTTPS URL");
        return;
    }

    const auto slash = source.find('/', 8);
    const std::string authority = source.substr(8, slash == std::string::npos ? slash : slash - 8);
    std::string host;
    std::string portText;
    if (!authority.empty() && authority[0] == '[') {
        const auto end = authority.find(']');
        if (end == std::string::npos || (end + 1 < authority.size() && authority[end + 1] != ':')) {
            fail("invalid miner cookie HTTPS host");
            return;
        }
        host = authority.substr(1, end - 1);
        if (end + 1 < authority.size()) { portText = authority.substr(end + 2); }
    }
    else {
        const auto colon = authority.find(':');
        host = authority.substr(0, colon);
        if (colon != std::string::npos) { portText = authority.substr(colon + 1); }
    }
    unsigned port = 443;
    if (!portText.empty()) {
        if (portText.size() > 5 || !std::all_of(portText.begin(), portText.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            fail("invalid miner cookie HTTPS port");
            return;
        }
        port = static_cast<unsigned>(std::strtoul(portText.c_str(), nullptr, 10));
    }
    const std::string path = slash == std::string::npos ? "/mining/cookie" : source.substr(slash);
    if (host.empty() || port == 0 || port > 65535 || path != "/mining/cookie" ||
        lower(host) != lower(m_pool.host().data()) || port != m_pool.port()) {
        fail("miner cookie endpoint must be /mining/cookie on the configured HTTPS RPC origin");
        return;
    }

    const std::string login = m_pool.rpcLogin().data();
    const auto loginColon = login.find(':');
    if (login.size() > 4096 || loginColon == std::string::npos || loginColon == 0 ||
        loginColon + 1 == login.size() || login.find_first_of("\r\n") != std::string::npos) {
        fail("invalid miner cookie bootstrap credentials");
        return;
    }
    std::string fingerprint = m_pool.daemonCookieFingerprint().isEmpty()
        ? std::string(m_pool.fingerprint().isEmpty() ? "" : m_pool.fingerprint().data())
        : std::string(m_pool.daemonCookieFingerprint().data());
    if (!fingerprint.empty() && !validFingerprint(fingerprint)) {
        fail("miner cookie TLS fingerprint must contain 64 hexadecimal digits");
        return;
    }

    FetchRequest request(HTTP_GET, host.c_str(), static_cast<uint16_t>(port), path.c_str(), true, isQuiet());
    request.requireVerifiedTls = true;
    if (!fingerprint.empty()) { request.fingerprint = fingerprint.c_str(); }
    request.timeout = 10000;
    request.headers.emplace("Authorization", "Basic " + base64(login));
    m_cookieRequest = m_sequence++;
    fetch(tag(), std::move(request), m_httpListener, 0, static_cast<uint64_t>(m_cookieRequest));
#endif
}

void xmrig::ZecneroClient::fail(const char *message)
{
    m_waitingForSync = false;
    if (!isQuiet()) { LOG_ERR("%s Zecnero RPC: %s", tag(), message); }
    m_state = ConnectingState;
    clearRequests("RPC failed; block acceptance unknown");
    m_timer->start(m_retryPause, 0);
    m_listener->onClose(this, static_cast<int>(++m_failures));
}

bool xmrig::ZecneroClient::authorization(std::string &value, std::string &error) const
{
    std::string credentials;
    const auto &destination = m_pool.daemonCookieFile();
    if (!destination.isEmpty()) {
        // Read the configured cookie for every RPC, including after local node
        // rotation or remote miner-cookie renewal.
        if (!readCookie(destination.data(), credentials, error)) { return false; }
    }
    else if (!m_pool.rpcLogin().isEmpty()) {
        credentials = m_pool.rpcLogin().data();
    }
    else {
        value.clear();
        return true;
    }
    value = "Basic " + base64(credentials);
    return true;
}

bool xmrig::ZecneroClient::sendRpc(int64_t id, const char *method, rapidjson::Value &params, rapidjson::Document &doc, std::string *errorOut)
{
    std::string auth, error;
    if (!authorization(auth, error)) {
        if (errorOut) { *errorOut = std::move(error); }
        return false;
    }
    auto &allocator = doc.GetAllocator();
    doc.AddMember("jsonrpc", "2.0", allocator);
    doc.AddMember("id", id, allocator);
    doc.AddMember("method", rapidjson::Value(method, allocator), allocator);
    doc.AddMember("params", params, allocator);
    FetchRequest req(HTTP_POST, m_pool.host(), m_pool.port(), "/", doc, m_pool.isTLS(), isQuiet());
    if (remoteCookie()) {
        req.requireVerifiedTls = true;
        req.fingerprint = m_pool.daemonCookieFingerprint().isEmpty()
            ? m_pool.fingerprint()
            : m_pool.daemonCookieFingerprint();
    }
    else {
        req.fingerprint = m_pool.fingerprint();
    }
    req.timeout = 10000;
    if (!auth.empty()) { req.headers.emplace("Authorization", std::move(auth)); }
    fetch(tag(), std::move(req), m_httpListener, 0, static_cast<uint64_t>(id));
    return true;
}

void xmrig::ZecneroClient::getBlockTemplate()
{
    if (m_templateRequest || m_state == UnconnectedState) { return; }
    rapidjson::Document doc(rapidjson::kObjectType);
    rapidjson::Value params(rapidjson::kArrayType);
    rapidjson::Value request(rapidjson::kObjectType);
    if (!m_pool.user().isEmpty() && m_pool.user() != "x") {
        request.AddMember("mineraddress", m_pool.user().toJSON(), doc.GetAllocator());
    }
    params.PushBack(request, doc.GetAllocator());
    m_templateRequest = m_sequence++;
    std::string error;
    if (!sendRpc(m_templateRequest, "getblocktemplate", params, doc, &error)) {
        m_templateRequest = 0;
        fail(error.c_str());
    }
}

void xmrig::ZecneroClient::onTimer(const Timer *)
{
    if (remoteCookie() && m_cookieRequest == 0 && m_cookieRefreshAt != 0 && Chrono::steadyMSecs() >= m_cookieRefreshAt) {
        retrieveCookie();
        return;
    }
    if (m_state == ConnectingState && m_waitingForSync) { getBlockTemplate(); }
    else if (m_state == ConnectingState) { connect(); }
    else if (m_state == ConnectedState) { getBlockTemplate(); }
}

int64_t xmrig::ZecneroClient::submit(const JobResult &result)
{
    if (m_state != ConnectedState || !result.algorithm.isZecnero() || result.nonce > UINT32_MAX) { return -1; }
    // One valid candidate is enough to extend this tip. In particular, an easy
    // Regtest target must not flood RPC with competing blocks while verification
    // of the first candidate is still in progress. Template polls remain independent.
    if (!m_results.empty()) { return -1; }
    const auto work = std::find_if(m_work.begin(), m_work.end(), [&result](const Work &w) { return w.job.id() == result.jobId; });
    if (work == m_work.end() || !work->job.meetsTarget(result.result())) { return -1; }

    const int64_t id = m_sequence++;
    rapidjson::Document doc(rapidjson::kObjectType);
    rapidjson::Value params(rapidjson::kArrayType);
    params.PushBack(Cvt::toHex(work->block.block(static_cast<uint32_t>(result.nonce))).toJSON(doc), doc.GetAllocator());
    // A poll in flight never blocks a discovered block from being submitted.
    std::string error;
    if (!sendRpc(id, "submitblock", params, doc, &error)) {
        if (!isQuiet()) { LOG_ERR("%s Zecnero submit failed before send: %s", tag(), error.c_str()); }
        return -1;
    }
    m_results.emplace(id, SubmitResult(id, result.diff, result.actualDiff(), 0, result.backend, result.algorithm, m_pool.coin()));
    return id;
}

void xmrig::ZecneroClient::onHttpData(const HttpData &data)
{
    if (m_state == UnconnectedState) { return; }
    const int64_t id = static_cast<int64_t>(data.rpcId);
    if (id == m_cookieRequest && m_cookieRequest != 0) {
        m_cookieRequest = 0;
        if (data.status != 200) {
            fail("miner cookie retrieval failed; check HTTPS trust and bootstrap credentials");
            return;
        }
        std::string credentials = data.body;
        while (!credentials.empty() && (credentials.back() == '\r' || credentials.back() == '\n')) { credentials.pop_back(); }
        const auto colon = credentials.find(':');
        if (credentials.size() > 4096 || colon == std::string::npos ||
            credentials.substr(0, colon) != "__miner__" || colon + 1 == credentials.size() ||
            credentials.find_first_of("\r\n") != std::string::npos || credentials.find('\0') != std::string::npos) {
            fail("miner cookie endpoint returned invalid credentials");
            return;
        }
        std::string existing;
        std::string error;
        if (!readCookie(m_pool.daemonCookieFile().data(), existing, error) || existing != credentials) {
            if (!writeCookie(m_pool.daemonCookieFile().data(), credentials, error)) {
                fail(error.c_str());
                return;
            }
            LOG_NOTICE("%s securely refreshed client-specific miner cookie", tag());
        }
        uint64_t ttl = 43200;
        const auto ttlHeader = data.headers.find("x-zecnero-cookie-ttl");
        if (ttlHeader != data.headers.end() && !ttlHeader->second.empty() &&
            std::all_of(ttlHeader->second.begin(), ttlHeader->second.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            const uint64_t advertised = std::strtoull(ttlHeader->second.c_str(), nullptr, 10);
            if (advertised >= 300 && advertised <= 604800) { ttl = advertised; }
        }
        m_cookieRefreshAt = Chrono::steadyMSecs() + ttl * 800;
        getBlockTemplate();
        return;
    }
    const bool isTemplate = id == m_templateRequest;
    if (!isTemplate && m_results.count(id) == 0) { return; } // superseded request
    if (isTemplate) { m_templateRequest = 0; }
    if (data.status != 200) {
        const std::string error = "HTTP " + std::to_string(data.status) + (data.status == 401 ? " (check RPC cookie or credentials)" : "");
        fail(error.c_str());
        return;
    }
    m_ip = data.ip().c_str();
    m_tlsVersion = data.tlsVersion();
    m_tlsFingerprint = data.tlsFingerprint();
    rapidjson::Document doc;
    if (doc.Parse(data.body.data(), data.body.size()).HasParseError() || !doc.IsObject() ||
        !field(doc, "id").IsInt64() || field(doc, "id").GetInt64() != id) {
        fail("invalid JSON-RPC response or request id"); return;
    }
    const auto &error = field(doc, "error");
    const auto &result = field(doc, "result");
    if (!isTemplate) {
        const char *message = nullptr;
        if (!error.IsNull()) { message = field(error, "message").IsString() ? field(error, "message").GetString() : "submitblock RPC error"; }
        else if (!doc.HasMember("result")) { message = "missing submitblock result"; }
        else if (result.IsString()) { message = result.GetStringLength() ? result.GetString() : "empty submitblock rejection"; }
        else if (!result.IsNull()) { message = "invalid submitblock result"; }
        handleSubmitResponse(id, message);
        if (!message) {
            LOG_NOTICE("%s Zecnero block accepted by node", tag());
            m_work.clear();
            // Discard any template requested before this block was accepted.
            m_templateRequest = 0;
        }
        getBlockTemplate();
        return;
    }
    if (!error.IsNull()) {
        if (field(error, "code").IsInt() && field(error, "code").GetInt() == -10) {
            if (!m_waitingForSync) {
                LOG_NOTICE("%s waiting for Zecnero daemon to finish syncing; mining paused", tag());
                m_waitingForSync = true;
                m_state = ConnectingState;
                clearRequests("node syncing; block acceptance unknown");
                m_httpListener = std::make_shared<HttpListener>(this);
                const uint64_t interval = std::max<uint64_t>(1000, m_pool.pollInterval());
                m_timer->start(interval, interval);
            }
            // A reachable but syncing daemon is still unavailable for mining.
            // Count every unsuccessful poll so the strategy can reach its retry
            // limit and activate a backup, while this client keeps polling for recovery.
            m_listener->onClose(this, static_cast<int>(++m_failures));
            return;
        }
        fail(field(error, "message").IsString() ? field(error, "message").GetString() : "getblocktemplate RPC error"); return;
    }
    ZecneroBlockTemplate block;
    std::string parseError;
    if (!block.parse(result, parseError)) { fail(parseError.c_str()); return; }
    if (!m_pool.user().isEmpty() && m_pool.user() != "x") {
        const auto &caps = field(result, "capabilities");
        bool payoutSupported = false;
        if (caps.IsArray()) {
            for (const auto &cap : caps.GetArray()) {
                if (cap.IsString() && std::string(cap.GetString(), cap.GetStringLength()) == "mineraddress") { payoutSupported = true; }
            }
        }
        if (!payoutSupported) { fail("node does not advertise mineraddress support; upgrade the node or omit user to use its configured payout"); return; }
    }
    const uint64_t now = Chrono::steadyMSecs();
    if (!m_work.empty() && block.sameWork(m_work.back().block) &&
        now - m_jobTime < std::max<uint64_t>(1000, m_pool.jobTimeout())) { return; }

    // Work from an old parent must never be paired with a new template.
    if (!m_work.empty() && !std::equal(block.header.begin() + 4, block.header.begin() + 36, m_work.back().block.header.begin() + 4)) {
        m_work.clear();
    }
    const auto extra = Cvt::randomBytes(28);
    block.setExtraNonce(extra.data());
    Job job(false, block.powVersion == 2 ? Algorithm::RX_ZECNERO2 : Algorithm::RX_ZECNERO, String());
    const std::string jobId = std::to_string(id);
    job.setId(jobId.c_str());
    job.setHeight(block.height);
    if (!job.setBlob(Cvt::toHex(block.header)) || !job.setSeedHash(Cvt::toHex(block.seed)) || !job.setFullTarget(Cvt::toHex(block.target))) {
        fail("cannot construct Zecnero mining job"); return;
    }
    bool supported = true;
    m_listener->onVerifyAlgorithm(this, job.algorithm(), &supported);
    if (!supported) { fail("rx/zecnero requires an enabled CPU backend with RandomX support"); return; }
    m_job = job;
    m_work.push_back({std::move(job), std::move(block)});
    while (m_work.size() > 8) { m_work.pop_front(); }
    m_jobTime = now;
    m_waitingForSync = false;
    if (m_state != ConnectedState) {
        m_state = ConnectedState;
        m_failures = 0;
        const uint64_t interval = std::max<uint64_t>(1000, m_pool.pollInterval());
        m_timer->start(interval, interval);
        m_listener->onLoginSuccess(this);
    }
    m_listener->onJobReceived(this, m_job, result);
}
