/* XMRigCC. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef XMRIG_TLSVERIFY_H
#define XMRIG_TLSVERIFY_H

#include <cstdlib>
#include <atomic>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <uv.h>

namespace xmrig {
namespace tls {

inline std::atomic<bool> &untrustedSetting()
{
    // Preserve existing self-signed deployments unless strict verification is requested.
    static std::atomic<bool> value{true};
    return value;
}
inline void setAllowUntrusted(bool value) { untrustedSetting().store(value, std::memory_order_relaxed); }
inline bool allowUntrusted() { return untrustedSetting().load(std::memory_order_relaxed); }


inline void loadSystemTrust(SSL_CTX *ctx)
{
    if (!ctx || allowUntrusted()) { return; }
    ERR_clear_error();
    SSL_CTX_set_default_verify_paths(ctx);
#ifndef _WIN32
    // A locally built static OpenSSL may have /usr/local/ssl as its default.
    // Respect explicit trust settings, otherwise include the OS certificate bundle.
    if (!std::getenv("SSL_CERT_FILE") && !std::getenv("SSL_CERT_DIR")) {
        const char *const bundles[] = {
            "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
            "/etc/ssl/ca-bundle.pem", "/etc/ssl/cert.pem"
        };
        for (const auto *bundle : bundles) {
            if (SSL_CTX_load_verify_locations(ctx, bundle, nullptr) == 1) { break; }
        }
    }
#endif
    ERR_clear_error();
}

inline bool isIpAddress(const char *host)
{
    unsigned char address[16];
    return host && (uv_inet_pton(AF_INET, host, address) == 0 || uv_inet_pton(AF_INET6, host, address) == 0);
}

inline bool setServerName(SSL *ssl, const char *host)
{
    // RFC 6066 permits DNS hostnames only in the SNI host_name extension.
    return !host || !*host || isIpAddress(host) || SSL_set_tlsext_host_name(ssl, host) == 1;
}

inline bool verifyPeer(SSL *ssl, const char *host, bool pinned)
{
    if (!ssl || !host || !*host) { return false; }
    // An explicit SHA-256 pin authenticates self-signed pool/node certificates.
    // In strict mode without a pin, authenticate the chain and requested host.
    const bool untrustedAllowed = allowUntrusted();
    SSL_set_verify(ssl, (pinned || untrustedAllowed) ? SSL_VERIFY_NONE : SSL_VERIFY_PEER, nullptr);
    if (pinned || untrustedAllowed) { return true; }
    loadSystemTrust(SSL_get_SSL_CTX(ssl));
    auto *param = SSL_get0_param(ssl);
    if (isIpAddress(host)) {
        return X509_VERIFY_PARAM_set1_ip_asc(param, host) == 1;
    }
    return X509_VERIFY_PARAM_set1_host(param, host, 0) == 1;
}

} // namespace tls
} // namespace xmrig
#endif
