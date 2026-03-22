if (WITH_ARGON2)
    if (NOT EXISTS "${CMAKE_SOURCE_DIR}/src/3rdparty/argon2/CMakeLists.txt")
        message(WARNING "Argon2 sources not found at src/3rdparty/argon2, disabling WITH_ARGON2")
        set(WITH_ARGON2 OFF)
    endif()
endif()

if (WITH_ARGON2)
    add_definitions(/DXMRIG_ALGO_ARGON2)

    list(APPEND HEADERS_CRYPTO
        src/crypto/argon2/Hash.h
        src/crypto/argon2/Impl.h
    )

    list(APPEND SOURCES_CRYPTO
        src/crypto/argon2/Impl.cpp
    )

    add_subdirectory(src/3rdparty/argon2)
    set(ARGON2_LIBRARY argon2)
else()
    remove_definitions(/DXMRIG_ALGO_ARGON2)
    set(ARGON2_LIBRARY "")
endif()
