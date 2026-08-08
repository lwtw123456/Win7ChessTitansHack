# Win7 Chess Titans Hack

一个针对 **Windows 7 x64 自带国际象棋 Chess Titans（`chess.exe`）** 的学习型逆向工程项目。

项目由 Flutter 控制端和进程内 Hook DLL 两部分组成。控制端以挂起状态启动游戏，通过 **Early Bird APC Injection** 在游戏进入正常初始化流程前加载 `chess_hack.dll`；DLL 随后复用逆向得到的内部函数、对象字段和状态流程，实现自由移动、兵随时升变、魅惑王后与移动即赢，并在魅惑成功后通过游戏内音频系统和 Direct3D 9 叠加层播放对应的声音与爱心效果。

> 当前代码中的 RVA、特征码和对象偏移均针对对应的 Windows 7 x64 Chess Titans 版本，不保证适用于其他系统版本或不同构建的 `chess.exe`。

## 功能

### 自由移动

解除棋子原有的常规走法限制，但不是简单地把合法性判断永久改成 `true`。

该功能通过两个 Inline Hook 接管候选走法与正式落子流程，并继续复用 Chess Titans 自己的棋局逻辑：

- 从游戏内部的选中走法对象恢复起点和终点；
- 构造引擎使用的内部走法结构；
- 临时落子，检查移动后己方王是否受攻击，再撤销试走；
- 禁止直接吃掉王，避免进入原游戏不会正常产生的棋局状态；
- 区分普通移动与兵升变，兵自由移动到底线时仍调用原游戏的升变选择窗口；
- 正式提交走法，并继续处理将军、将死、逼和、重复局面与半回合计数等状态。

国际象棋中，王理论上不会被吃，因此自由移动仍保留王安全检查和王不可被直接吃掉的限制，避免破坏引擎内部状态。

### 指兵为后

允许玩家不必将兵走到底线，而是在自己的回合中直接将棋盘上的己方兵升变为王后。

该功能 Hook 玩家操作处理函数，并同步修改游戏表现层与 AI 棋盘：

- 在玩家首次选中或切换选中到己方兵时触发；
- 先在 AI 棋盘中临时将该兵替换为王后；
- 使用原游戏的攻击判断检查升变后是否会直接使对方王处于被攻击状态；
- 如果会导致这种异常状态，则恢复兵并调用游戏内部提示接口显示“无法升变”；
- 检查通过后，在棋盘对象中创建对应颜色的王后对象；
- 向原游戏动画事件数组加入替换事件，并保持 AI 棋盘与显示棋盘同步。

### 魅惑王后

修改玩家王后的吃子行为。当玩家王后尝试吃掉一个非王棋子时，不再执行普通吃子，而是将目标棋子**魅惑为己方棋子**。

魅惑流程会同时维护 Chess Titans 的多套内部状态：

- 仅在玩家回合且移动棋子为王后时处理；
- 不允许魅惑王；
- 保留目标棋子的原始类型，只改变其所属阵营；
- 在显示棋盘中重新构造对应棋子对象；
- 同步修改 AI 棋盘中的棋子颜色、阵营信息与 Zobrist Hash；
- 将当前棋盘加入历史记录并切换回合；
- 继续调用原游戏的局面结果、和棋与重复局面检查流程；
- 王后本身留在原位置，被魅惑棋子留在目标格，该次魅惑计为一次完整行动。

魅惑成功后还会触发两项表现效果：

- **爱心叠加**：Hook Direct3D 9 的设备创建与 `EndScene`，读取游戏自身的 View / Projection Matrix，将目标棋子的棋盘坐标投影到屏幕，在棋子上方显示约 1 秒的爱心；
- **成功音效**：通过游戏内部 `CSoundManager::CreateFromMemory2` 从 DLL 资源创建声音，并调用游戏自身的 `CSound::Play` 播放 `succubus.wav`。

### 移动即赢

通过 AOB 特征码定位两个结果计算点，将对应结果强制设为 `1`，使玩家完成移动后直接进入胜利流程。

两个补丁作为一组启用和恢复，关闭功能后会写回原始指令。

## 逆向内容概述

项目并非只修改单个条件跳转，而是对 Chess Titans 的走棋链、棋盘对象、渲染状态和声音系统进行了部分恢复。目前代码中语义化使用了以下内部函数：

