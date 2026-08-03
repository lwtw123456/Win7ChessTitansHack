#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <CommCtrl.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "memoryeditor.hpp"
#include "safetyhook_manager.hpp"

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Ws2_32.lib")

namespace {

constexpr std::uintptr_t kResolveCandidateRva = 0x6A9AC;
constexpr std::uintptr_t kProcessMoveRva = 0x3BB3C;
constexpr std::uintptr_t kCommitMoveRva = 0x3C0E0;
constexpr std::uintptr_t kApplyMoveTemporarilyRva = 0x3C4E0;
constexpr std::uintptr_t kUndoTemporaryMoveRva = 0x3C6E0;
constexpr std::uintptr_t kCheckRepetitionRva = 0x3C394;
constexpr std::uintptr_t kCheckDrawConditionRva = 0x3D1C0;
constexpr std::uintptr_t kIsSquareAttackedRva = 0x3DC94;
constexpr std::uintptr_t kQueryPostMoveStateRva = 0x3E420;
constexpr std::uintptr_t kGameWindowRootRva = 0x19F830;
constexpr std::uintptr_t kGameRootRva = 0x19AAF8;

constexpr std::uintptr_t kRulesToGameStateOffset = 0x38;
constexpr std::uintptr_t kGameStateTurnOffset = 0x1C;
constexpr std::uintptr_t kGameStateHumanMoveOffset = 0x40;
constexpr std::uintptr_t kTemporaryMoveSideOffset = 0x14;
constexpr std::uintptr_t kTemporaryMoveSideZeroKingPositionOffset = 0x18;
constexpr std::uintptr_t kTemporaryMoveSideOneKingPositionOffset = 0x1C;
constexpr std::uintptr_t kPieceTypeVtableOffset = 0x20;
constexpr std::uintptr_t kProcessMoveSideOffset = 0x10;
constexpr std::uintptr_t kProcessMoveSideZeroPositionOffset = 0x18;
constexpr std::uintptr_t kProcessMoveSideOnePositionOffset = 0x1C;
constexpr std::uintptr_t kProcessMoveDrawCounterOffset = 0x49B20;
constexpr std::uintptr_t kProcessMoveStateFlagOffset = 0x49B30;

constexpr int kBoardWidth = 8;
constexpr int kBoardHeight = 8;
constexpr int kPawnPieceType = 1;
constexpr int kMoveExposesKingType = 3;
constexpr int kNormalMoveType = 6;
constexpr int kPromotionDefaultType = 12;

constexpr char kResolveCandidateHookName[] = "ResolveCandidate";
constexpr char kProcessMoveHookName[] = "ProcessMove";
constexpr char kForceCallResultPatchName[] = "ForceCallResult";
constexpr char kForceConditionResultPatchName[] = "ForceConditionResult";

constexpr unsigned short kControlPort = 27654;
constexpr std::size_t kMaxCommandLength = 128;

struct GameState;

struct Move {
    std::int32_t type;
    std::int32_t fromX;
    std::int32_t fromY;
    std::int32_t toX;
    std::int32_t toY;
};

struct EngineMove {
    std::int32_t fromCode;
    std::int32_t toCode;
    std::int32_t type;
    std::int32_t auxiliary;
};

using ResolveCandidateFn = bool(__fastcall*)(void* rules, void* position, Move* move);
using ProcessMoveFn = std::int32_t(__fastcall*)(
    GameState* game,
    const Move* request);
using CommitMoveFn = void(__fastcall*)(
    GameState* game,
    const EngineMove* move);
using ApplyMoveTemporarilyFn = void(__fastcall*)(
    GameState* game,
    const EngineMove* move);
using UndoTemporaryMoveFn = void(__fastcall*)(
    GameState* game);
using QueryPostMoveStateFn = EngineMove*(__fastcall*)(
    GameState* game,
    Move* request,
    std::int32_t option,
    EngineMove* fallback);
using CheckDrawConditionFn = bool(__fastcall*)(GameState* game);
using CheckRepetitionFn = std::int32_t(__fastcall*)(
    GameState* game,
    std::int32_t option);
using IsSquareAttackedFn = bool(__fastcall*)(
    GameState* game,
    std::int32_t position,
    std::int32_t side,
    std::uint32_t sideIsZero);
using GetPieceTypeFn = int(__fastcall*)(void* piece);

HMODULE g_gameModule = nullptr;
CommitMoveFn g_commitMove = nullptr;
ApplyMoveTemporarilyFn g_applyMoveTemporarily = nullptr;
UndoTemporaryMoveFn g_undoTemporaryMove = nullptr;
QueryPostMoveStateFn g_queryPostMoveState = nullptr;
CheckDrawConditionFn g_checkDrawCondition = nullptr;
CheckRepetitionFn g_checkRepetition = nullptr;
IsSquareAttackedFn g_isSquareAttacked = nullptr;
std::atomic_bool g_detaching = false;
std::atomic_bool g_resolveCandidateDetaching = false;
std::atomic_bool g_processMoveDetaching = false;
std::atomic_bool g_resolveCandidateHookInstalled = false;
std::atomic_bool g_processMoveHookInstalled = false;
std::atomic_bool g_stopSocketThread = false;
std::atomic<SOCKET> g_listenSocket = INVALID_SOCKET;
MemoryEditor::PatchManager g_patchManager;
std::mutex g_resultPatchMutex;

bool IsBoardCoordinate(int x, int y) noexcept {
    return x >= 0 && x < kBoardWidth && y >= 0 && y < kBoardHeight;
}

std::uintptr_t GetBoardPiece(void* position, int x, int y) {
    if (!position || !IsBoardCoordinate(x, y)) {
        return 0;
    }

    const auto index = static_cast<std::size_t>(x + y * kBoardWidth);
    const auto board = reinterpret_cast<std::uintptr_t>(position);

    return MemoryEditor::ReadValue<std::uintptr_t>(
        board + index * sizeof(std::uintptr_t));
}

int GetPieceType(std::uintptr_t piece) {
    const std::uintptr_t vtable =
        MemoryEditor::ReadValue<std::uintptr_t>(piece);
    if (!vtable) {
        return -1;
    }

    const std::uintptr_t functionAddress =
        MemoryEditor::ReadValue<std::uintptr_t>(
            vtable + kPieceTypeVtableOffset);
    if (!functionAddress) {
        return -1;
    }

    const auto function = reinterpret_cast<GetPieceTypeFn>(functionAddress);
    return function(reinterpret_cast<void*>(piece));
}

HWND GetGameWindow() {
    if (!g_gameModule) {
        return nullptr;
    }

    const std::uintptr_t windowHandleAddress =
        MemoryEditor::CalculatePointerChain(
            g_gameModule,
            kGameWindowRootRva,
            {0x68, 0x4D8, 0x170});

    return MemoryEditor::ReadValue<HWND>(
        windowHandleAddress);
}

void SelectPawnPromotion(Move* move) {
    if (!move) {
        return;
    }

    const HWND parent = GetGameWindow();
    const HINSTANCE instance = g_gameModule
        ? reinterpret_cast<HINSTANCE>(g_gameModule)
        : ::GetModuleHandleW(nullptr);

    const TASKDIALOG_BUTTON buttons[] = {
        {  2, MAKEINTRESOURCEW(0xE3) },
        { -2, MAKEINTRESOURCEW(0xE4) },
        { -3, MAKEINTRESOURCEW(0xE5) },
        { -4, MAKEINTRESOURCEW(0xE6) },
    };

    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hwndParent = parent;
    config.hInstance = instance;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS;

    if (parent && !::IsIconic(parent)) {
        config.dwFlags |= TDF_POSITION_RELATIVE_TO_WINDOW;
    }

    config.pszWindowTitle = MAKEINTRESOURCEW(0xE1);
    config.pszMainInstruction = MAKEINTRESOURCEW(0xE2);
    config.cButtons = static_cast<UINT>(_countof(buttons));
    config.pButtons = buttons;
    config.nDefaultButton = 2;

    int selectedButton = 0;
    move->type = kPromotionDefaultType;

    if (FAILED(::TaskDialogIndirect(
            &config,
            &selectedButton,
            nullptr,
            nullptr))) {
        return;
    }

    switch (selectedButton) {
    case -2:
        move->type = 11;
        break;
    case -3:
        move->type = 10;
        break;
    case -4:
        move->type = 9;
        break;
    default:
        move->type = kPromotionDefaultType;
        break;
    }
}

ResolveCandidateFn GetOriginalResolveCandidate() noexcept {
    try {
        return safetyhook_manager::global().original<ResolveCandidateFn>(
            kResolveCandidateHookName);
    } catch (...) {
        return nullptr;
    }
}

bool CallOriginalResolveCandidate(
    void* rules,
    void* position,
    Move* move) noexcept {
    const ResolveCandidateFn original = GetOriginalResolveCandidate();
    if (!original ||
        g_detaching.load(std::memory_order_acquire) ||
        g_resolveCandidateDetaching.load(std::memory_order_acquire)) {
        return false;
    }

    try {
        return original(rules, position, move);
    } catch (...) {
        return false;
    }
}

enum class HumanTurnResult {
    UseOriginal,
    AcceptCandidate,
    RejectCandidate,
};

std::int32_t EncodePosition(
    std::int32_t x,
    std::int32_t y) noexcept {
    return (y << 4) + x;
}

HumanTurnResult HandleHumanTurn(
    std::uintptr_t gameState,
    void* position,
    Move* move) {
    if (move->type >= kNormalMoveType) {
        return HumanTurnResult::AcceptCandidate;
    }

    if (!g_gameModule ||
        !g_applyMoveTemporarily ||
        !g_undoTemporaryMove ||
        !g_isSquareAttacked) {
        return HumanTurnResult::UseOriginal;
    }

    const std::uintptr_t selectedMove =
        MemoryEditor::ReadValue<std::uintptr_t>(
            gameState + kGameStateHumanMoveOffset);
    if (!selectedMove) {
        return HumanTurnResult::UseOriginal;
    }

    const std::int32_t selectedFromX = MemoryEditor::ReadValue<std::int32_t>(selectedMove + 0x18);
    const std::int32_t selectedFromY = MemoryEditor::ReadValue<std::int32_t>(selectedMove + 0x1C);
    const std::int32_t selectedToX = MemoryEditor::ReadValue<std::int32_t>(selectedMove + 0x10);
    const std::int32_t selectedToY = MemoryEditor::ReadValue<std::int32_t>(selectedMove + 0x14);

    EngineMove engineMove{};
    engineMove.fromCode =
        EncodePosition(selectedFromX, selectedFromY);
    engineMove.toCode =
        EncodePosition(selectedToX, selectedToY);
    engineMove.type = move->type;
    engineMove.auxiliary = 0;

    const std::uintptr_t gamePointerAddress =
        MemoryEditor::CalculatePointerChain(
            g_gameModule,
            kGameRootRva,
            {0x48, 0x18});

    GameState* game =
        MemoryEditor::ReadValue<GameState*>(
            gamePointerAddress);
    if (!game) {
        return HumanTurnResult::UseOriginal;
    }

    g_applyMoveTemporarily(game, &engineMove);

    bool kingAttacked = false;
    try {
        auto* gameBase =
            reinterpret_cast<std::uint8_t*>(game);

        const std::int32_t sideToCheck =
            MemoryEditor::ReadValue<std::int32_t>(
                reinterpret_cast<std::uintptr_t>(gameBase) +
                kTemporaryMoveSideOffset);

        const std::int32_t kingPosition =
            sideToCheck == 0
                ? MemoryEditor::ReadValue<std::int32_t>(
                    reinterpret_cast<std::uintptr_t>(gameBase) +
                    kTemporaryMoveSideZeroKingPositionOffset)
                : MemoryEditor::ReadValue<std::int32_t>(
                    reinterpret_cast<std::uintptr_t>(gameBase) +
                    kTemporaryMoveSideOneKingPositionOffset);

        kingAttacked = g_isSquareAttacked(
            game,
            kingPosition,
            sideToCheck,
            sideToCheck == 0 ? 1u : 0u);
    } catch (...) {
        g_undoTemporaryMove(game);
        throw;
    }

    g_undoTemporaryMove(game);

    if (kingAttacked) {
        move->type = kMoveExposesKingType;
        return HumanTurnResult::RejectCandidate;
    }

    move->fromX = selectedFromX;
    move->fromY = selectedFromY;
    move->toX = selectedToX;
    move->toY = selectedToY;

    const std::uintptr_t piece =
        GetBoardPiece(
            position,
            move->fromX,
            move->fromY);
    if (!piece) {
        return HumanTurnResult::UseOriginal;
    }

    const int pieceType = GetPieceType(piece);
    if (pieceType < 0) {
        return HumanTurnResult::UseOriginal;
    }

    if (pieceType != kPawnPieceType) {
        if (move->type < kNormalMoveType) {
            move->type = kNormalMoveType;
        }

        return HumanTurnResult::AcceptCandidate;
    }

    if (move->toY != 0) {
        move->type = kNormalMoveType;
        return HumanTurnResult::AcceptCandidate;
    }

    SelectPawnPromotion(move);
    return HumanTurnResult::AcceptCandidate;
}

bool __fastcall HookedResolveCandidate(
    void* rules,
    void* position,
    Move* move) noexcept {
    if (!rules || !position || !move ||
        g_detaching.load(std::memory_order_acquire) ||
        g_resolveCandidateDetaching.load(std::memory_order_acquire)) {
        return CallOriginalResolveCandidate(rules, position, move);
    }

    try {
        const std::uintptr_t rulesAddress =
            reinterpret_cast<std::uintptr_t>(rules);

        const std::uintptr_t gameState =
            rulesAddress - kRulesToGameStateOffset;

        const int turn = MemoryEditor::ReadValue<std::int32_t>(
            gameState + kGameStateTurnOffset);

        if (turn != 0) {
            return CallOriginalResolveCandidate(rules, position, move);
        }

        switch (HandleHumanTurn(gameState, position, move)) {
        case HumanTurnResult::AcceptCandidate:
            return true;
        case HumanTurnResult::RejectCandidate:
            return false;
        case HumanTurnResult::UseOriginal:
            break;
        }
    } catch (...) {
    }

    return CallOriginalResolveCandidate(rules, position, move);
}

ProcessMoveFn GetOriginalProcessMove() noexcept {
    try {
        return safetyhook_manager::global().original<ProcessMoveFn>(
            kProcessMoveHookName);
    } catch (...) {
        return nullptr;
    }
}

std::int32_t CallOriginalProcessMove(
    GameState* game,
    const Move* request) noexcept {
    const ProcessMoveFn original = GetOriginalProcessMove();
    if (!original ||
        g_detaching.load(std::memory_order_acquire) ||
        g_processMoveDetaching.load(std::memory_order_acquire)) {
        return 0;
    }

    try {
        return original(game, request);
    } catch (...) {
        return 0;
    }
}

std::int32_t __fastcall HookedProcessMove(
    GameState* game,
    const Move* request) noexcept {
    if (!game || !request ||
        g_detaching.load(std::memory_order_acquire) ||
        g_processMoveDetaching.load(std::memory_order_acquire) ||
        !g_commitMove ||
        !g_queryPostMoveState ||
        !g_checkDrawCondition ||
        !g_checkRepetition ||
        !g_isSquareAttacked) {
        return CallOriginalProcessMove(game, request);
    }

    bool moveCommitted = false;

    try {
        const auto gameState = reinterpret_cast<std::uintptr_t>(game);
        Move requestCopy = MemoryEditor::ReadValue<Move>(
            reinterpret_cast<std::uintptr_t>(request));

        EngineMove engineMove{};
        engineMove.fromCode = EncodePosition(
            requestCopy.fromX,
            requestCopy.fromY);
        engineMove.toCode = EncodePosition(
            requestCopy.toX,
            requestCopy.toY);
        engineMove.type = requestCopy.type;
        engineMove.auxiliary = 0;

        g_commitMove(game, &engineMove);
        moveCommitted = true;

        if (!MemoryEditor::WriteValue<std::uint8_t>(
                gameState + kProcessMoveStateFlagOffset,
                0)) {
            return 0;
        }

        EngineMove fallback{};
        EngineMove* result = g_queryPostMoveState(
            game,
            &requestCopy,
            0,
            &fallback);
        if (!result) {
            return 0;
        }

        const std::int32_t resultType =
            MemoryEditor::ReadValue<std::int32_t>(
                reinterpret_cast<std::uintptr_t>(result) +
                offsetof(EngineMove, type));

        if (resultType >= 0 && resultType <= 5) {
            const std::int32_t side =
                MemoryEditor::ReadValue<std::int32_t>(
                    gameState + kProcessMoveSideOffset);

            const std::uintptr_t positionOffset = side == 0
                ? kProcessMoveSideZeroPositionOffset
                : kProcessMoveSideOnePositionOffset;
            const std::int32_t position =
                MemoryEditor::ReadValue<std::int32_t>(
                    gameState + positionOffset);

            const bool attacked = g_isSquareAttacked(
                game,
                position,
                side,
                side == 0 ? 1u : 0u);

            return attacked ? 1 : 2;
        }

        if (!g_checkDrawCondition(game)) {
            return 5;
        }

        if (MemoryEditor::ReadValue<std::int32_t>(
                gameState + kProcessMoveDrawCounterOffset) == 100) {
            return 3;
        }

        if (g_checkRepetition(game, -1) == 2) {
            return 4;
        }

        return 0;
    } catch (...) {
        if (!moveCommitted) {
            return CallOriginalProcessMove(game, request);
        }
        return 0;
    }
}

bool InstallResolveCandidateHookInternal() noexcept {
    if (g_resolveCandidateHookInstalled.load(std::memory_order_acquire)) {
        return true;
    }

    if (!g_gameModule) {
        g_gameModule = ::GetModuleHandleW(nullptr);
    }
    if (!g_gameModule) {
        return false;
    }

    g_resolveCandidateDetaching.store(false, std::memory_order_release);

    const auto moduleBase = reinterpret_cast<std::uintptr_t>(g_gameModule);
    g_applyMoveTemporarily = reinterpret_cast<ApplyMoveTemporarilyFn>(
        moduleBase + kApplyMoveTemporarilyRva);
    g_undoTemporaryMove = reinterpret_cast<UndoTemporaryMoveFn>(
        moduleBase + kUndoTemporaryMoveRva);
    g_isSquareAttacked = reinterpret_cast<IsSquareAttackedFn>(
        moduleBase + kIsSquareAttackedRva);

    const auto target = reinterpret_cast<ResolveCandidateFn>(
        moduleBase + kResolveCandidateRva);
    auto result = safetyhook_manager::global().install_inline(
        kResolveCandidateHookName,
        target,
        &HookedResolveCandidate,
        true);

    if (!result) {
        g_applyMoveTemporarily = nullptr;
        g_undoTemporaryMove = nullptr;
        if (!g_processMoveHookInstalled.load(std::memory_order_acquire)) {
            g_isSquareAttacked = nullptr;
        }
        return false;
    }

    g_resolveCandidateHookInstalled.store(true, std::memory_order_release);
    return true;
}

bool InstallProcessMoveHookInternal() noexcept {
    if (g_processMoveHookInstalled.load(std::memory_order_acquire)) {
        return true;
    }

    if (!g_gameModule) {
        g_gameModule = ::GetModuleHandleW(nullptr);
    }
    if (!g_gameModule) {
        return false;
    }

    g_processMoveDetaching.store(false, std::memory_order_release);

    const auto moduleBase = reinterpret_cast<std::uintptr_t>(g_gameModule);
    g_commitMove = reinterpret_cast<CommitMoveFn>(
        moduleBase + kCommitMoveRva);
    g_queryPostMoveState = reinterpret_cast<QueryPostMoveStateFn>(
        moduleBase + kQueryPostMoveStateRva);
    g_checkDrawCondition = reinterpret_cast<CheckDrawConditionFn>(
        moduleBase + kCheckDrawConditionRva);
    g_checkRepetition = reinterpret_cast<CheckRepetitionFn>(
        moduleBase + kCheckRepetitionRva);
    g_isSquareAttacked = reinterpret_cast<IsSquareAttackedFn>(
        moduleBase + kIsSquareAttackedRva);

    const auto target = reinterpret_cast<ProcessMoveFn>(
        moduleBase + kProcessMoveRva);
    auto result = safetyhook_manager::global().install_inline(
        kProcessMoveHookName,
        target,
        &HookedProcessMove,
        true);

    if (!result) {
        g_commitMove = nullptr;
        g_queryPostMoveState = nullptr;
        g_checkDrawCondition = nullptr;
        g_checkRepetition = nullptr;
        if (!g_resolveCandidateHookInstalled.load(
                std::memory_order_acquire)) {
            g_isSquareAttacked = nullptr;
        }
        return false;
    }

    g_processMoveHookInstalled.store(true, std::memory_order_release);
    return true;
}

void UninstallResolveCandidateHookInternal() noexcept {
    g_resolveCandidateDetaching.store(true, std::memory_order_release);

    if (g_resolveCandidateHookInstalled.exchange(
            false,
            std::memory_order_acq_rel)) {
        try {
            (void)safetyhook_manager::global().uninstall(
                kResolveCandidateHookName);
        } catch (...) {
        }
    }

    g_applyMoveTemporarily = nullptr;
    g_undoTemporaryMove = nullptr;
    if (!g_processMoveHookInstalled.load(std::memory_order_acquire)) {
        g_isSquareAttacked = nullptr;
    }
}

void UninstallProcessMoveHookInternal() noexcept {
    g_processMoveDetaching.store(true, std::memory_order_release);

    if (!g_processMoveHookInstalled.exchange(
            false,
            std::memory_order_acq_rel)) {
        g_commitMove = nullptr;
        g_queryPostMoveState = nullptr;
        g_checkDrawCondition = nullptr;
        g_checkRepetition = nullptr;
        if (!g_resolveCandidateHookInstalled.load(
                std::memory_order_acquire)) {
            g_isSquareAttacked = nullptr;
        }
        return;
    }

    try {
        (void)safetyhook_manager::global().uninstall(kProcessMoveHookName);
    } catch (...) {
    }

    g_commitMove = nullptr;
    g_queryPostMoveState = nullptr;
    g_checkDrawCondition = nullptr;
    g_checkRepetition = nullptr;
    if (!g_resolveCandidateHookInstalled.load(std::memory_order_acquire)) {
        g_isSquareAttacked = nullptr;
    }
}


bool InstallHooks() noexcept {
    g_detaching.store(false, std::memory_order_release);

    const bool resolveAlreadyInstalled =
        g_resolveCandidateHookInstalled.load(std::memory_order_acquire);

    if (!InstallResolveCandidateHookInternal()) {
        return false;
    }

    if (!InstallProcessMoveHookInternal()) {
        if (!resolveAlreadyInstalled) {
            UninstallResolveCandidateHookInternal();
        }
        return false;
    }

    return true;
}

void UninstallHooks() noexcept {
    g_detaching.store(true, std::memory_order_release);
    UninstallProcessMoveHookInternal();
    UninstallResolveCandidateHookInternal();
}

bool EnableResultPatches() noexcept {
    std::lock_guard<std::mutex> lock(g_resultPatchMutex);

    try {
        const bool hasCallPatch =
            g_patchManager.Has(kForceCallResultPatchName);
        const bool hasConditionPatch =
            g_patchManager.Has(kForceConditionResultPatchName);

        if (hasCallPatch && hasConditionPatch) {
            if (!g_patchManager.Enable(kForceCallResultPatchName)) {
                return false;
            }

            if (!g_patchManager.Enable(kForceConditionResultPatchName)) {
                (void)g_patchManager.Disable(kForceCallResultPatchName);
                return false;
            }

            return true;
        }

        // Clear an incomplete group before resolving both targets again.
        if (hasCallPatch) {
            (void)g_patchManager.Remove(kForceCallResultPatchName);
        }
        if (hasConditionPatch) {
            (void)g_patchManager.Remove(kForceConditionResultPatchName);
        }

        if (!g_gameModule) {
            return false;
        }

        const MemoryEditor::PatternScan callResultScan{
            MemoryEditor::Pattern{
                "48 8D 4B 50 48 8D 54 24 30 E8 ?? ?? ?? ??"},
            MemoryEditor::Offset(9)};
        const MemoryEditor::PatternScan conditionResultScan{
            MemoryEditor::Pattern{"0F 94 C0 41 03 C5"}};

        void* const callResultTarget =
            callResultScan.Scan<void*>(g_gameModule);
        void* const conditionResultTarget =
            conditionResultScan.Scan<void*>(g_gameModule);

        if (!callResultTarget || !conditionResultTarget) {
            return false;
        }

        if (!g_patchManager.Add(
                kForceCallResultPatchName,
                callResultTarget,
                {0xB8, 0x01, 0x00, 0x00, 0x00},
                false)) {
            return false;
        }

        if (!g_patchManager.Add(
                kForceConditionResultPatchName,
                conditionResultTarget,
                {0xB8, 0x01, 0x00, 0x00, 0x00, 0x90},
                false)) {
            (void)g_patchManager.Remove(kForceCallResultPatchName);
            return false;
        }

        if (!g_patchManager.Enable(kForceCallResultPatchName) ||
            !g_patchManager.Enable(kForceConditionResultPatchName)) {
            (void)g_patchManager.Remove(kForceConditionResultPatchName);
            (void)g_patchManager.Remove(kForceCallResultPatchName);
            return false;
        }

        return true;
    } catch (...) {
        (void)g_patchManager.Remove(kForceConditionResultPatchName);
        (void)g_patchManager.Remove(kForceCallResultPatchName);
        return false;
    }
}

bool RestoreResultPatches() noexcept {
    std::lock_guard<std::mutex> lock(g_resultPatchMutex);

    bool restored = true;
    if (g_patchManager.Has(kForceConditionResultPatchName)) {
        restored =
            g_patchManager.Disable(kForceConditionResultPatchName) &&
            restored;
    }
    if (g_patchManager.Has(kForceCallResultPatchName)) {
        restored =
            g_patchManager.Disable(kForceCallResultPatchName) &&
            restored;
    }
    return restored;
}

std::string ResultPatchStatus() {
    std::lock_guard<std::mutex> lock(g_resultPatchMutex);

    const bool callEnabled =
        g_patchManager.IsEnabled(kForceCallResultPatchName);
    const bool conditionEnabled =
        g_patchManager.IsEnabled(kForceConditionResultPatchName);

    return std::string{"call="} +
        (callEnabled ? "1" : "0") +
        " condition=" +
        (conditionEnabled ? "1" : "0");
}

std::string NormalizeCommand(std::string command) {
    const auto first = command.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }

