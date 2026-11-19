#include "LuaSystem.h"
#include "GLViewport.h"
#include "AudioService.h"
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QQuaternion>
#include <cmath>

// 每个 lua_State 的上下文（存注册表）
static const char *CTX_KEY = "engine.ctx";
struct LuaCtx { LuaSystem *sys; int objIdx; };

static LuaCtx *ctxOf(lua_State *L) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, (void*)CTX_KEY);
    LuaCtx *ctx = (LuaCtx*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return ctx;
}

// ---- self:Move(x, y, z)：位置增量 ----
int LuaSystem::l_move(lua_State *L) {
    LuaCtx *ctx = ctxOf(L);
    if (!ctx || ctx->objIdx < 0) return 0;
    float x = (float)luaL_checknumber(L, 2), y = (float)luaL_checknumber(L, 3), z = (float)luaL_checknumber(L, 4);
    ctx->sys->obj(ctx->objIdx).position += QVector3D(x, y, z);
    return 0;
}

// ---- self:Rotate(x, y, z)：欧拉角增量（度/帧） ----
int LuaSystem::l_rotate(lua_State *L) {
    LuaCtx *ctx = ctxOf(L);
    if (!ctx || ctx->objIdx < 0) return 0;
    float x = (float)luaL_checknumber(L, 2), y = (float)luaL_checknumber(L, 3), z = (float)luaL_checknumber(L, 4);
    SceneObject &o = ctx->sys->obj(ctx->objIdx);
    o.rotation = QQuaternion::fromEulerAngles(QVector3D(x, y, z)) * o.rotation;
    return 0;
}

// ---- self:GetPosition() -> x, y, z ----
int LuaSystem::l_getPosition(lua_State *L) {
    LuaCtx *ctx = ctxOf(L);
    QVector3D p(0, 0, 0);
    if (ctx && ctx->objIdx >= 0) p = ctx->sys->obj(ctx->objIdx).position;
    lua_pushnumber(L, p.x()); lua_pushnumber(L, p.y()); lua_pushnumber(L, p.z());
    return 3;
}

// ---- self:SetColor(r, g, b) ----
int LuaSystem::l_setColor(lua_State *L) {
    LuaCtx *ctx = ctxOf(L);
    if (ctx && ctx->objIdx >= 0) {
        ctx->sys->obj(ctx->objIdx).color = QVector3D(
            (float)lua_tonumber(L, 2), (float)lua_tonumber(L, 3), (float)lua_tonumber(L, 4));
    }
    return 0;
}

// ---- self:GetName() -> string ----
int LuaSystem::l_getName(lua_State *L) {
    LuaCtx *ctx = ctxOf(L);
    QByteArray n = (ctx && ctx->objIdx >= 0) ? ctx->sys->obj(ctx->objIdx).name.toUtf8() : QByteArray("?");
    lua_pushstring(L, n.constData());
    return 1;
}

// ---- self:PlayAnimation(name)：播放物体挂载的骨骼动画 ----
int LuaSystem::l_playAnimation(lua_State *L) {
    LuaCtx *ctx = ctxOf(L);
    if (!ctx || ctx->objIdx < 0) return 0;
    SceneObject &o = ctx->sys->obj(ctx->objIdx);
    QString animName = lua_tostring(L, 2) ? QString::fromUtf8(lua_tostring(L, 2)) : QString();
    QString animPath = o.animPath;
    if (!animName.isEmpty() && !animPath.isEmpty()) {
        QString dir = QFileInfo(animPath).absolutePath();
        QString cand = dir + "/" + animName + ".vnanim";
        if (QFile(cand).exists()) animPath = cand;
    }
    if (animPath.isEmpty() || !QFile(animPath).exists()) {
        qWarning() << "[Lua] PlayAnimation：物体未挂载 .vnanim（或文件缺失）";
        return 0;
    }
    GLViewport::playAnim(o, animPath);
    return 0;
}

// ---- Game API：音频（真实调用 QMediaPlayer / QSoundEffect） ----
int LuaSystem::l_gamePlayBGM(lua_State *L) {
    const char *s = lua_tostring(L, 1);
    if (!s || !*s) return 0;
    AudioService::instance().playBGM(QString::fromUtf8(s));
    return 0;
}
int LuaSystem::l_gameStopBGM(lua_State *L) {
    AudioService::instance().stopBGM();
    return 0;
}
int LuaSystem::l_gamePlaySFX(lua_State *L) {
    const char *s = lua_tostring(L, 1);
    if (!s || !*s) return 0;
    AudioService::instance().playSFX(QString::fromUtf8(s));
    return 0;
}

// ---- print 重定向到引擎日志（终端可见） ----
int LuaSystem::l_print(lua_State *L) {
    int n = lua_gettop(L);
    QString out;
    for (int i = 1; i <= n; ++i) {
        if (i > 1) out += "  ";
        const char *s = lua_tostring(L, i);
        out += s ? QString::fromUtf8(s) : QString("<%1>").arg(lua_typename(L, lua_type(L, i)));
    }
    qInfo() << "[Lua]" << out;
    return 0;
}

