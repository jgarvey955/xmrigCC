/* XMRigCC
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef XMRIG_BUILDINFO_H
#define XMRIG_BUILDINFO_H

#include <string>

#ifdef __GLIBC__
#   include <gnu/libc-version.h>
#endif

#ifndef XMRIG_DAEMON_PROJECT
#   include <uv.h>
#   ifdef XMRIG_FEATURE_TLS
#       include <openssl/crypto.h>
#   endif
#   ifdef XMRIG_FEATURE_HWLOC
#       include <hwloc.h>
#   endif
#   ifdef CPPHTTPLIB_ZLIB_SUPPORT
#       include <zlib.h>
#   endif
#endif

namespace xmrig {

inline const char *buildLinkage()
{
#   ifdef XMRIG_BUILD_STATIC
    return "static";
#   else
    return "dynamic";
#   endif
}

inline std::string runtimeLibraryVersions()
{
    std::string libs;
#   ifdef __GLIBC__
    libs = std::string("glibc/") + gnu_get_libc_version();
#   endif

#   if defined(_GLIBCXX_RELEASE)
    if (!libs.empty()) { libs += ' '; }
    libs += "libstdc++/" + std::to_string(_GLIBCXX_RELEASE);
#   elif defined(_LIBCPP_VERSION)
    if (!libs.empty()) { libs += ' '; }
    libs += "libc++/" + std::to_string(_LIBCPP_VERSION);
#   elif defined(_MSVC_STL_VERSION)
    if (!libs.empty()) { libs += ' '; }
    libs += "MSVC-STL/" + std::to_string(_MSVC_STL_VERSION);
#   endif

    return libs.empty() ? "system C/C++ runtime" : libs;
}

#ifndef XMRIG_DAEMON_PROJECT
inline std::string libraryVersions(bool includeHwloc = true)
{
    std::string libs = std::string("libuv/") + uv_version_string();

#   ifdef XMRIG_FEATURE_TLS
#       if defined(LIBRESSL_VERSION_TEXT)
    std::string tls = LIBRESSL_VERSION_TEXT;
#       elif OPENSSL_VERSION_NUMBER >= 0x10100000L
    std::string tls = OpenSSL_version(OPENSSL_VERSION);
#       else
    std::string tls = SSLeay_version(SSLEAY_VERSION);
#       endif
    // Library name and version precede the optional release date.
    const auto space = tls.find(' ');
    if (space != std::string::npos) {
        tls[space] = '/';
        const auto date = tls.find(' ', space + 1);
        if (date != std::string::npos) { tls.resize(date); }
    }
    libs += ' ' + tls;
#   endif

#   ifdef XMRIG_FEATURE_HWLOC
    if (includeHwloc) {
#       ifdef HWLOC_VERSION
        libs += " hwloc/" HWLOC_VERSION;
#       else
        const unsigned api = hwloc_get_api_version();
        libs += " hwloc/API-" + std::to_string((api >> 16) & 0xff) + "." +
                std::to_string((api >> 8) & 0xff) + "." + std::to_string(api & 0xff);
#       endif
    }
#   else
    (void) includeHwloc;
#   endif

#   ifdef CPPHTTPLIB_ZLIB_SUPPORT
    libs += std::string(" zlib/") + zlibVersion();
#   endif

    return libs + ' ' + runtimeLibraryVersions();
}
#endif

} // namespace xmrig

#endif // XMRIG_BUILDINFO_H
