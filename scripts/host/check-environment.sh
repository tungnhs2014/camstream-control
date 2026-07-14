#!/usr/bin/env bash
# Read-only Stage 1 host readiness check for CamStream Control.

set -u
set -o pipefail

required_passed=0
required_failed=0
recommended_missing=0
optional_missing=0

usage() {
    printf 'Usage: %s\n' "${0##*/}"
}

report_ok() {
    printf '[OK] %s\n' "$1"
}

report_info() {
    printf '[INFO] %s\n' "$1"
}

report_warning() {
    printf '[WARNING] %s\n' "$1"
}

record_present() {
    local category=$1
    local description=$2

    report_ok "$description"
    if [[ $category == required ]]; then
        required_passed=$((required_passed + 1))
    fi
}

record_missing() {
    local category=$1
    local description=$2

    case $category in
        required)
            printf '[MISSING][REQUIRED] %s\n' "$description"
            required_failed=$((required_failed + 1))
            ;;
        recommended)
            printf '[MISSING][RECOMMENDED] %s\n' "$description"
            recommended_missing=$((recommended_missing + 1))
            ;;
        optional)
            printf '[MISSING][OPTIONAL] %s\n' "$description"
            optional_missing=$((optional_missing + 1))
            ;;
    esac
}

check_command() {
    local category=$1
    local command_name=$2

    if command -v "$command_name" >/dev/null 2>&1; then
        record_present "$category" "$command_name: $(command -v "$command_name")"
    else
        record_missing "$category" "$command_name"
    fi
}

check_development_capability() {
    local label=$1
    local pkg_config_name=$2
    local package_name=$3
    local version

    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists "$pkg_config_name"; then
        version=$(pkg-config --modversion "$pkg_config_name" 2>/dev/null || true)
        record_present required "$label: pkg-config $pkg_config_name ${version:+($version)}"
    elif command -v dpkg-query >/dev/null 2>&1 && dpkg-query -W -f='${db:Status-Abbrev}' "$package_name" 2>/dev/null | grep -q '^ii '; then
        record_present required "$label: package $package_name installed"
    else
        record_missing required "$label (pkg-config $pkg_config_name or package $package_name)"
    fi
}

check_gstreamer_element() {
    local element=$1

    if command -v gst-inspect-1.0 >/dev/null 2>&1 && gst-inspect-1.0 "$element" >/dev/null 2>&1; then
        record_present recommended "GStreamer element: $element"
    else
        record_missing recommended "GStreamer element: $element"
    fi
}

if (( $# != 0 )); then
    usage
    exit 2
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repository_root=$(cd -- "$script_dir/../.." && pwd)

printf 'CamStream Control Stage 1 host environment check\n\n'

if [[ -r /etc/os-release ]]; then
    os_name=$(sed -n 's/^PRETTY_NAME="\(.*\)"$/\1/p' /etc/os-release | head -n 1)
    report_info "Operating system: ${os_name:-unknown}"
else
    report_warning 'Operating system release information is unavailable'
fi

if command -v uname >/dev/null 2>&1; then
    report_info "Architecture: $(uname -m)"
else
    report_warning 'Architecture detection is unavailable'
fi

if command -v free >/dev/null 2>&1; then
    available_ram=$(free -m | awk '/^Mem:/ { print $7 " MiB" }')
    report_info "Available RAM: ${available_ram:-unknown}"
else
    report_warning 'Available RAM detection is unavailable'
fi

if command -v df >/dev/null 2>&1; then
    available_disk=$(df -hP "$repository_root" | awk 'NR == 2 { print $4 " available on " $1 }')
    report_info "Repository filesystem: ${available_disk:-unknown}"
else
    report_warning 'Repository filesystem detection is unavailable'
fi

printf '\nCore required tools\n'
for tool in git gcc g++ make cmake python3 perl pkg-config; do
    check_command required "$tool"
done

printf '\nBuildroot host utilities\n'
for tool in patch gzip bzip2 xz tar cpio unzip rsync file bc find awk wget sed diff; do
    check_command required "$tool"
done

printf '\nKernel, U-Boot, and Device Tree tools\n'
for tool in flex bison dtc; do
    check_command required "$tool"
done
check_development_capability 'ncurses development capability' ncursesw libncurses-dev
check_development_capability 'OpenSSL development capability' openssl libssl-dev
check_development_capability 'libelf development capability' libelf libelf-dev

printf '\nUART and target access\n'
for tool in minicom lsusb ssh scp; do
    check_command required "$tool"
done
check_command optional picocom
if id -nG 2>/dev/null | tr ' ' '\n' | grep -qx dialout; then
    report_info 'Stage 2 prerequisite: current user is in dialout (not a Stage 1 check)'
else
    report_info 'Stage 2 prerequisite: current user is not in dialout (not a Stage 1 failure)'
fi

printf '\nCamera and multimedia\n'
for tool in v4l2-ctl media-ctl gst-launch-1.0 gst-inspect-1.0; do
    check_command required "$tool"
done
for element in v4l2src udpsrc rtpjpegdepay; do
    check_gstreamer_element "$element"
done

printf '\nDebug and quality\n'
check_command required shellcheck
for tool in gdb strace valgrind cppcheck clang-format clang-tidy; do
    check_command recommended "$tool"
done
check_command optional ltrace

printf '\nSummary\n'
printf '[INFO] Required checks passed: %d\n' "$required_passed"
printf '[INFO] Required checks failed: %d\n' "$required_failed"
printf '[INFO] Recommended checks missing: %d\n' "$recommended_missing"
printf '[INFO] Optional checks missing: %d\n' "$optional_missing"

if (( required_failed == 0 )); then
    report_ok 'Stage 1 host-readiness result: PASS'
    exit 0
fi

printf '[MISSING][REQUIRED] Stage 1 host-readiness result: FAIL\n'
exit 1
