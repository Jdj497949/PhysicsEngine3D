#!/usr/bin/env bash
# build_mac.sh — 在 macOS 上用同一套源码编译 VNLauncher.app
# 依赖：brew install cmake qt
set -e
cd "$(dirname "$0")"
BUILD=build-mac

cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt 2>/dev/null || echo /opt/homebrew/opt/qt6)"
cmake --build "$BUILD" --target VNLauncher -j$(sysctl -n hw.ncpu)

# 打成 .app 包
APP=VNLauncher.app
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BUILD/VNLauncher" "$APP/Contents/MacOS/VNLauncher"
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>VNLauncher</string>
  <key>CFBundleIdentifier</key><string>com.physicsengine3d.vnlauncher</string>
  <key>CFBundleName</key><string>NovaEngine3D Launcher</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>1.0.0</string>
</dict></plist>
PLIST

echo "=============================================="
echo "[BUILD] macOS build done:"
ls -lh "$APP/Contents/MacOS/VNLauncher"
echo "[BUILD] Deploy Qt frameworks next:"
echo "  \$(brew --prefix qt)/bin/macos-notarization || macdeployqt $APP"
