#!/bin/bash
cd /root/workspace/a626663e-516e-4b27-ba62-f7513cbf2d7d/PhysicsEngine3D
export LIBGL_ALWAYS_SOFTWARE=1 DISPLAY=:99
pkill -9 -f "build/3D" 2>/dev/null; sleep 1
pgrep Xvfb >/dev/null || { Xvfb :99 -screen 0 1600x900x24 & sleep 2; }

# 真实流程：3DLauncher 保持运行，编辑器由它 QProcess 拉起
./build/3DLauncher &
sleep 2
# 双击列表里第一个项目 → 启动器内部 QProcess::startDetached 拉起 3DEditor
xdotool search --name "3DLauncher" > /dev/null 2>&1
xdotool mousemove 400 27 click --repeat 2 1
sleep 4

# 终端窗口：ls -lh 双程序 + ps 进程共存
xterm -T "Terminal - PhysicsEngine3D" -geometry 96x22+20+40 -e bash -c '
echo "=== ls -lh: 双可执行文件 ==="
ls -lh build/3DLauncher build/3DEditor
echo
echo "=== ps: 3DLauncher 与 3DEditor 进程共存 ==="
ps -ef | grep -E "3D(Launcher|Editor)" | grep -v grep
echo
echo "（3DLauncher 保持运行中，3DEditor 由启动器 QProcess 拉起）"
sleep 8' &
sleep 3
# 把终端提到最前再截屏
WID=$(xdotool search --name "Terminal - PhysicsEngine3D" | head -1)
[ -n "$WID" ] && xdotool windowactivate $WID && sleep 1
import -window root shots/04_terminal.png
sleep 6
pkill -9 -f "build/3D" 2>/dev/null
pkill Xvfb 2>/dev/null
