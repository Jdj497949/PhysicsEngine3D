#!/bin/bash
cd /root/workspace/a626663e-516e-4b27-ba62-f7513cbf2d7d/PhysicsEngine3D
export LIBGL_ALWAYS_SOFTWARE=1
pkill Xvfb 2>/dev/null; sleep 1
Xvfb :99 -screen 0 1600x900x24 &
sleep 2
export DISPLAY=:99

# 演示流程1：启动器创建项目（截图1）
env 3D_AUTO_NAME=Demo火锅店项目 3D_SHOT=$PWD/shots/01_launcher.png ./build/3DLauncher &
sleep 5
pkill -f build/3DLauncher 2>/dev/null
sleep 1

# 编辑器UI（截图2）
env 3D_AUTO_MODE=ui 3D_SHOT=$PWD/shots/02_editor_ui.png ./build/3DEditor ~/Physics3DProjects/Demo火锅店项目 &
sleep 6
pkill -f build/3DEditor 2>/dev/null
sleep 1

# 物理测试100方块（截图3）
env 3D_AUTO_MODE=physics 3D_SHOT=$PWD/shots/03_physics.png ./build/3DEditor ~/Physics3DProjects/Demo火锅店项目 &
sleep 8
pkill -f build/3DEditor 2>/dev/null
sleep 1

# 终端：ls -lh + 进程共存（截图4）——同时留一个3DEditor真在跑
./build/3DEditor ~/Physics3DProjects/Demo火锅店项目 >/dev/null 2>&1 &
sleep 2
xterm -T "Terminal - PhysicsEngine3D" -geometry 100x26+30+30 -e bash -c "echo '=== ls -lh 双程序产物 ==='; ls -lh build/3DLauncher build/3DEditor; echo; echo '=== 进程共存 ==='; ps -ef | grep -E '3D(Launcher|Editor)' | grep -v grep; sleep 6" &
sleep 8
import -window root $PWD/shots/04_terminal.png
pkill -f build/3DEditor 2>/dev/null
pkill Xvfb 2>/dev/null
