#!/bin/bash
cd /root/workspace/a626663e-516e-4b27-ba62-f7513cbf2d7d/PhysicsEngine3D
export LIBGL_ALWAYS_SOFTWARE=1
pkill -9 Xvfb 2>/dev/null; sleep 1
Xvfb :99 -screen 0 1600x900x24 &
sleep 2
export DISPLAY=:99
rm -f ~/.config/PhysicsEngine3D/3DEditor.conf

# 场景+Gizmo：两次截图 + 射线日志（输出重定向到 editor_log.txt）
env 3D_AUTO_MODE=scene 3D_SHOT=$PWD/shots/05_scene.png ./build/3DEditor ~/Physics3DProjects/Demo火锅店项目 > editor_log.txt 2>&1 &
sleep 6
pkill -9 -f "build/3DEditor" 2>/dev/null; sleep 1
rm -f ~/.config/PhysicsEngine3D/3DEditor.conf

# 设置窗口
env 3D_AUTO_MODE=settings 3D_SHOT=$PWD/shots/06_settings.png ./build/3DEditor ~/Physics3DProjects/Demo火锅店项目 >/dev/null 2>&1 &
sleep 4
pkill -9 -f "build/3DEditor" 2>/dev/null; sleep 1
rm -f ~/.config/PhysicsEngine3D/3DEditor.conf

# 重力 +9.81：方块往天上飞
env 3D_AUTO_MODE=fly 3D_SHOT=$PWD/shots/07_fly.png ./build/3DEditor ~/Physics3DProjects/Demo火锅店项目 >/dev/null 2>&1 &
sleep 6
pkill -9 -f "build/3DEditor" 2>/dev/null; sleep 1
rm -f ~/.config/PhysicsEngine3D/3DEditor.conf

# 物理线框调试（绿色线框）
env 3D_AUTO_MODE=wire 3D_SHOT=$PWD/shots/08_wire.png ./build/3DEditor ~/Physics3DProjects/Demo火锅店项目 >/dev/null 2>&1 &
sleep 5
pkill -9 -f "build/3DEditor" 2>/dev/null; sleep 1
pkill Xvfb 2>/dev/null
