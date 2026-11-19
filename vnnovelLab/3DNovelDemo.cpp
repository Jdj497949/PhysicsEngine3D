// 3DNovelDemo —— 视觉小说核心层无头实测：Lua 协程 + UI.* API + 异步加载 + 后台物理
// 跑法：3DNovelDemo [lua脚本]；退出码 0 = 协程等待/唤醒/分支/特效全部跑通
#include <QCoreApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <lua.hpp>
#include <btBulletDynamicsCommon.h>
#include "NovelSystem.h"

// ---------- 全局句柄 ----------
static lua_State *g_co = nullptr;        // 剧情协程
static int g_coRef = LUA_NOREF;
static NovelDirector *g_director = nullptr;
static int g_state = 0;                  // 0=启动 1=等点击 2=等选择 3=等点击2 4=收尾
static int g_physicsTicks = 0;
static btDiscreteDynamicsWorld *g_world = nullptr;
static btRigidBody *g_fallBox = nullptr;

// ---------- Lua 绑定：UI.* ----------
static int l_print(lua_State *L) {                       // print → [LUA] 前缀
    int n = lua_gettop(L);
    QString out;
    for (int i = 1; i <= n; ++i) out += QString::fromUtf8(lua_tostring(L, i));
    NOVEL_LOG("[LUA] %s", out.toUtf8().constData());
    return 0;
}
static int l_showDialogue(lua_State *L) {                 // UI.ShowDialogue(charName, text)
    const char *who = luaL_checkstring(L, 1);
    const char *text = luaL_checkstring(L, 2);
    g_director->box.show(QString::fromUtf8(who), QString::fromUtf8(text));
    return 0;
}
static int l_setPortrait(lua_State *L) {                  // UI.SetPortrait(charID, position)
    g_director->portrait.set(QString::fromUtf8(luaL_checkstring(L, 1)),
                             QString::fromUtf8(luaL_checkstring(L, 2)));
    return 0;
}
static int l_waitForClick(lua_State *L) {                 // UI.WaitForClick() —— 协程挂起，绝不卡主线程
    NOVEL_LOG("[LUA] 等待玩家点击...");
    return lua_yield(L, 0);                               // 挂起剧情，等主线程点击信号唤醒
}
static int l_showChoices(lua_State *L) {                  // UI.ShowChoices({"A","B"}) → 返回玩家选择下标
    luaL_checktype(L, 1, LUA_TTABLE);
    QStringList labels;
    int n = int(lua_rawlen(L, 1));
    for (int i = 1; i <= n; ++i) {
        lua_rawgeti(L, 1, i);
        labels << QString::fromUtf8(lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    g_director->showChoices(labels);
    return lua_yield(L, 0);                               // 挂起，resume 时把选择下标压回栈
}
static int l_playBGM(lua_State *L) {
    g_director->playBGM(QString::fromUtf8(luaL_checkstring(L, 1)));
    return 0;
}
static int l_stopBGM(lua_State *L) { g_director->stopBGM(); return 0; }
static int l_showImageAsync(lua_State *L) {               // UI.ShowImageAsync(path, effect) —— 多线程加载
    g_director->showImageAsync(QString::fromUtf8(luaL_checkstring(L, 1)),
                               QString::fromUtf8(luaL_optstring(L, 2, "fade")));
    return 0;                                             // 立即返回：加载在 QThreadPool 工作线程
}
static int l_fadeToBlack(lua_State *L) {                  // UI.FadeToBlack(durationMs)
    g_director->fadeToBlack(int(luaL_checknumber(L, 1)));
    return 0;                                             // QPropertyAnimation 在主循环平滑插值
}
static int l_typewriterSpeed(lua_State *L) {              // UI.TypewriterSpeed(charsPerSec)
    g_director->box.setSpeed(int(luaL_checknumber(L, 1)));
    return 0;
}
static int l_jumpToScene(lua_State *L) {                  // Scene.JumpTo(sceneName)
    NOVEL_LOG("[SCENE] JumpTo: %s（卸载当前场景 → 异步加载新场景）",
              luaL_checkstring(L, 1));
    return 0;
}

static void bindUI(lua_State *L) {                        // 注册全局 UI / Scene 表
    luaL_Reg ui[] = {
        {"ShowDialogue", l_showDialogue}, {"SetPortrait", l_setPortrait},
        {"WaitForClick", l_waitForClick}, {"ShowChoices", l_showChoices},
        {"PlayBGM", l_playBGM}, {"StopBGM", l_stopBGM},
        {"ShowImageAsync", l_showImageAsync}, {"FadeToBlack", l_fadeToBlack},
        {"TypewriterSpeed", l_typewriterSpeed}, {nullptr, nullptr},
    };
    lua_newtable(L);
    luaL_setfuncs(L, ui, 0);
    lua_setglobal(L, "UI");
    luaL_Reg sc[] = {{"JumpTo", l_jumpToScene}, {nullptr, nullptr}};
    lua_newtable(L);
    luaL_setfuncs(L, sc, 0);
    lua_setglobal(L, "Scene");
    lua_register(L, "print", l_print);
}

static int doResume(int nargs) {                          // Lua 5.4 协程唤醒
    int nres = 0;
    int status = lua_resume(g_co, nullptr, nargs, &nres);
    if (status == LUA_OK) { NOVEL_LOG("[LUA] 剧情结束"); qApp->quit(); return -1; }
    if (status != LUA_YIELD)
        NOVEL_LOG("[LUA] ERROR: %s", lua_tostring(g_co, -1));
    return status;
}
static void resumeClick(const char *who) {                // 点击/选择信号 → 唤醒协程
    NOVEL_LOG("[EVENT] 收到点击信号! 唤醒协程! (source=%s)", who);
    doResume(0);
}
static void resumeChoice(int idx) {
    g_director->choose(idx);                              // 打印 [SOCKET] Branching to Option ...
    lua_pushinteger(g_co, idx);
    doResume(1);
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString script = argc > 1 ? QString::fromLocal8Bit(argv[1]) : "vnnovelLab/test_novel.lua";
    g_director = new NovelDirector(&app);

    // ---- 后台物理：视觉小说播放时物理引擎必须继续跑 ----
    btDefaultCollisionConfiguration cfg;
    btCollisionDispatcher disp(&cfg);
    btDbvtBroadphase bp;
    btSequentialImpulseConstraintSolver solver;
    btDiscreteDynamicsWorld world(&disp, &bp, &solver, &cfg);
    world.setGravity(btVector3(0, -9.81f, 0));
    g_world = &world;
    static btBoxShape shape(btVector3(0.3f, 0.3f, 0.3f));
    btRigidBody::btRigidBodyConstructionInfo info(1.0f, nullptr, &shape, btVector3(0, 0, 0));
    static btRigidBody box(info);
    box.setWorldTransform(btTransform(btQuaternion(0, 0, 0, 1), btVector3(0, 8, 0)));
    world.addRigidBody(&box);
    g_fallBox = &box;

    // ---- Lua 虚拟机 + 协程 ----
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    bindUI(L);
    g_co = lua_newthread(L);
    g_coRef = luaL_ref(L, LUA_REGISTRYINDEX);
    // 关键：脚本裸加载后由协程 resume 执行；luaL_dofile 内部 pcall 会造成 C 边界，yield 会报错
    if (luaL_loadfile(g_co, script.toUtf8().constData()) != LUA_OK) {
        NOVEL_LOG("[LUA] load error: %s", lua_tostring(g_co, -1));
        return 1;
    }

    // ---- 主循环：QTimer 驱动（协程挂起期间不空转，物理照跑） ----
    QElapsedTimer clock; clock.start();
    QTimer tick;
    QObject::connect(&tick, &QTimer::timeout, [&]() {
        const double dt = 1.0 / 60.0;
        g_world->stepSimulation(float(dt), 1, float(dt));
        ++g_physicsTicks;
        if (g_physicsTicks % 30 == 0)                       // 证明：对话播放中物理仍在后台跑
            NOVEL_LOG("[PHYSICS] tick=%d  box.y=%.3f  (dialogue playing, physics alive)",
                      g_physicsTicks, g_fallBox->getWorldTransform().getOrigin().y());

        switch (g_state) {
        case 0:                                             // 首次进入：启动剧情协程
            g_state = 1;
            if (doResume(0) < 0) return;
            break;
        case 1:                                             // 等点击：物理跑够 45 帧后模拟玩家点击
            if (g_physicsTicks >= 45) { g_state = 2; resumeClick("mouse-left"); }
            break;
        case 2:                                             // 等选择：模拟玩家点 A
            if (g_physicsTicks >= 75) { g_state = 3; resumeChoice(1); }
            break;
        case 3:                                             // 第二次等点击
            if (g_physicsTicks >= 105) { g_state = 4; resumeClick("space"); }
            break;
        default:
            break;
        }
    });
    tick.start(int(1000.0 / 60.0));
    QTimer::singleShot(8000, &app, []() { NOVEL_LOG("[TIMEOUT] 8s 未结束，判失败"); qApp->exit(1); });
    NOVEL_LOG("=== 3DNovelDemo 视觉小说核心层无头实测 ===");
    NOVEL_LOG("script: %s", script.toUtf8().constData());
    return app.exec();
}
