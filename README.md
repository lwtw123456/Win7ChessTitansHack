# Win7 Chess Titans Hack

一个针对 **Windows 7 x64 自带国际象棋 Chess Titans（`chess.exe`）** 的学习型逆向工程项目。

项目由 Flutter 控制端和进程内 Hook DLL 两部分组成。控制端以挂起状态启动游戏，通过 **Early Bird APC Injection** 在游戏进入正常初始化流程前加载 `chess_hack.dll`；DLL 随后复用逆向得到的内部函数、对象字段和状态流程，实现自由移动与移动即赢。

## 功能

### 自由移动

解除棋子原有的常规走法限制，但不是简单地把合法性判断永久改成 `true`。

该功能通过两个 Inline Hook 接管候选走法与正式落子流程，并继续复用 Chess Titans 自己的棋局逻辑：

- 从游戏内部的选中走法对象恢复起点和终点；
- 构造引擎使用的内部走法结构；
- 临时落子，检查移动后己方王是否受攻击，再撤销试走；
- 区分普通移动与兵升变，并调用原游戏升变选择窗口；
- 正式提交走法，继续处理将军、将死、逼和、重复局面与半回合计数等状态。

因此，自由移动绕过的是棋子的常规移动规则，而棋盘更新、吃子、王安全检查、升变和棋局状态仍尽量交给原引擎完成。

### 移动即赢

通过 AOB 特征码定位两个结果计算点，将对应结果强制设为 `1`，使玩家完成移动后直接进入胜利流程。

两个补丁作为一组启用和恢复，关闭功能后会写回原始指令。

## 逆向内容概述

项目并非只修改单个条件跳转，而是对 Chess Titans 的走棋链进行了函数级和对象级恢复。目前代码中语义化使用了以下内部函数：

| 语义名称 | RVA | 用途 |
| --- | ---: | --- |
| `ResolveCandidate` | `0x6A9AC` | 解析并判断玩家当前候选走法 |
| `ProcessMove` | `0x3BB3C` | 处理已接受的走法与局面结果 |
| `CommitMove` | `0x3C0E0` | 正式提交内部走法 |
| `ApplyMoveTemporarily` | `0x3C4E0` | 临时应用走法 |
| `UndoTemporaryMove` | `0x3C6E0` | 撤销临时走法 |
| `CheckRepetition` | `0x3C394` | 检查重复局面 |
| `CheckDrawCondition` | `0x3D1C0` | 检查落子后的和棋相关状态 |
| `IsSquareAttacked` | `0x3DC94` | 判断指定格是否受攻击 |
| `QueryPostMoveState` | `0x3E420` | 查询正式落子后的局面状态 |

同时恢复并使用了部分内部数据布局：

- 外部候选走法 `Move`；
- 引擎内部走法 `EngineMove`；
- 64 项棋子指针棋盘；
- 玩家当前选中走法对象的坐标字段；
- 棋局引擎对象中的回合、王位置、和棋计数与状态字段；
- 棋子对象虚表中的类型查询函数；
- `(y << 4) + x` 形式的位置编码；
- 游戏对象与窗口句柄的全局指针链。

项目中的函数名和结构体名均为根据调用关系、参数、返回值及内存访问行为添加的语义名称，并非微软原始符号。

## Early Bird APC Injection

控制端使是 **Early Bird APC Injection**：

```text
CreateProcess(CREATE_SUSPENDED)
        ↓
VirtualAllocEx / WriteProcessMemory
        ↓
QueueUserAPC(LoadLibraryW, PrimaryThread, DllPath)
        ↓
ResumeThread
        ↓
初始线程在进入游戏主要逻辑前执行 APC 并加载 DLL
```

注入完成后，控制端通过模块枚举确认 `chess_hack.dll` 已进入目标进程。

## 使用的技术

- C++23 / Windows x64
- Flutter、Dart FFI 与 Win32 API
- Early Bird APC Injection
- SafetyHook Inline Hook 与 trampoline
- Zydis 指令解析
- AOB 特征码扫描与运行时代码补丁
- 指针链、虚表与部分 C++ 对象布局恢复
- `TaskDialogIndirect` 复用游戏资源实现升变选择
- `127.0.0.1:27654` 本地 TCP 控制通道

## 控制命令

DLL 在回环地址 `127.0.0.1:27654` 监听以下文本命令：

| 命令 | 功能 |
| --- | --- |
| `Open free move` | 启用自由移动 |
| `Close free move` | 关闭自由移动 |
| `Open win directly` | 启用移动即赢 |
| `Close win directly` | 关闭移动即赢 |

## 如何使用

将控制端可执行文件、`chess.exe` 和 `chess_hack.dll` 放在同一目录。
控制端启动后会自动创建游戏进程并完成 Early Bird APC 注入，点击按钮即可控制功能。
