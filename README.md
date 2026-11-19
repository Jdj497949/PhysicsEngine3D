# PhysicsEngine3D

> **纯手搓的跨平台 3D 物理引擎 + 视觉小说（VN）工具链。**
> C++17 / Qt6 / OpenGL / Bullet Physics / Lua 5.4，从一个窗口到 13 个独立程序，从 3D 编辑器到 2D VN 编辑器，从布娃娃物理到 AES-256 资源加密——不依赖任何游戏引擎，全部手写。

![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)
![Qt6](https://img.shields.io/badge/Qt-6-41CD52?logo=qt&logoColor=white)
![Bullet](https://img.shields.io/badge/Physics-Bullet-orange)
![Lua](https://img.shields.io/badge/Script-Lua%205.4-2C2D72?logo=lua&logoColor=white)
![License](https://img.shields.io/badge/License-MIT-green)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20macOS%20%7C%20Linux-blue)

---

## 目录

- [这是什么](#这是什么)
- [十大特色功能](#十大特色功能)
- [十三个独立程序](#十三个独立程序)
- [目录结构](#目录结构)
- [跨平台编译指南](#跨平台编译指南)
- [一分钟跑通第一个 Demo](#一分钟跑通第一个-demo)
- [视觉小说层（vnnovel）](#视觉小说层vnnovel)
- [打包你的游戏](#打包你的游戏)
- [开源协议](#开源协议)
- [用 git 推上 GitHub：命令大全](#用-git-推上-github命令大全)

---

## 这是什么

PhysicsEngine3D 是一套从零手写的游戏工具链：

- **3D 一侧**：自带 OpenGL 视口的场景编辑器，Bullet Physics 刚体/约束物理、Lua 5.4 脚本热绑定、材质实验室、插座式节点逻辑（1000+ 节点库）、模型骨骼动画工作室、天气/天空配置、音频播放、崩溃自恢复。
- **2D 一侧**：VN2DEditor 同时是 **2D 物理沙盘**（RigidBody2D、平台跳跃、金币收集）和 **VN UI 编辑器**（画布 + 锚点 + 控件库 18 项，含对话框九宫格 / 角色立绘 / 选项分支按钮）。
- **视觉小说引擎层（vnnovel）**：Lua 协程驱动的剧情系统——`WaitForClick` 挂起剧情、信号唤醒、多线程异步图片加载、打字机、BGM、FadeToBlack、场景跳转，剧情播放时物理照跑不空转。
- **资产管线**：`.fbx / .obj / .gltf → .vnmodel` 一键转换（可带碰撞体），构建产物用 **AES-256 CTR** 加密成资源包。

没有 Unity，没有 Unreal，没有 Godot——每一行都是自己写的。

## 十大特色功能

| # | 特色 | 说明 |
|---|------|------|
| 1 | **13 个独立程序** | 启动器、3D 编辑器、材质实验室、插座编辑器、脚本工作室、2D 编辑器、模型工作室、音频播放器、天气工作室、崩溃报告员、布娃娃实测、资产转换器、VN 无头实测，见下表 |
| 2 | **Bullet 布娃娃物理** | 16 盒形件 + 15 个锥形扭转约束（btConeTwistConstraint）人形布娃娃，跌落 120Hz 步进，15/15 关节完好落地 |
| 3 | **AES-256 CTR 资源加密** | 自包含 header-only 实现（`common/Aes256.h`），构建游戏时一键把资源打成加密包 |
| 4 | **Lua 5.4 协程剧情系统** | `UI.WaitForClick()` 真挂起协程不卡主线程，信号唤醒、选项分支把选择压回 Lua 栈 |
| 5 | **多线程异步资源加载** | `UI.ShowImageAsync` 走 QThreadPool，大图加载期间主线程自由、QPropertyAnimation 平滑淡入 |
| 6 | **插座节点逻辑系统** | 可视化节点画布，内置 1036 个节点（含「剧情控制」分类 14 节点），拖线连线驱动游戏逻辑 |
| 7 | **崩溃自恢复** | 进程崩溃时派生干净的 CrashReporter 子进程，双层弹窗收日志后可自爆重启主程序 |
| 8 | **3D 草地项目模板** | 启动器一键创建草地演示项目（默认摄像机 + 平行光 + 摇摆草地）；GPU Instancing 百万级草地渲染在路线图中 |
| 9 | **微型 VS Code——VNCodeStudio** | 多标签、语法高亮、QProcess 一键测试运行 Lua 脚本 |
| 10 | **跨平台三端构建** | 同一份 CMakeLists：Linux 原生、macOS（Homebrew）、Windows（MinGW 交叉编译或 MSVC + vcpkg），GitHub Actions 全平台自动打包 |

## 十三个独立程序

| 程序 | 用途 |
|------|------|
| `3DLauncher` | 项目启动器：模板创建（3D 动作 / 3D 草地 / 2D 演示 / UI 2D 等），不含 3D 代码的轻量壳 |
| `3DEditor` | 主编辑器：3D 视口 + Bullet 物理 + Lua 脚本 + 材质/粒子/天空/插座/插件系统 + 一键构建 |
| `VNMateriaLab` | 材质实验室：PBR 参数实时调（baseColor/metalness/roughness/纹理/风力） |
| `VNSocketEditor` | 插座编辑器：节点画布 + 1036 节点库 + 控制台 |
| `VNCodeStudio` | 脚本工作室：微型 VS Code，多标签 + 高亮 + 测试运行 |
| `VN2DEditor` | 2D 编辑器：UI 模式（画布/锚点/18 项控件库）与 2D 物理模式（跳跃/金币）双形态 |
| `VNModelStudio` | 模型骨骼动画工作室 |
| `VNAudioPlayer` | 音频播放器：拖拽导入 + 播放控制 |
| `VNWeatherStudio` | 天气工作室：`.sky` 天空/天气配置编辑 |
| `CrashReporter` | 崩溃报告员：崩溃后干净进程弹窗收日志 |
| `3DRagdollDemo` | 布娃娃物理实测：15 关节人形跌落验收 |
| `VNAssetConverter` | 资产转换器：`.fbx/.obj/.gltf → .vnmodel`（可选碰撞体） |
| `3DNovelDemo` | 视觉小说核心层无头实测：Lua 协程 + 异步加载 + 后台物理 |

## 目录结构

```
PhysicsEngine3D/
├── CMakeLists.txt              # 单一构建脚本，13 个目标全在这
├── LICENSE                     # MIT License
├── common/                     # 跨程序公共头
│   ├── Aes256.h                #   AES-256 CTR（header-only，资源包加密）
│   ├── BuildKit.h              #   构建工具桩
│   ├── CrashHandler.h          #   崩溃捕获（POSIX/Windows 双分支）
│   └── Ragdoll.h               #   布娃娃规格/部件/构建器（16 盒件 + 15 锥形约束）
├── editor/                     # 3D 编辑器源码
│   ├── main.cpp
│   ├── MainWindow.h/.cpp       #   主窗口：菜单/停靠面板/运行态
│   ├── GLViewport.h/.cpp       #   OpenGL 视口
│   ├── LuaSystem.h/.cpp        #   Lua 5.4 绑定与热执行
│   ├── SettingsDialog.h/.cpp
│   ├── World.h                 #   场景对象模型
│   ├── MaterialSystem.h        #   PBR 材质系统
│   ├── ParticleSystem.h        #   粒子系统
│   ├── SkySystem.h             #   天空/天气系统
│   ├── SocketSystem.h          #   插座节点运行时
│   ├── PluginSystem.h          #   插件加载
│   ├── RendererFactory.h       #   渲染器工厂
│   ├── BuildManager.h          #   一键构建 + AES 资源加密
│   ├── BuildDialog.h / ImportDialog.h / AudioService.h
│   └── ModelSystem.h           #   模型加载（assimp）
├── launcher/main.cpp           # 3DLauncher
├── materiaLab/VNMateriaLab.cpp
├── socketLab/VNSocketEditor.cpp
├── codeLab/VNCodeStudio.cpp
├── 2dLab/VN2DEditor.cpp        # 2D 编辑器（UI 模式 + 2D 物理模式）
├── modelLab/VNModelStudio.cpp
├── audioLab/VNAudioPlayer.cpp
├── weatherLab/VNWeatherStudio.cpp
├── crashReporter/main.cpp
├── ragdollLab/3DRagdollDemo.cpp
├── tools/
│   ├── convert_model.cpp       # VNAssetConverter
│   ├── gen_models.py / gen_stickman.py / gen_socket_nodes.py / patch_uv.py
├── vnnovel/NovelSystem.h       # 视觉小说核心层（NovelImage/DialogueBox/Portrait/ChoiceButton/NovelDirector）
└── vnnovelLab/
    ├── 3DNovelDemo.cpp         # 无头实测：Lua 协程 + UI.* API + 后台物理
    └── test_novel.lua          # 示例剧情脚本
```

> 无头/headless 变体与 GUI 源码同构（同名目录的 `PhysicsEngine3D_Headless`），用于 CI 与无显示环境验证。

## 跨平台编译指南

### 依赖一览

| 依赖 | 版本 | 用途 |
|------|------|------|
| CMake | ≥ 3.16 | 构建 |
| C++ 编译器 | C++17 | MSVC / GCC / Clang 均可 |
| Qt6 | Widgets + OpenGLWidgets + Multimedia | 全部 UI |
| Bullet Physics | 2.8+ / 3.x | 物理 |
| Lua | 5.4（注意 5.3 的 `lua_resume` 是三参，编译不过） | 脚本 |
| OpenGL | 3.3+ | 渲染 |
| assimp | 5.x | 模型导入 |
| zlib | assimp 依赖 | — |

### Linux（Ubuntu 22.04 / Debian）

```bash
sudo apt update
sudo apt install -y build-essential cmake \
    qt6-base-dev qt6-multimedia-dev qt6-base-dev-tools \
    libbullet-dev liblua5.4-dev libassimp-dev libgl1-mesa-dev

git clone https://github.com/YOUR_NAME/PhysicsEngine3D.git
cd PhysicsEngine3D
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

./build/3DLauncher          # 启动器
./build/3DEditor            # 主编辑器
./build/3DRagdollDemo 3     # 布娃娃物理实测（15/15 关节、退出码 0）
./build/3DNovelDemo         # 视觉小说无头实测
```

### macOS（Apple Silicon / Intel）

```bash
brew install cmake qt bullet lua assimp

git clone https://github.com/YOUR_NAME/PhysicsEngine3D.git
cd PhysicsEngine3D
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix bullet);$(brew --prefix lua);$(brew --prefix assimp)"
cmake --build build -j$(sysctl -n hw.ncpu)
open build/3DLauncher
```

### Windows

**方式 A：MSVC + vcpkg（原生）**

```powershell
git clone https://github.com/YOUR_NAME/PhysicsEngine3D.git
cd PhysicsEngine3D
vcpkg install --triplet x64-windows bullet3 lua assimp
cmake -B build -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release
```

**方式 B：MinGW 交叉编译（在 Linux 上产 Windows exe）**

```bash
sudo apt install -y gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64
# bullet/zlib/assimp 需按目标三元组静态编译出 lib*.a 放入交叉工具链 lib 目录，
# 详见 CMakeLists.txt 中 ASSIMP_LIB 的手工回退分支
cmake -B build-win \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-win -j$(nproc)
```

**零编译快速路线**：推上 GitHub 后，本仓库自带的 GitHub Actions（`.github/workflows/build.yml`）会在 push 时自动产出三平台安装包——什么都不用装，push 即得。

## 一分钟跑通第一个 Demo

```bash
# 1. 布娃娃：16 盒件 + 15 关节人形从 3 米跌落，控制台逐帧打印关节状态
./build/3DRagdollDemo 3

# 2. 视觉小说无头实测：协程挂起 → 点击唤醒 → 选项分支 → 异步加载 → 后台物理
./build/3DNovelDemo
# 日志里能看到：
#   [PHYSICS] tick=30  box.y=6.733  (dialogue playing, physics alive)
#   [EVENT] 收到点击信号! 唤醒协程! (source=mouse-left)
#   [SOCKET] WaitForClick triggered. Branching to Option A.
#   [EFFECT] Image loaded in worker thread (1 ms), applying 'fade' via QPropertyAnimation
```

示例剧情脚本见 [`vnnovelLab/test_novel.lua`](vnnovelLab/test_novel.lua)：

```lua
print("开始游戏")
UI.ShowDialogue("主角", "你好世界")
UI.WaitForClick()                                       -- 挂起：等玩家点击
local pick = UI.ShowChoices({"A", "B"})                 -- 挂起：返回玩家选择
print("玩家选择了选项 " .. pick)
UI.TypewriterSpeed(60)
UI.SetPortrait("赛博女仆", "left")
UI.ShowDialogue("赛博女仆", "欢迎来到我的超市。")
UI.ShowImageAsync("assets/bg_supermarket_4k.png", "fade")
UI.PlayBGM("audio/supermarket_lofi.ogg")
UI.WaitForClick()
UI.ShowImageAsync("assets/pan_4k.png", "pan")
UI.FadeToBlack(800)
UI.StopBGM()
Scene.JumpTo("ending_scene.vnscene")
print("剧情结束")
```

## 视觉小说层（vnnovel）

`vnnovel/NovelSystem.h` 提供：

- `NovelImage`：`opacity` / `offsetX` 属性动画；`QThreadPool` 异步加载大图，加载完 `QMetaObject::invokeMethod` 回主线程应用 fade/pan 效果
- `DialogueBox`：九宫格参数、`QTimer` 打字机逐字（速度可由 `UI.TypewriterSpeed(字/秒)` 调）
- `Portrait`：三档立绘锚点（left / center / right = x 0.18 / 0.50 / 0.82）
- `ChoiceButton` + `NovelDirector::showChoices/choose`：选项分支，选择下标经 `lua_yield` 挂起点压回 Lua 栈
- `NovelDirector::fadeToBlack`：QPropertyAnimation 驱动遮罩透明度

**三个移植到 5.4 必知的坑**（都替你踩好了）：

1. `luaL_dofile` 内部是 pcall，协程 yield 会报 `attempt to yield across a C-call boundary` —— 必须用 `luaL_loadfile` 裸加载后 `lua_resume`。
2. Lua 5.4 的 `lua_resume` 是四参：`(L, from, nargs, int *nresults)`。
3. 含 `Q_OBJECT` 的头文件（如 `NovelSystem.h`）必须列进 `add_executable` 的源文件列表，否则 AUTOMOC 不生成 moc，链接报 vtable undefined。

## 打包你的游戏

编辑器里 **构建 → 一键构建** 会把场景、资源打进资源包，走 `common/Aes256.h` 的 AES-256 CTR 加密；目标机解包运行即可。资产先用 `VNAssetConverter` 把 `.fbx/.obj/.gltf` 转成 `.vnmodel`（加 `--col` 参数同时生成碰撞体版本）。

## 开源协议

本项目基于 **MIT License** 开源：

- ✅ 商用、修改、分发、私用，全部自由
- ✅ 唯一义务：保留版权声明（拷贝 `LICENSE` 文件即可）
- ⚠️ 免责声明：软件按"现状"提供，不含任何担保；使用产生的风险由使用者自担

```
MIT License

Copyright (c) 2026 PhysicsEngine3D Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## 用 git 推上 GitHub：命令大全

> 把 `YOUR_NAME` 换成你的 GitHub 用户名；先在 GitHub 网页上 New repository → `PhysicsEngine3D`（**不要**勾选初始化 README/.gitignore，本地已经有了）。

**① 第一次上传（全新仓库）**

```bash
# 0. 装好 git 并配置身份（只需一次）
git config --global user.name  "你的名字"
git config --global user.email "你的邮箱"

# 1. 进入项目根目录（含 CMakeLists.txt 的那一层）
cd PhysicsEngine3D

# 2. 初始化仓库（.gitignore / LICENSE / README.md 已就位）
git init

# 3. 切换主分支名为 main
git branch -M main

# 4. 添加全部文件（.gitignore 自动屏蔽 build/、*.exe、*.dll 等垃圾）
git add .

# 5. 核对将要提交的内容（确认没有误带 build 目录）
git status

# 6. 提交
git commit -m "PhysicsEngine3D v1.0: 首次开源——13 程序 + 布娃娃物理 + VN 视觉小说层"

# 7. 关联远程仓库并推送
git remote add origin https://github.com/YOUR_NAME/PhysicsEngine3D.git
git push -u origin main
```

**② 日常更新（改了代码再上传）**

```bash
git add .
git commit -m "写清这次改了什么"
git push
```

**③ 打 Tag 触发自动发版**（Actions 会自动构建三平台并把安装包挂到 Release）

```bash
git tag v1.0.0
git push origin v1.0.0
```

**④ 常用排错**

```bash
git remote -v                       # 查看远程地址对不对
git log --oneline -5                # 看最近 5 次提交
git checkout -- 文件名               # 撤销某个文件的未提交修改
git push --force                    # 强推（慎用，覆盖远程历史）
```

**⑤ 用 SSH 免密码（可选）**

```bash
ssh-keygen -t ed25519 -C "你的邮箱"
cat ~/.ssh/id_ed25519.pub           # 把输出粘到 GitHub → Settings → SSH and GPG keys
git remote set-url origin git@github.com:YOUR_NAME/PhysicsEngine3D.git
```

---

<div align="center">

**从空文件夹到 13 个程序，每一个功能都是真的。**
⭐ 觉得有用就点个 Star。

</div>