    const auto last = command.find_last_not_of(" \t\r\n");
    command = command.substr(first, last - first + 1);

    std::transform(
        command.begin(),
        command.end(),
        command.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
    return command;
}

std::string ExecuteCommand(std::string command) noexcept {
    try {
        command = NormalizeCommand(std::move(command));

        if (command == "open free move") {
            return InstallHooks()
                ? "OK\n"
                : "ERR install_failed\n";
        }

        if (command == "close free move") {
            UninstallHooks();
            return "OK\n";
        }

        if (command == "open win directly") {
            return EnableResultPatches()
                ? "OK\n"
                : "ERR patch_enable_failed\n";
        }

        if (command == "close win directly") {
            return RestoreResultPatches()
                ? "OK\n"
                : "ERR patch_restore_failed\n";
        }
        return "ERR unknown_command\n";
    } catch (...) {
        return "ERR internal_error\n";
    }
}

bool SendAll(SOCKET socket, std::string_view data) noexcept {
    std::size_t sentTotal = 0;
    while (sentTotal < data.size()) {
        const int sent = ::send(
            socket,
            data.data() + sentTotal,
            static_cast<int>(data.size() - sentTotal),
            0);
        if (sent == SOCKET_ERROR || sent == 0) {
            return false;
        }
        sentTotal += static_cast<std::size_t>(sent);
    }
    return true;
}

void HandleClient(SOCKET client) noexcept {
    try {
        DWORD timeoutMs = 1000;
        (void)::setsockopt(
            client,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeoutMs),
            static_cast<int>(sizeof(timeoutMs)));

        std::string command;
        command.reserve(kMaxCommandLength);

        char buffer[64]{};
        while (!g_stopSocketThread.load(std::memory_order_acquire) &&
               command.size() < kMaxCommandLength) {
            const int received = ::recv(
                client,
                buffer,
                static_cast<int>(sizeof(buffer)),
                0);
            if (received <= 0) {
                break;
            }

            command.append(buffer, static_cast<std::size_t>(received));
            if (command.find('\n') != std::string::npos) {
                break;
            }
        }

        if (command.size() >= kMaxCommandLength &&
            command.find('\n') == std::string::npos) {
            (void)SendAll(client, "ERR command_too_long\n");
            return;
        }

        const std::string response = ExecuteCommand(std::move(command));
        (void)SendAll(client, response);
    } catch (...) {
        (void)SendAll(client, "ERR internal_error\n");
    }
}

