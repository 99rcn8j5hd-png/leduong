#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build artifacts
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
for arch in arm64 arm64e; do
  mkdir -p "build/$arch"
  for unit in fname objects; do
    xcrun clang++ -isysroot "$SDK" -arch "$arch" -miphoneos-version-min=15.0 -std=c++17 -O2 -Isrc -c "src/$unit.cpp" -o "build/$arch/$unit.o"
  done
  xcrun clang++ -isysroot "$SDK" -arch "$arch" -miphoneos-version-min=15.0 -std=c++17 -O2 -fobjc-arc -Isrc -c src/probe.mm -o "build/$arch/probe.o"
  xcrun clang++ -isysroot "$SDK" -arch "$arch" -miphoneos-version-min=15.0 -dynamiclib "build/$arch/fname.o" "build/$arch/objects.o" "build/$arch/probe.o" -framework Foundation -framework UIKit -Wl,-install_name,@rpath/ZKProbe.dylib -o "build/$arch/ZKProbe.dylib"
done
xcrun lipo -create build/arm64/ZKProbe.dylib build/arm64e/ZKProbe.dylib -output artifacts/ZKProbe.dylib
ldid -S artifacts/ZKProbe.dylib
xcrun lipo -info artifacts/ZKProbe.dylib
shasum -a 256 artifacts/ZKProbe.dylib > artifacts/SHA256.txt
