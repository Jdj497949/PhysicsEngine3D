#!/bin/bash
cd /root/workspace/a626663e-516e-4b27-ba62-f7513cbf2d7d/PhysicsEngine3D
export LIBGL_ALWAYS_SOFTWARE=1 DISPLAY=:99
pkill -9 Xvfb 2>/dev/null; sleep 1
Xvfb :99 -screen 0 1600x900x24 &
sleep 2
./build/3DLauncher &
sleep 2
# 双击第一行项目 → QProcess::startDetached 拉起编辑器
xdotool mousemove 400 27 click --repeat 2 1
sleep 4
echo "=== QProcess 拉起验证：进程树 ==="
ps -ef | grep -E "3D(Launcher|Editor)" | grep -v grep
