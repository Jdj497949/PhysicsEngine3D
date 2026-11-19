-- test_novel.lua —— 视觉小说纯代码剧情（协程挂起/唤醒/分支/特效全走真实引擎）
print("开始游戏")
UI.ShowDialogue("主角", "你好世界")
UI.WaitForClick()                                        -- 挂起：等玩家点击（不卡主线程）
local pick = UI.ShowChoices({"A", "B"})                  -- 挂起：返回玩家选择的下标
print("玩家选择了选项 " .. pick)
UI.TypewriterSpeed(60)
UI.SetPortrait("赛博女仆", "left")
UI.ShowDialogue("赛博女仆", "欢迎来到我的超市。")          -- 第二幕：立绘+对话框联袂登场
UI.ShowImageAsync("assets/bg_supermarket_4k.png", "fade") -- 多线程异步加载，主线程不卡
UI.PlayBGM("audio/supermarket_lofi.ogg")
UI.WaitForClick()
UI.ShowImageAsync("assets/pan_4k.png", "pan")            -- 图片平移流动特效
UI.FadeToBlack(800)                                      -- QPropertyAnimation 淡入黑
UI.StopBGM()
Scene.JumpTo("ending_scene.vnscene")                     -- 场景跳转
print("剧情结束")
