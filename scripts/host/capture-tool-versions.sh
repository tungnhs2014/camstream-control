#!/usr/bin/env bash
# Print host and tool versions to standard output; never creates a report file.

set -u
set -o pipefail

usage() {
    printf 'Usage: %s\n' "${0##*/}"
}

print_command_version() {
    local label=$1
    local command_name=$2
    local output
    shift 2

    if ! command -v "$command_name" >/dev/null 2>&1; then
        printf '%s: unavailable\n' "$label"
        return
    fi

    output=$("$command_name" "$@" 2>&1 || true)
    output=$(printf '%s\n' "$output" | sed -n '1p')
    printf '%s: %s\n' "$label" "${output:-available; version output unavailable}"
}

print_development_version() {
    local label=$1
    local pkg_config_name=$2
    local package_name=$3
    local version

    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists "$pkg_config_name"; then
        version=$(pkg-config --modversion "$pkg_config_name" 2>/dev/null || true)
        printf '%s: pkg-config %s\n' "$label" "$version"
    elif command -v dpkg-query >/dev/null 2>&1; then
        version=$(dpkg-query -W -f='${Version}' "$package_name" 2>/dev/null || true)
        printf '%s: %s\n' "$label" "${version:-unavailable}"
    else
        printf '%s: unavailable\n' "$label"
    fi
}

if (( $# != 0 )); then
    usage
    exit 2
fi

printf 'CamStream Control host tool versions\n\n'

if [[ -r /etc/os-release ]]; then
    os_name=$(sed -n 's/^PRETTY_NAME="\(.*\)"$/\1/p' /etc/os-release | head -n 1)
    printf 'Ubuntu release: %s\n' "${os_name:-unavailable}"
else
    printf 'Ubuntu release: unavailable\n'
fi
print_command_version 'Host kernel' uname -r
print_command_version 'Architecture' uname -m
if command -v getconf >/dev/null 2>&1; then
    printf 'CPU count: %s\n' "$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf unavailable)"
else
    printf 'CPU count: unavailable\n'
fi
if command -v free >/dev/null 2>&1; then
    free -h | sed 's/^/RAM: /'
else
    printf 'RAM: unavailable\n'
fi
if command -v df >/dev/null 2>&1; then
    df -hP . | awk 'NR == 1 { print "Disk: " $0 } NR == 2 { print "Disk: " $0 }'
else
    printf 'Disk: unavailable\n'
fi

printf '\nCore development tools\n'
print_command_version git git --version
print_command_version gcc gcc --version
print_command_version g++ g++ --version
print_command_version make make --version
print_command_version cmake cmake --version
print_command_version ninja ninja --version
print_command_version python3 python3 --version
print_command_version perl perl -e 'print "$^V\n"'
print_command_version pkg-config pkg-config --version

printf '\nBuild and platform tools\n'
print_command_version patch patch --version
print_command_version cpio cpio --version
print_command_version rsync rsync --version
print_command_version file file --version
print_command_version bc bc --version
print_command_version awk awk --version
print_command_version wget wget --version
print_command_version flex flex --version
print_command_version bison bison --version
print_command_version dtc dtc --version
print_development_version 'OpenSSL development' openssl libssl-dev
print_development_version 'ncurses development' ncursesw libncurses-dev
print_development_version 'libelf development' libelf libelf-dev

printf '\nAccess, multimedia, and quality tools\n'
print_command_version minicom minicom --version
print_command_version picocom picocom --version
print_command_version lsusb lsusb --version
print_command_version ssh ssh -V
print_command_version v4l2-ctl v4l2-ctl --version
print_command_version media-ctl media-ctl --version
print_command_version GStreamer gst-launch-1.0 --version
print_command_version gdb gdb --version
print_command_version strace strace --version
print_command_version ltrace ltrace --version
print_command_version valgrind valgrind --version
print_command_version cppcheck cppcheck --version
print_command_version clang-format clang-format --version
print_command_version clang-tidy clang-tidy --version
print_command_version shellcheck shellcheck --version

printf '\nOptional unavailable tools are reported above and do not make this script fail.\n'
