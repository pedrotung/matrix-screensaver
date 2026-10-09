#!/bin/sh
# Build MatrixRain.saver (universal: Apple silicon + Intel). Needs Xcode command line tools.
set -e
cd "$(dirname "$0")"
OUT=build/MatrixRain.saver
rm -rf build
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"
cp Info.plist "$OUT/Contents/Info.plist"
clang++ -std=c++17 -O2 -fobjc-arc -bundle \
  -arch arm64 -arch x86_64 -mmacosx-version-min=10.15 \
  -framework ScreenSaver -framework Cocoa \
  -o "$OUT/Contents/MacOS/MatrixRain" MatrixRainView.mm
# Ad-hoc signature: required for Apple silicon. Not notarized.
codesign --force --deep --sign - "$OUT"
(cd build && ditto -c -k --keepParent MatrixRain.saver MatrixRain-macos.zip)
ls -l build
