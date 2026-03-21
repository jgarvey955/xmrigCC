if (WITH_CC_SERVER)
    set(SOURCES_CC_SERVER
            src/3rdparty/fmt/format.cc
            src/base/io/log/backends/ConsoleLog.cpp
            src/base/io/log/backends/FileLog.cpp
            src/base/io/log/FileLogWriter.cpp
            src/base/io/log/Log.cpp
            src/base/io/log/Tags.cpp
            src/base/io/json/Json.cpp
            src/base/io/json/JsonChain.cpp
            src/base/io/Console.cpp
            src/base/io/Env.cpp
            src/base/io/Signals.cpp
            src/base/kernel/config/Title.cpp
            src/base/kernel/Process.cpp
            src/base/tools/Arguments.cpp
            src/base/tools/String.cpp
            src/cc/CCCServerConfig.cpp
            src/cc/CCServer.cpp
            src/cc/Summary.cpp
            src/cc/Service.cpp
            src/cc/Httpd.cpp
            src/cc/XMRigCC.cpp
            )

    if (WIN32)
        set(SOURCES_CC_SERVER
                "${SOURCES_CC_SERVER}"
                src/base/io/json/Json_win.cpp
                )
    else()
        set(SOURCES_CC_SERVER
                "${SOURCES_CC_SERVER}"
                src/base/io/json/Json_unix.cpp
                )
    endif()

    add_definitions("/DXMRIG_FEATURE_CC_SERVER")
    add_definitions("/DCXXOPTS_NO_RTTI")

    if (WITH_TLS)
        set(SOURCES_CC_SERVER
                "${SOURCES_CC_SERVER}"
                src/base/net/tls/TlsGen.cpp
                )
        add_definitions(/DCPPHTTPLIB_OPENSSL_SUPPORT)
    endif()
else()
    remove_definitions(/DXMRIG_FEATURE_CC_SERVER)
endif()

if (WITH_CC_CLIENT)
    set(SOURCES_CC_CLIENT
            src/cc/CCClientConfig.cpp
            src/cc/CCClient.cpp
            src/base/io/log/backends/RemoteLog.cpp)
    add_definitions("/DXMRIG_FEATURE_CC_CLIENT")

    if (WITH_CC_SHELL_EXEC)
        add_definitions("/DXMRIG_FEATURE_CC_CLIENT_SHELL_EXECUTE")
    endif()

    if (WITH_TLS)
        add_definitions(/DCPPHTTPLIB_OPENSSL_SUPPORT)
    endif()
else()
    remove_definitions(/DXMRIG_FEATURE_CC_CLIENT)
endif()

if (WITH_CC_SERVER OR WITH_CC_CLIENT)
    set(SOURCES_CC_COMMON
            src/cc/ControlCommand.cpp
            src/cc/ClientStatus.cpp
            src/cc/GPUInfo.cpp)

    if (WITH_HTTPLIB_POLL)
        if (WIN32)
            if (CMAKE_C_COMPILER_ID MATCHES MSVC)
                add_definitions("/DCPPHTTPLIB_USE_POLL")
            endif()
        else ()
            add_definitions("/DCPPHTTPLIB_USE_POLL")
        endif()
    endif()

    if (WITH_ZLIB)
        set(ZLIB_ROOT ${XMRIG_DEPS})
        find_package(ZLIB)
        add_definitions(/DCPPHTTPLIB_ZLIB_SUPPORT)
    endif()
endif()
