#!/bin/bash
# Builds the Mac release of LoopsMaster and zips it.
# Run on a Mac with Xcode and CMake installed:  bash tools/build-mac.sh
#
# Signing (optional, needed for public release): set these before running.
#   SIGN_ID       "Developer ID Application: Your Name (TEAMID)"
#   NOTARY_PROFILE  a notarytool keychain profile, made once with:
#                   xcrun notarytool store-credentials <name> --apple-id <id> --team-id <TEAMID>
# Without them the plugin is only ad-hoc signed and other Macs will block it after download.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
VERSION=$(sed -n 's/^project(LoopsMaster VERSION \([0-9.]*\)).*/\1/p' CMakeLists.txt)

if [ ! -f external/vst3sdk/CMakeLists.txt ]; then
    git clone --recursive --depth 1 --branch v3.8.1_build_84 \
        https://github.com/steinbergmedia/vst3sdk.git external/vst3sdk
fi

cmake -S . -B build-mac -G Xcode
cmake --build build-mac --config Release --target LoopsMaster

BUNDLE="$ROOT/build-mac/VST3/Release/LoopsMaster.vst3"
lipo -info "$BUNDLE/Contents/MacOS/LoopsMaster"

if [ -n "${SIGN_ID:-}" ]; then
    codesign --force --deep --timestamp --options runtime --sign "$SIGN_ID" "$BUNDLE"
else
    codesign --force --deep --sign - "$BUNDLE"
fi
codesign --verify --deep --strict "$BUNDLE"

STAGE="$ROOT/dist/stage-mac/LoopsMaster $VERSION Mac"
rm -rf "$ROOT/dist/stage-mac"; mkdir -p "$STAGE"
ditto "$BUNDLE" "$STAGE/LoopsMaster.vst3"
cp "$ROOT/dist/mac/READ ME FIRST.txt" "$ROOT/dist/THIRD-PARTY-NOTICES.txt" "$STAGE/"

ZIP="$ROOT/dist/LoopsMaster-$VERSION-Mac.zip"
rm -f "$ZIP"
ditto -c -k --keepParent "$STAGE" "$ZIP"

if [ -n "${SIGN_ID:-}" ] && [ -n "${NOTARY_PROFILE:-}" ]; then
    xcrun notarytool submit "$ZIP" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$STAGE/LoopsMaster.vst3"
    rm -f "$ZIP"; ditto -c -k --keepParent "$STAGE" "$ZIP"
fi
rm -rf "$ROOT/dist/stage-mac"
echo "Made $ZIP"
