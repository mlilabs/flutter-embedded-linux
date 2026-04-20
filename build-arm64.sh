#!/bin/bash

ENGINE_VERSION=cb4b5fff73

set -e
cd -- "$( dirname -- "${BASH_SOURCE[0]}" )"

# Download Flutter engine
mkdir -p engine/$ENGINE_VERSION/arm64
pushd engine/$ENGINE_VERSION/arm64
if ! [ -f libflutter_engine.so ] ; then
    wget https://github.com/sony/flutter-embedded-linux/releases/download/$ENGINE_VERSION/elinux-arm64-release.zip
    unzip elinux-arm64-release.zip libflutter_engine.so
fi
popd

# Build Flutter embedder
BUILD_DIR=${BUILD_DIR:-build-arm64}
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake -DCMAKE_TOOLCHAIN_FILE=../cross-toolchain-aarch64-raumscan.cmake \
      -DBUILD_ELINUX_SO=ON \
      -DBACKEND_TYPE=WAYLAND \
      -DCMAKE_BUILD_TYPE=Release \
      -DFLUTTER_RELEASE=ON \
      -DFLUTTER_EMBEDDER_LIB="$PWD/../engine/$ENGINE_VERSION/arm64/libflutter_engine.so" \
      ..
cmake --build . --parallel

# Install Flutter embedder
cp -fv libflutter_elinux_wayland.so ~/flutter-elinux/flutter/bin/cache/artifacts/engine/elinux-arm64-debug/
cp -fv libflutter_elinux_wayland.so ~/flutter-elinux/flutter/bin/cache/artifacts/engine/elinux-arm64-profile/
cp -fv libflutter_elinux_wayland.so ~/flutter-elinux/flutter/bin/cache/artifacts/engine/elinux-arm64-release/

cp -fv libflutter_elinux_wayland.so /workspaces/raumscan/apps/raumscan/flutter-elinux-engine-lib/aarch64/
