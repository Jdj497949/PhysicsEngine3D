#pragma once
#include <QString>
#include <QVector3D>
#include <vector>
#include <lua.hpp>
#include "World.h"

// ============ Lua 脚本引擎（C API 直连） ============
// 每个挂载的脚本一个 lua_State。
// 生命周期：function Start(self) / function Update(self, deltaTime)
// 绑定 API：self:Move(x,y,z) self:Rotate(x,y,z) self:GetPosition() self:SetColor(r,g,b) self:GetName()
class LuaSystem {
public:
    struct ScriptInstance {
        int objIdx = -1;
        QString path;
        lua_State *L = nullptr;
        bool started = false;
        bool dead = false;   // 出错后停跑
    };

    std::vector<ScriptInstance> instances;
    std::vector<SceneObject> *objects = nullptr;   // 当前场景（运行时由 MainWindow 设置）

    SceneObject &obj(int i) { return (*objects)[size_t(i)]; }

    // 给物体挂脚本（objects[idx] 必须有效）
    bool attach(int objIdx, const QString &luaPath);
    void detachByObject(int objIdx);
    void reloadByPath(const QString &luaPath, std::vector<SceneObject> &objs); // 保存后热重载
    void startAll(std::vector<SceneObject> &objects);          // 运行时调用一次
    void updateAll(std::vector<SceneObject> &objects, float dt); // 每帧
    void stopAll();
    static void reportError(const QString &where, const QString &msg);

    // Lua C API 绑定（public 供内部静态函数注册）
    static int l_move(lua_State *L);
    static int l_rotate(lua_State *L);
    static int l_getPosition(lua_State *L);
    static int l_setColor(lua_State *L);
    static int l_getName(lua_State *L);
    static int l_playAnimation(lua_State *L);  // self:PlayAnimation(name)
    static int l_print(lua_State *L);   // 重定向 print 到引擎日志
    // Game 全局 API：Game.PlayBGM/StopBGM/PlaySFX（真实调用 QMediaPlayer / QSoundEffect）
    static int l_gamePlayBGM(lua_State *L);
    static int l_gameStopBGM(lua_State *L);
    static int l_gamePlaySFX(lua_State *L);
};