static void pushSelfTable(lua_State *L) {
    LuaCtx *ctx = ctxOf(L);
    lua_newtable(L);
    if (!ctx) return;
    lua_pushlightuserdata(L, ctx->sys);
    lua_setfield(L, -2, "__sys");
    lua_pushinteger(L, ctx->objIdx);
    lua_setfield(L, -2, "__idx");
    lua_pushcfunction(L, &LuaSystem::l_move);        lua_setfield(L, -2, "Move");
    lua_pushcfunction(L, &LuaSystem::l_rotate);      lua_setfield(L, -2, "Rotate");
    lua_pushcfunction(L, &LuaSystem::l_getPosition); lua_setfield(L, -2, "GetPosition");
    lua_pushcfunction(L, &LuaSystem::l_setColor);    lua_setfield(L, -2, "SetColor");
    lua_pushcfunction(L, &LuaSystem::l_getName);     lua_setfield(L, -2, "GetName");
    lua_pushcfunction(L, &LuaSystem::l_playAnimation); lua_setfield(L, -2, "PlayAnimation");
}

bool LuaSystem::attach(int objIdx, const QString &luaPath) {
    lua_State *L = luaL_newstate();
    if (!L) return false;
    luaL_openlibs(L);

    ScriptInstance inst;
    inst.objIdx = objIdx;
    inst.path = luaPath;
    inst.L = L;
    instances.push_back(inst);

    auto *ctx = new LuaCtx{this, objIdx};
    lua_pushlightuserdata(L, ctx);
    lua_rawsetp(L, LUA_REGISTRYINDEX, (void*)CTX_KEY);

    lua_register(L, "print", &LuaSystem::l_print);
    // Game 全局表：Game.PlayBGM("music.mp3") / Game.StopBGM() / Game.PlaySFX("sfx.wav")
    lua_newtable(L);
    lua_pushcfunction(L, &LuaSystem::l_gamePlayBGM);  lua_setfield(L, -2, "PlayBGM");
    lua_pushcfunction(L, &LuaSystem::l_gameStopBGM);  lua_setfield(L, -2, "StopBGM");
    lua_pushcfunction(L, &LuaSystem::l_gamePlaySFX);  lua_setfield(L, -2, "PlaySFX");
    lua_setglobal(L, "Game");
    if (objects && objIdx >= 0 && size_t(objIdx) < objects->size())
        qInfo() << QString("[Lua] 脚本已挂载：%1 -> %2").arg(QFile(luaPath).fileName()).arg(obj(objIdx).name);
    return true;
}

// ---- 热重载：关闭该脚本所有实例，重建 lua_State 并重跑 Start ----
void LuaSystem::reloadByPath(const QString &luaPath, std::vector<SceneObject> &objs) {
    std::vector<int> idxs;
    for (auto it = instances.begin(); it != instances.end();) {
        if (it->path == luaPath) {
            if (it->L) lua_close(it->L);
            idxs.push_back(it->objIdx);
            it = instances.erase(it);
        } else {
            ++it;
        }
    }
    if (idxs.empty()) return;
    for (int i : idxs) attach(i, luaPath);
    if (objects) startAll(objs);
}

void LuaSystem::detachByObject(int objIdx) {
    for (auto &it : instances) {
        if (it.objIdx == objIdx) {
            if (it.L) lua_close(it.L);
            it.L = nullptr;
            it.dead = true;
        }
    }
}

void LuaSystem::reportError(const QString &where, const QString &msg) {
    qWarning() << "[Lua 错误]" << where << ":" << msg;
}

void LuaSystem::startAll(std::vector<SceneObject> &objs) {
    objects = &objs;
    for (auto &inst : instances) {
        if (inst.dead || !inst.L || inst.started) continue;
        inst.started = true;
        if (luaL_dofile(inst.L, inst.path.toUtf8().constData()) != 0) {
            reportError(inst.path, QString::fromUtf8(lua_tostring(inst.L, -1)));
            inst.dead = true;
            continue;
        }
        qInfo() << QString("[Lua] 脚本已加载：%1").arg(inst.path);
        lua_getglobal(inst.L, "Start");
        if (lua_isfunction(inst.L, -1)) {
            pushSelfTable(inst.L);
            if (lua_pcall(inst.L, 1, 0, 0) != 0)
                reportError(inst.path, QString::fromUtf8(lua_tostring(inst.L, -1)));
            else
                qInfo() << QString("[Lua] Start() 已执行：%1").arg(objects->at(size_t(inst.objIdx)).name);
        } else {
            lua_pop(inst.L, 1);
        }
    }
}

void LuaSystem::updateAll(std::vector<SceneObject> &objs, float dt) {
    objects = &objs;
    for (auto &inst : instances) {
        if (inst.dead || !inst.L || !inst.started) continue;
        if (inst.objIdx < 0 || size_t(inst.objIdx) >= objects->size()) { inst.dead = true; continue; }
        lua_getglobal(inst.L, "Update");
        if (!lua_isfunction(inst.L, -1)) { lua_pop(inst.L, 1); continue; }
        pushSelfTable(inst.L);
        lua_pushnumber(inst.L, dt);
        if (lua_pcall(inst.L, 2, 0, 0) != 0) {
            reportError(inst.path, QString::fromUtf8(lua_tostring(inst.L, -1)));
            inst.dead = true;
        }
    }
}

void LuaSystem::stopAll() {
    for (auto &inst : instances) {
        if (inst.L) lua_close(inst.L);
        inst.L = nullptr;
        inst.started = false;
    }
    instances.clear();
}