| 语义名称 | RVA | 用途 |
| --- | ---: | --- |
| `ResolveCandidate` | `0x6A9AC` | 解析并判断玩家当前候选走法 |
| `HandleAction` | `0x6BC60` | 处理玩家选中棋子等操作，用于兵随时升变 |
| `ProcessMove` / `AIBoardNotifyMove` | `0x3BB3C` | 处理已接受走法与局面结果 |
| `CommitMove` | `0x3C0E0` | 正式提交内部走法 |
| `ApplyMoveTemporarily` | `0x3C4E0` | 临时应用走法 |
| `UndoTemporaryMove` | `0x3C6E0` | 撤销临时走法 |
| `CheckRepetition` | `0x3C394` | 检查重复局面 |
| `CheckDrawCondition` | `0x3D1C0` | 检查落子后的和棋相关状态 |
| `IsSquareAttacked` | `0x3DC94` | 判断指定格是否受攻击 |
| `QueryPostMoveState` | `0x3E420` | 查询正式落子后的局面状态 |
| `SwapPiece` | `0x3CA6C` | 修改 AI 棋盘中的棋子类型 |
| `AIHashFlipPiece` | `0x3D2E8` | 更新 AI Hash 中的棋子状态 |
| `AnimationEventAdd` | `0x696E0` | 向游戏动画事件数组加入事件 |
| `SetSizeInternal` | `0x5FA04` | 扩展游戏内部棋盘历史数组 |
| `ShowTip` | `0x6795C` | 复用游戏提示系统显示自定义提示 |
| `CSoundManager::CreateFromMemory2` | `0x61A10` | 从 DLL 内嵌 WAV 内存创建游戏声音对象 |
| `CSound::Play` | `0x61F80` | 使用游戏自己的声音对象播放音效 |
| `OnCreateDevice` | `0x04D8C8` | 获取游戏创建的 Direct3D 9 Device，并安装叠加层 Hook |

同时恢复并使用了部分内部数据布局：

- 外部候选走法 `Move`；
- 引擎内部走法 `EngineMove`；
- 64 项棋子指针棋盘；
- `Piece` 对象的虚表、颜色、移动状态等字段；
- 玩家当前选中走法对象的起点、终点、阵营与状态字段；
- 棋局引擎对象中的回合、王位置、和棋计数与状态字段；
- AI 棋盘中的棋子类型、颜色与 Zobrist Hash 状态；
- 游戏棋盘历史数组与动画事件数组；
- 棋子对象虚表中的类型查询函数；
- `(y << 4) + x` 形式的位置编码；
- 游戏对象与窗口句柄的全局指针链；
- 游戏内 View / Projection Matrix；
- `Game` 对象中内嵌的 `CSoundManager`。

项目中的函数名和结构体名均为根据调用关系、参数、返回值及内存访问行为添加的语义名称，并非微软原始符号。

## 图形与音频资源

### 爱心 DDS

爱心不是创建额外窗口绘制，而是直接复用游戏的 Direct3D 9 Device：

```text
棋盘坐标
    ↓
读取 Game 中的 View / Projection Matrix
    ↓
投影为屏幕坐标
    ↓
EndScene 中绘制 60×60 左右的透明纹理
```

当前资源固定为 **64×64 DXT5 DDS**。DLL 中的轻量 DDS loader 只处理该项目实际需要的传统 DDS / DXT5 格式，将 BC3 压缩块直接上传到 `D3DFMT_DXT5` 纹理，不在 CPU 侧解压。

### 魅惑音效

`succubus.wav` 作为 `RCDATA` 编译进 DLL。播放时不调用系统 `PlaySound` 或额外播放器，而是复用 Chess Titans 自己已经初始化的声音管理器：

```text
DLL RCDATA
    ↓
CSoundManager::CreateFromMemory2
    ↓
CSound
    ↓
CSound::Play
```

因此音效与游戏现有音频设备走同一套内部播放链。

## Early Bird APC Injection

控制端使用 **Early Bird APC Injection**：

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
- SafetyHook Inline Hook、Mid Hook 与 trampoline
- Zydis 指令解析
- AOB 特征码扫描与运行时代码补丁
- 指针链、虚表与部分 C++ 对象布局恢复
- `TaskDialogIndirect` 复用游戏资源实现正常底线升变选择
- 游戏内部 AI Board / Hash / Animation Event 状态同步
- Direct3D 9 `EndScene` 叠加绘制与世界坐标投影
- DXT5 / BC3 DDS 内嵌纹理资源
- 游戏内部 `CSoundManager` 内存 WAV 加载与播放
- Win32 Resource / `RCDATA`
- `127.0.0.1:27654` 本地 TCP 控制通道

## 控制命令

DLL 在回环地址 `127.0.0.1:27654` 监听以下文本命令：

| 命令 | 功能 |
| --- | --- |
| `Open free move` | 启用自由移动 |
| `Close free move` | 关闭自由移动 |
| `Open pawn promotion` | 启用兵随时升变 |
| `Close pawn promotion` | 关闭兵随时升变 |
| `Open succubus queen` | 启用魅惑王后 |
| `Close succubus queen` | 关闭魅惑王后 |
| `Open win directly` | 启用移动即赢 |
| `Close win directly` | 关闭移动即赢 |

爱心渲染相关 Hook 会在 DLL 初始化时安装，但只有魅惑王后成功时才会显示爱心；`succubus.wav` 同样只在魅惑成功后播放，因此不需要单独的控制命令。

## 如何使用

将控制端可执行文件、`chess.exe` 和 `chess_hack.dll` 放在同一目录。

控制端启动后会自动创建游戏进程并完成 Early Bird APC 注入，随后通过本地 TCP 控制通道启用或关闭对应功能。
