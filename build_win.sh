#!/usr/bin/env bash
# build_win.sh — 用同一套源码交叉编译 Windows 版 VNLauncher（.exe）
# 依赖：apt-get install -y g++-mingw-w64-x86-64 cmake
# Qt6 交叉编译环境沿用工程既有 cmake/windows-mingw.cmake 工具链
set -e
cd "$(dirname "$0")"
TOOLCHAIN=cmake/windows-mingw.cmake
BUILD=build-win-launcher

cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DTARGET_TRIPLET=x86_64-w64-mingw32
cmake --build "$BUILD" --target VNLauncher -j4

echo "=============================================="
echo "[BUILD] Windows build done:"
ls -lh "$BUILD"/VNLauncher.exe
echo "[BUILD] Deploy Qt DLLs next:"
echo "  windeployqt-qt6 --release $BUILD/VNLauncher.exe"
echo "  (需带 Qt6Network.dll / Qt6Widgets.dll / platforms/qwindows)"
