#!/bin/bash
# 第四阶段修复后重拍：s03_matdrop（方块落回+红宝石）、s11_socketrun（重力正确）、s12_inspector、终端截图
cd /root/workspace/a626663e-516e-4b27-ba62-f7513cbf2d7d/PhysicsEngine3D
export LIBGL_ALWAYS_SOFTWARE=1 DISPLAY=:99
pkill -9 Xvfb; pkill -9 xterm; sleep 1
Xvfb :99 -screen 0 1600x900x24 &
sleep 2

PROJ=/root/workspace/Physics3DProjects/Demo火锅店项目

# s03: 拖红宝石.mat 到方块（方块应落回平面并呈红宝石红色+金属高光）
3D_SHOT=shots4/s03_matdrop.png 3D_AUTO_MODE=matdrop ./build/3DEditor "$PROJ" >/dev/null 2>&1
sleep 1

# s11: 插座图运行（方块按W受力移动，控制台[Socket]日志，重力正常下落）
3D_SHOT=shots4/s11_socketrun.png 3D_AUTO_MODE=socketrun ./build/3DEditor "$PROJ" >/dev/null 2>&1
sleep 1

# s12: 属性面板（挂Script+Socket+材质）
3D_SHOT=shots4/s12_inspector.png 3D_AUTO_MODE=inspector ./build/3DEditor "$PROJ" >/dev/null 2>&1
sleep 1

# 终端截图：四程序编译产物
xterm -T "TermShot4" -geometry 110x18+10+10 -e bash -c '
ls -lh build/3DLauncher build/3DEditor build/VNSocketEditor build/VNMateriaLab
sleep 20' &
sleep 5
WID=$(xdotool search --name "TermShot4" | head -1)
import -window "$WID" shots4/s13_terminal.png
identify shots4/s13_terminal.png

pkill -9 xterm; pkill -9 Xvfb
echo DONE
