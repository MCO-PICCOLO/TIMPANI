#!/bin/bash
# SPDX-FileCopyrightText: Copyright 2026 LG Electronics Inc.
# SPDX-License-Identifier: MIT
#
# build.sh — Build Timpani release artifacts
#
#   timpani-n → native package (.deb via CPack on Ubuntu, .rpm via CPack on CentOS)
#   timpani-o → Podman container image
#
# Usage:
#   ./scripts/build.sh n        # build timpani-n package only (.deb or .rpm, auto-detected)
#   ./scripts/build.sh o        # build timpani-o container image only
#   ./scripts/build.sh all      # build both (default)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${REPO_ROOT}/dist"
TARGET="${1:-all}"

TIMPANI_O_VERSION="0.1.0"

log() { echo "[build.sh] $*"; }

detect_os() {
    if [[ -f /etc/os-release ]]; then
        # shellcheck disable=SC1091
        source /etc/os-release
        echo "${ID}"
    else
        echo "unknown"
    fi
}

# ---------------------------------------------------------------------------
# timpani-n — native package via CPack
# ---------------------------------------------------------------------------
build_timpani_n() {
    local os
    os=$(detect_os)
    local generator="DEB"

    # CPack's RPM generator only needs the `rpmbuild` tool, which is available
    # on any distro (e.g. `apt install rpm` on Ubuntu/Debian) — a CentOS/RHEL
    # host is NOT required to produce a .rpm.
    if command -v rpmbuild &>/dev/null; then
        generator="DEB;RPM"
    else
        log "rpmbuild not found — building .deb only. Install it to also get" \
            " a .rpm (Ubuntu/Debian: 'sudo apt-get install rpm')."
    fi

    log "Building timpani-n for OS '${os}' (generator: ${generator})..."
    mkdir -p "${REPO_ROOT}/timpani-n/build"
    (
        cd "${REPO_ROOT}/timpani-n/build"
        cmake ..
        make -j"$(nproc)"
        cpack -G "${generator}"
    )

    mkdir -p "${DIST_DIR}"
    find "${REPO_ROOT}/timpani-n/build" -maxdepth 1 \( -name "*.deb" -o -name "*.rpm" \) -exec cp {} "${DIST_DIR}/" \;
    log "timpani-n package(s) copied to ${DIST_DIR}/"
}

# ---------------------------------------------------------------------------
# timpani-o — Podman container image
# ---------------------------------------------------------------------------
build_timpani_o() {
    log "Building timpani-o container image (version ${TIMPANI_O_VERSION})..."
    podman build \
        --build-arg "VERSION=${TIMPANI_O_VERSION}" \
        -f "${REPO_ROOT}/timpani-o/Containerfile" \
        -t "timpani-o:${TIMPANI_O_VERSION}" \
        "${REPO_ROOT}"

    mkdir -p "${DIST_DIR}"
    log "Saving image to ${DIST_DIR}/timpani-o-${TIMPANI_O_VERSION}.tar (for offline transfer)..."
    podman save "timpani-o:${TIMPANI_O_VERSION}" -o "${DIST_DIR}/timpani-o-${TIMPANI_O_VERSION}.tar"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
case "${TARGET}" in
    n) build_timpani_n ;;
    o) build_timpani_o ;;
    all)
        build_timpani_n
        build_timpani_o
        ;;
    *)
        echo "Usage: $0 [n|o|all]"
        exit 1
        ;;
esac

log "Done. Artifacts in: ${DIST_DIR}/"
ls -lh "${DIST_DIR}/" 2>/dev/null || true