DWORD WINAPI SocketThreadProc(void*) noexcept {
    WSADATA winsockData{};
    if (::WSAStartup(MAKEWORD(2, 2), &winsockData) != 0) {
        return 0;
    }

    const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        ::WSACleanup();
        return 0;
    }

    BOOL exclusiveAddress = TRUE;
    (void)::setsockopt(
        listener,
        SOL_SOCKET,
        SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(&exclusiveAddress),
        static_cast<int>(sizeof(exclusiveAddress)));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = ::htons(kControlPort);
    address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);

    if (::bind(
            listener,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) == SOCKET_ERROR ||
        ::listen(listener, 1) == SOCKET_ERROR) {
        ::closesocket(listener);
        ::WSACleanup();
        return 0;
    }

    g_listenSocket.store(listener, std::memory_order_release);

    while (!g_stopSocketThread.load(std::memory_order_acquire)) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listener, &readSet);

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 250000;

        const int ready = ::select(0, &readSet, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR) {
            break;
        }
        if (ready == 0) {
            continue;
        }

        const SOCKET client = ::accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (g_stopSocketThread.load(std::memory_order_acquire)) {
                break;
            }
            continue;
        }

        HandleClient(client);
        ::shutdown(client, SD_BOTH);
        ::closesocket(client);
    }

    const SOCKET ownedListener = g_listenSocket.exchange(
        INVALID_SOCKET,
        std::memory_order_acq_rel);
    if (ownedListener != INVALID_SOCKET) {
        ::closesocket(ownedListener);
    }

    ::WSACleanup();
    return 0;
}

void StopSocketThread() noexcept {
    g_stopSocketThread.store(true, std::memory_order_release);

    const SOCKET listener = g_listenSocket.exchange(
        INVALID_SOCKET,
        std::memory_order_acq_rel);
    if (listener != INVALID_SOCKET) {
        ::closesocket(listener);
    }
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH: {
        g_gameModule = ::GetModuleHandleW(nullptr);
        g_stopSocketThread.store(false, std::memory_order_release);
        ::DisableThreadLibraryCalls(module);

        const HANDLE thread = ::CreateThread(
            nullptr,
            0,
            &SocketThreadProc,
            nullptr,
            0,
            nullptr);
        if (thread) {
            ::CloseHandle(thread);
        }
        break;
    }

    case DLL_PROCESS_DETACH:
        StopSocketThread();
        if (reserved == nullptr) {
            (void)RestoreResultPatches();
            UninstallHooks();
        }
        break;

    default:
        break;
    }

    return TRUE;
}
