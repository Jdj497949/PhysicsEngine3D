#!/bin/bash
cd /root/workspace/a626663e-516e-4b27-ba62-f7513cbf2d7d/PhysicsEngine3D
export LIBGL_ALWAYS_SOFTWARE=1 DISPLAY=:99
pkill -9 Xvfb; pkill -9 xterm; sleep 1
Xvfb :99 -screen 0 1600x900x24 &
sleep 2

# 两个程序同时运行：3DLauncher + 3DEditor（项目路径参数）
./build/3DLauncher >/dev/null 2>&1 &
sleep 1
./build/3DEditor ~/Physics3DProjects/Demo火锅店项目 >/dev/null 2>&1 &
sleep 3

xterm -T "TermShot" -geometry 110x20+10+10 -e bash -c '
echo "=== ls -lh: 双可执行文件 ==="
ls -lh build/3DLauncher build/3DEditor
echo
echo "=== ps -ef: 3DLauncher 与 3DEditor 进程共存 ==="
ps -ef | grep -E "3D(Launcher|Editor)" | grep -v grep
sleep 25' &
sleep 4
WID=$(xdotool search --name "TermShot" | head -1)
import -window "$WID" shots/04_terminal.png
identify shots/04_terminal.png
pkill -9 xterm; pkill -9 -f "build/3DLauncher"; pkill -9 -f "build/3DEditor"; pkill -9 Xvfb
