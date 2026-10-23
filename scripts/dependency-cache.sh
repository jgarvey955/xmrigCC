#!/bin/sh

dependency_is_current() {
    dependency_name="$1"
    dependency_version="$2"
    shift 2

    dependency_stamp="${DEPS_DIR}/.versions/${dependency_name}"
    [ -f "$dependency_stamp" ] || return 1
    [ "$(sed -n '1p' "$dependency_stamp")" = "$dependency_version" ] || return 1

    for dependency_artifact in "$@"; do
        [ -e "$dependency_artifact" ] || return 1
    done

    printf '%s %s is already built; skipping.\n' "$dependency_name" "$dependency_version"
    return 0
}

dependency_mark_current() {
    dependency_name="$1"
    dependency_version="$2"
    mkdir -p "${DEPS_DIR}/.versions"
    printf '%s\n' "$dependency_version" > "${DEPS_DIR}/.versions/${dependency_name}"
}
