cmake_minimum_required(VERSION 3.16)
project(PhysicsEngine3D VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Multimedia)
find_package(Qt6 QUIET COMPONENTS Gui)
find_package(Bullet REQUIRED)
find_package(OpenGL REQUIRED)
find_package(assimp QUIET)
if(NOT assimp_FOUND)
    # Windows 交叉编译：find_package 找不到时，用手工交叉编译的 assimp 静态库
    if(WIN32 AND EXISTS /root/workspace/.local/assimp-mingw/lib/libassimp.a)
        set(ASSIMP_LIB /root/workspace/.local/assimp-mingw/lib/libassimp.a)
        set(ASSIMP_MINGW_INCLUDE /root/workspace/.local/assimp-mingw/include)
        set(ZLIB_MINGW_LIB /tmp/zlib-1.3.1/build-win/libzlibstatic.a)
    else()
        set(ASSIMP_LIB assimp)
    endif()
else()
    set(ASSIMP_LIB assimp::assimp)
endif()

# ============ 3DLauncher: 只管项目创建与列表，不含任何 3D/物理代码 ============
add_executable(3DLauncher
    launcher/main.cpp
)
target_link_libraries(3DLauncher PRIVATE
    Qt6::Widgets
)

# ============ 3DEditor: 3D 视口 + Bullet 物理 + Lua 脚本 + 主编辑器 ============
add_executable(3DEditor
    editor/main.cpp
    editor/MainWindow.cpp
    editor/GLViewport.cpp
    editor/SettingsDialog.cpp
    editor/LuaSystem.cpp
)
find_package(Lua REQUIRED)
target_include_directories(3DEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/editor ${BULLET_INCLUDE_DIRS} ${LUA_INCLUDE_DIR}
    ${ASSIMP_MINGW_INCLUDE})
target_link_libraries(3DEditor PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Multimedia
    ${BULLET_LIBRARIES}
    ${LUA_LIBRARIES}
    OpenGL::GL
    ${ASSIMP_LIB}
    ${ZLIB_MINGW_LIB}
)

# ============ VNMateriaLab: 材质实验室（独立程序，共享 MaterialSystem 着色器） ============
add_executable(VNMateriaLab
    materiaLab/VNMateriaLab.cpp
)
target_include_directories(VNMateriaLab PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/editor)
target_link_libraries(VNMateriaLab PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    OpenGL::GL
)

# ============ VNSocketEditor: 插座编辑器（节点画布 + 模块库 + 控制台） ============
add_executable(VNSocketEditor
    socketLab/VNSocketEditor.cpp
)
target_include_directories(VNSocketEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/editor ${BULLET_INCLUDE_DIRS})
target_link_libraries(VNSocketEditor PRIVATE
    Qt6::Widgets
    ${BULLET_LIBRARIES}
)

# ============ VNCodeStudio: 脚本工作室（微型 VS Code：多标签+高亮+QProcess 测试运行） ============
add_executable(VNCodeStudio
    codeLab/VNCodeStudio.cpp
)
set_target_properties(VNCodeStudio PROPERTIES AUTOMOC ON)
target_link_libraries(VNCodeStudio PRIVATE
    Qt6::Widgets
)

# ============ VN2DEditor: 2D 编辑器（UI 模式=VNUIEditor：画布+锚点+控件库；纯 2D：RigidBody2D 物理模拟） ============
add_executable(VN2DEditor
    2dLab/VN2DEditor.cpp
)
set_target_properties(VN2DEditor PROPERTIES AUTOMOC ON)
target_link_libraries(VN2DEditor PRIVATE
    Qt6::Widgets
)

# ============ VNModelStudio: 模型骨骼动画工作室（第七个程序） ============
add_executable(VNModelStudio
    modelLab/VNModelStudio.cpp
)
set_target_properties(VNModelStudio PROPERTIES AUTOMOC ON)
target_include_directories(VNModelStudio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/editor ${ASSIMP_MINGW_INCLUDE})
target_link_libraries(VNModelStudio PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    OpenGL::GL
    ${ASSIMP_LIB}
)

# ============ VNAudioPlayer: 音频播放器（第八个程序：拖拽音频+播放控制） ============
add_executable(VNAudioPlayer
    audioLab/VNAudioPlayer.cpp
)
set_target_properties(VNAudioPlayer PROPERTIES AUTOMOC ON)
target_link_libraries(VNAudioPlayer PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
)

# ============ VNWeatherStudio: 天气工作室（第九个程序：.sky 天空配置编辑） ============
add_executable(VNWeatherStudio
    weatherLab/VNWeatherStudio.cpp
)
set_target_properties(VNWeatherStudio PROPERTIES AUTOMOC ON)
target_link_libraries(VNWeatherStudio PRIVATE
    Qt6::Widgets
)

# ============ CrashReporter: 崩溃报告员（第十个程序：由崩溃进程派生，状态干净地弹双层弹窗并自爆重启） ============
add_executable(CrashReporter
    crashReporter/main.cpp
)
target_link_libraries(CrashReporter PRIVATE
    Qt6::Widgets
)

# ============ 3DRagdollDemo: 布娃娃物理实测（第十一个程序：15 关节锥形约束人形跌落） ============
add_executable(3DRagdollDemo
    ragdollLab/3DRagdollDemo.cpp
)
target_include_directories(3DRagdollDemo PRIVATE ${BULLET_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(3DRagdollDemo PRIVATE
    Qt6::Core
    ${BULLET_LIBRARIES}
)

# ============ VNAssetConverter: 资产转换器（第十二个程序：.fbx/.obj/.gltf → .vnmodel 一键转换+碰撞体生成） ============
add_executable(VNAssetConverter
    tools/convert_model.cpp
)
set_target_properties(VNAssetConverter PROPERTIES AUTOMOC ON)
target_include_directories(VNAssetConverter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/editor ${ASSIMP_MINGW_INCLUDE} ${Qt6Gui_INCLUDE_DIRS})
target_link_libraries(VNAssetConverter PRIVATE
    Qt6::Core
    ${ASSIMP_LIB}
    ${ZLIB_MINGW_LIB}
)

# ============ 3DNovelDemo: 视觉小说核心层无头实测（Lua 协程 + UI.* API + 异步加载 + 后台物理） ============
add_executable(3DNovelDemo
    vnnovelLab/3DNovelDemo.cpp
    vnnovel/NovelSystem.h
)
find_package(Lua REQUIRED)
target_include_directories(3DNovelDemo PRIVATE ${BULLET_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/vnnovel ${LUA_INCLUDE_DIR})
target_link_libraries(3DNovelDemo PRIVATE
    Qt6::Core
    ${BULLET_LIBRARIES}
    ${LUA_LIBRARIES}
)
