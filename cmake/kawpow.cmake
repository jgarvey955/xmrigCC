if (WITH_KAWPOW)
    if (NOT EXISTS "${CMAKE_SOURCE_DIR}/src/3rdparty/libethash/CMakeLists.txt")
        message(WARNING "KawPow sources not found at src/3rdparty/libethash, disabling WITH_KAWPOW")
        set(WITH_KAWPOW OFF)
    endif()
endif()

if (WITH_KAWPOW)
    add_definitions(/DXMRIG_ALGO_KAWPOW)

    list(APPEND HEADERS_CRYPTO
        src/crypto/kawpow/KPCache.h
        src/crypto/kawpow/KPHash.h
    )

    list(APPEND SOURCES_CRYPTO
        src/crypto/kawpow/KPCache.cpp
        src/crypto/kawpow/KPHash.cpp
    )

    add_subdirectory(src/3rdparty/libethash)
    set(ETHASH_LIBRARY ethash)
else()
    remove_definitions(/DXMRIG_ALGO_KAWPOW)
    set(ETHASH_LIBRARY "")
endif()
