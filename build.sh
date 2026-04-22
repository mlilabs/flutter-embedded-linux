#!/bin/bash
#
# Build the Flutter embedded-linux engine wrapper (libflutter_elinux_wayland.so)
# for x64 and/or arm64 in debug, profile, and release variants, and install
# them alongside headers and C++ client wrapper sources into the flutter-elinux
# cache so the runner compiles against the matching ABI.
#
# Usage:
#   ./build.sh                              build all arches, all flavors (default)
#   ./build.sh [x64|arm64|all]              build all flavors for given arch
#   ./build.sh [x64|arm64] [debug|profile|release|all]

ENGINE_VERSION=cb4b5fff73

set -e
cd -- "$( dirname -- "${BASH_SOURCE[0]}" )"

CACHE_DIR=~/flutter-elinux/flutter/bin/cache/artifacts/engine
COMMON_DIR="$CACHE_DIR/elinux-common"

# -----------------------------------------------------------------------------
# build_arch_flavor <arch> <flavor>
#
# arch:   x64 | arm64
# flavor: debug | profile | release
# -----------------------------------------------------------------------------
build_arch_flavor() {
  local arch="$1"
  local flavor="$2"
  local engine_dir="engine/$ENGINE_VERSION/$arch/$flavor"
  local build_dir="build-$arch-$flavor"

  # CMake settings per flavor:
  #   debug   – unoptimized wrapper, JIT engine (assertions on)
  #   profile – optimized wrapper, JIT engine with profiling support
  #   release – optimized wrapper, AOT engine (FLUTTER_RELEASE=ON)
  local cmake_build_type flutter_release
  case "$flavor" in
    debug)   cmake_build_type=Debug;   flutter_release=OFF ;;
    profile) cmake_build_type=Release; flutter_release=OFF ;;
    release) cmake_build_type=Release; flutter_release=ON  ;;
  esac

  # Download prebuilt Flutter engine for this flavor.
  mkdir -p "$engine_dir"
  if ! [ -f "$engine_dir/libflutter_engine.so" ]; then
    (cd "$engine_dir" && \
      wget "https://github.com/sony/flutter-embedded-linux/releases/download/$ENGINE_VERSION/elinux-$arch-$flavor.zip" && \
      unzip "elinux-$arch-$flavor.zip" libflutter_engine.so)
  fi

  # Configure toolchain.
  local -a cmake_args=(
    -DBUILD_ELINUX_SO=ON
    -DBACKEND_TYPE=WAYLAND
    "-DCMAKE_BUILD_TYPE=$cmake_build_type"
    "-DFLUTTER_RELEASE=$flutter_release"
    "-DFLUTTER_EMBEDDER_LIB=$PWD/$engine_dir/libflutter_engine.so"
  )
  if [ "$arch" = "arm64" ]; then
    cmake_args+=(-DCMAKE_TOOLCHAIN_FILE=../cross-toolchain-aarch64-raumscan.cmake)
  fi

  # Build.
  mkdir -p "$build_dir"
  (cd "$build_dir" && cmake "${cmake_args[@]}" .. && cmake --build . --parallel)

  # Install the .so into the matching cache directory.
  local dest="$CACHE_DIR/elinux-$arch-$flavor"
  mkdir -p "$dest"
  cp -fv "$build_dir/libflutter_elinux_wayland.so" "$dest/"
}

# -----------------------------------------------------------------------------
# build_arch <arch> [flavor]
#
# Builds all flavors (or a specific one) for the given arch.
# -----------------------------------------------------------------------------
build_arch() {
  local arch="$1"
  local flavor="${2:-all}"

  if [ "$flavor" = "all" ]; then
    build_arch_flavor "$arch" debug
    build_arch_flavor "$arch" profile
    build_arch_flavor "$arch" release
  else
    build_arch_flavor "$arch" "$flavor"
  fi
}

# -----------------------------------------------------------------------------
# install_common
#
# Install embedder headers and C++ client wrapper sources (arch-independent)
# into the flutter-elinux cache so the runner compiles against the same ABI
# as the .so files installed above.
# -----------------------------------------------------------------------------
install_common() {
  mkdir -p "$COMMON_DIR/cpp_client_wrapper/include/flutter"

  # Public C headers.
  cp -fv \
    src/flutter/shell/platform/linux_embedded/public/flutter_elinux.h \
    src/flutter/shell/platform/linux_embedded/public/flutter_platform_views.h \
    src/flutter/shell/platform/common/public/flutter_export.h \
    src/flutter/shell/platform/common/public/flutter_messenger.h \
    src/flutter/shell/platform/common/public/flutter_plugin_registrar.h \
    src/flutter/shell/platform/common/public/flutter_texture_registrar.h \
    "$COMMON_DIR/"

  # C++ client wrapper headers (elinux-specific + shared with desktop common).
  cp -fv \
    src/client_wrapper/include/flutter/*.h \
    src/flutter/shell/platform/common/client_wrapper/include/flutter/*.h \
    "$COMMON_DIR/cpp_client_wrapper/include/flutter/"

  # C++ client wrapper sources and private headers (compiled into the runner;
  # must match headers).
  cp -fv \
    src/client_wrapper/*.cc \
    src/flutter/shell/platform/common/client_wrapper/*.cc \
    src/flutter/shell/platform/common/client_wrapper/*.h \
    "$COMMON_DIR/cpp_client_wrapper/"
}

# -----------------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------------
arch_arg="${1:-all}"
flavor_arg="${2:-all}"

case "$arch_arg" in
  x64)   build_arch x64   "$flavor_arg" ;;
  arm64) build_arch arm64 "$flavor_arg" ;;
  all)   build_arch x64   "$flavor_arg" ; build_arch arm64 "$flavor_arg" ;;
  *)     echo "Usage: $0 [x64|arm64|all] [debug|profile|release|all]" >&2 ; exit 1 ;;
esac

install_common
