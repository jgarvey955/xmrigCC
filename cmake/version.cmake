function(xmrig_read_version version_header changelog out_version)
    if (NOT EXISTS "${version_header}")
        message(FATAL_ERROR "Version header not found: ${version_header}")
    endif()

    file(STRINGS "${version_header}" version_lines
        REGEX "^[ \t]*#define[ \t]+APP_VERSION[ \t]+\"[0-9]+\\.[0-9]+\\.[0-9]+\"[ \t]*$")
    list(LENGTH version_lines version_line_count)
    if (NOT version_line_count EQUAL 1)
        message(FATAL_ERROR "Expected exactly one semantic APP_VERSION in ${version_header}")
    endif()

    list(GET version_lines 0 version_line)
    string(REGEX REPLACE ".*\"([0-9]+\\.[0-9]+\\.[0-9]+)\".*" "\\1" version "${version_line}")

    foreach(component MAJOR MINOR PATCH)
        file(STRINGS "${version_header}" component_lines
            REGEX "^[ \t]*#define[ \t]+APP_VER_${component}[ \t]+[0-9]+[ \t]*$")
        list(LENGTH component_lines component_line_count)
        if (NOT component_line_count EQUAL 1)
            message(FATAL_ERROR "Expected exactly one APP_VER_${component} in ${version_header}")
        endif()

        list(GET component_lines 0 component_line)
        string(REGEX REPLACE ".*[ \t]([0-9]+)[ \t]*$" "\\1" version_${component} "${component_line}")
    endforeach()

    set(component_version "${version_MAJOR}.${version_MINOR}.${version_PATCH}")
    if (NOT version STREQUAL component_version)
        message(FATAL_ERROR
            "APP_VERSION ${version} disagrees with APP_VER_* ${component_version} in ${version_header}")
    endif()

    if (NOT EXISTS "${changelog}")
        message(FATAL_ERROR "Changelog not found: ${changelog}")
    endif()

    file(STRINGS "${changelog}" changelog_heading LIMIT_COUNT 1)
    if (NOT changelog_heading STREQUAL "# ${version}")
        message(FATAL_ERROR
            "The first changelog heading must be '# ${version}', found '${changelog_heading}'")
    endif()

    set(${out_version} "${version}" PARENT_SCOPE)
endfunction()
