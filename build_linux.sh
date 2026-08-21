#!/bin/bash
# Set KTP_NO_STAGE=1 to build WITHOUT copying into the local test tree -- staging
# overwrites an artifact whose md5 may be pinned to a reviewed build.
# KTPAmxxCurl Linux Build Script
# Run via WSL: wsl bash -c "cd '/mnt/n/Nein_/KTP Git Projects/KTPAmxxCurl' && bash build_linux.sh"

set -e

# A failed build must be VISIBLE, not merely non-zero. Callers pipe this script
# (`| tail`, `| tee`), and the shell then reports the PIPE's status -- so a failed
# build reads as exit 0 unless the log itself says so. Gate on the banners below,
# never on the exit code.
_ktp_build_exit() {
    local rc=$?
    [ -n "${BUILD_STAMP:-}" ] && rm -f "$BUILD_STAMP"
    if [ "$rc" -ne 0 ]; then
        echo ""
        echo "========================================"
        echo "[KTP-BUILD] FAILED: KTPAmxxCurl build_linux.sh exited $rc"
        echo "========================================"
        echo "Nothing has been staged."
    fi
    exit "$rc"
}
trap _ktp_build_exit EXIT


echo "========================================"
echo "KTPAmxxCurl Linux Build Script (CMake)"
echo "========================================"

# Check for required tools
for tool in cmake make gcc; do
    if ! command -v $tool &> /dev/null; then
        echo "ERROR: $tool is not installed"
        exit 1
    fi
done

# Reference mtime: the artifact must be NEWER than this, so a stale .so left in
# build/ can never be mistaken for a fresh one. `set -e` alone already stops a
# failed make here, but it does so SILENTLY -- no banner, and the "BUILD FAILED!"
# branch below was unreachable. A failed build that prints nothing reads like a
# short success in a scrollback, which is how a stale artifact gets shipped.
BUILD_STAMP="$(mktemp)"

# Clean and build
rm -rf build
mkdir -p build
cd build
set +e
cmake .. -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
CMAKE_RC=$?
if [ "$CMAKE_RC" -eq 0 ]; then
    make -j$(nproc)
    BUILD_RC=$?
else
    BUILD_RC=$CMAKE_RC
fi
set -e
cd ..

if [ "$BUILD_RC" -ne 0 ]; then
    echo ""
    echo "========================================"
    echo "BUILD FAILED! (exit $BUILD_RC)"
    echo "========================================"
    echo "Nothing has been staged."
    exit 1
fi

# Check if build succeeded -- on the ARTIFACT, and on it being ours.
BINARY_PATH="build/amxxcurl_ktp_i386.so"
if [ -f "$BINARY_PATH" ] && [ "$BINARY_PATH" -nt "$BUILD_STAMP" ]; then
    echo ""
    echo "========================================"
    echo "BUILD SUCCESS!"
    echo "========================================"
    echo "Binary: $BINARY_PATH"
    ls -lh "$BINARY_PATH"

    # Deploy to staging folder. Overridable so a test run can stage somewhere
    # harmless instead of over an artifact whose md5 is pinned to a shipped build.
    DEPLOY_DIR="${KTP_STAGING_DIR:-/mnt/n/Nein_/KTP Git Projects/KTP DoD Server/serverfiles}"
    if [ -d "$DEPLOY_DIR" ]; then
        echo ""
        if [ -n "${KTP_NO_STAGE:-}" ]; then
            echo "Staging SKIPPED (KTP_NO_STAGE set)."
            echo "  Binary left at: $BINARY_PATH"
        else
            echo "Deploying to staging folder..."
            mkdir -p "$DEPLOY_DIR/dod/addons/ktpamx/modules"
            if ! cp "$BINARY_PATH" "$DEPLOY_DIR/dod/addons/ktpamx/modules/"; then
                echo "ERROR: failed to copy $BINARY_PATH into the staging tree."
                exit 1
            fi
            echo "  -> Copied amxxcurl_ktp_i386.so  (md5 $(md5sum "$BINARY_PATH" | cut -d' ' -f1))"
            echo ""
            echo "Files staged at: $DEPLOY_DIR/dod/addons/ktpamx/modules/"
        fi
    fi
else
    echo ""
    echo "========================================"
    echo "BUILD FAILED!"
    echo "========================================"
    if [ -f "$BINARY_PATH" ]; then
        echo "$BINARY_PATH exists but predates this run -- the compile did not"
        echo "produce it. Refusing to stage a stale artifact."
    else
        echo "No amxxcurl_ktp_i386.so was produced."
    fi
    echo "Nothing has been staged."
    exit 1
fi

# Success sentinel, last line on the only path that reaches here. A caller checks
# for this rather than for `$?`, which a pipe launders.
echo "[KTP-BUILD] OK: KTPAmxxCurl build_linux.sh"
