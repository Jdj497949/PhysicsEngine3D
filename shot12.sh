#!/bin/bash
cd /root/workspace/a626663e-516e-4b27-ba62-f7513cbf2d7d/PhysicsEngine3D
export LIBGL_ALWAYS_SOFTWARE=1 DISPLAY=:99
PROJ=/root/workspace/Physics3DProjects/Demo火锅店项目
S=shots12

echo "=== R1: 2D 双标签页 ==="
C2D_AUTO_MODE=tabs C2D_SHOT=$S/R1_2D双标签页.png ./build/VN2DEditor > $S/r1.log 2>&1
echo "R1 exit=$?"

echo "=== R2: 3D 字体管理 ==="
3D_AUTO_MODE=fontmenu 3D_SHOT=$S/R2_3D字体管理.png ./build/3DEditor "$PROJ" > $S/r2.log 2>&1
echo "R2 exit=$?"

echo "=== R3: 插座蓝图 文件选择器 ==="
SOCKET_AUTO_MODE=filedlg SOCKET_SHOT=$S/R3_文件选择器蓝图.png ./build/VNSocketEditor "$PROJ/assets/打开图片.socket" > $S/r3.log 2>&1
echo "R3 exit=$?"

echo "=== R4: 运行时弹系统文件选择器 ==="
3D_AUTO_MODE=filedlg 3D_SHOT=$S/R4_运行弹文件选择器.png ./build/3DEditor "$PROJ" > $S/r4.log 2>&1
echo "R4 exit=$?"

echo "=== R5: 车交互 按E开门 ==="
3D_AUTO_MODE=interact 3D_SHOT=$S/R5_车交互按E.png ./build/3DEditor "$PROJ" > $S/r5.log 2>&1
echo "R5 exit=$?"
ls -la $S/*.png
