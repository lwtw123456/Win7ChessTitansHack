#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <CommCtrl.h>
#include <d3d9.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
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
constexpr std::uintptr_t kHandleActionRva = 0x6BC60;
constexpr std::uintptr_t kAIBoardNotifyMoveRva = 0x3BB3C;
constexpr std::uintptr_t kAIBoardCommitMoveRva = 0x3C0E0;
constexpr std::uintptr_t kAIBoardApplyMoveTemporarilyRva = 0x3C4E0;
constexpr std::uintptr_t kAIBoardUndoTemporaryMoveRva = 0x3C6E0;
constexpr std::uintptr_t kAIBoardCheckRepetitionRva = 0x3C394;
constexpr std::uintptr_t kAIBoardCheckDrawConditionRva = 0x3D1C0;
constexpr std::uintptr_t kAIBoardIsSquareAttackedRva = 0x3DC94;
constexpr std::uintptr_t kAIBoardQueryPostMoveStateRva = 0x3E420;
constexpr std::uintptr_t kAIBoardSwapPieceRva = 0x3CA6C;
constexpr std::uintptr_t kAIHashFlipPieceRva = 0x3D2E8;
constexpr std::uintptr_t kAnimationEventAddRva = 0x696E0;
constexpr std::uintptr_t kSetSizeInternalRva = 0x5FA04;
constexpr std::uintptr_t kGameStrCtorRva = 0x16E414;
constexpr std::uintptr_t kShowTipRva = 0x6795C;
constexpr std::uintptr_t kOperatorNewRva = 0x179D98;
constexpr std::uintptr_t kOperatorDeleteRva = 0x179D10;
constexpr std::uintptr_t kPawnVtableRva = 0x25290;
constexpr std::uintptr_t kKnightVtableRva = 0x252C0;
constexpr std::uintptr_t kBishopVtableRva = 0x252F0;
constexpr std::uintptr_t kRookVtableRva = 0x25320;
constexpr std::uintptr_t kQueenVtableRva = 0x25350;
constexpr std::uintptr_t kGameWindowRootRva = 0x19F830;
constexpr std::uintptr_t kGameRootRva = 0x19AAF8;
constexpr std::uintptr_t kOnCreateDeviceRva = 0x04D8C8;

constexpr std::uintptr_t kCreateFromMemory2Rva = 0x61A10;
constexpr std::uintptr_t kCSoundPlayRva = 0x61F80;
constexpr std::uintptr_t kGameSoundManagerOffset = 0x49DA0;
constexpr int kSuccubusWavResourceId = 201;

constexpr std::uintptr_t kGameRulesOffset = 0x38;
constexpr std::uintptr_t kGameTurnOffset = 0x1C;
constexpr std::uintptr_t kGameHumanMoveStateOffset = 0x40;
constexpr std::uintptr_t kGameAIBoardOffset = 0x50;
constexpr std::uintptr_t kGameBoardArrayOffset = 0x28;
constexpr std::uintptr_t kGameBoardCountOffset = 0x30;
constexpr std::uintptr_t kGameAnimationEventsOffset = 0x49E80;
constexpr std::uintptr_t kGameViewMatrixOffset = 0x49E00;
constexpr std::uintptr_t kGameProjectionMatrixOffset = 0x49E40;

constexpr std::uintptr_t kHumanMoveStateSideOffset = 0x08;
constexpr std::uintptr_t kHumanMoveStateToXOffset = 0x10;
constexpr std::uintptr_t kHumanMoveStateToYOffset = 0x14;
constexpr std::uintptr_t kHumanMoveStateFromXOffset = 0x18;
constexpr std::uintptr_t kHumanMoveStateFromYOffset = 0x1C;
constexpr std::uintptr_t kHumanMoveStateStateOffset = 0x28;

constexpr std::uintptr_t kAIBoardPostMoveSideOffset = 0x10;
constexpr std::uintptr_t kAIBoardTemporarySideOffset = 0x14;
constexpr std::uintptr_t kAIBoardSideZeroPositionOffset = 0x18;
constexpr std::uintptr_t kAIBoardSideOnePositionOffset = 0x1C;
constexpr std::uintptr_t kAIBoardDrawCounterOffset = 0x49B20;
constexpr std::uintptr_t kAIBoardStateFlagOffset = 0x49B30;
constexpr std::uintptr_t kAIBoardHashOffset = 0x08;
constexpr std::uintptr_t kAIBoardPieceTypesOffset = 0x20;
constexpr std::uintptr_t kAIBoardPieceColorsOffset = 0x220;
constexpr std::uintptr_t kAIHashSideToMoveKeyOffset = 0x5400;
constexpr std::uintptr_t kAIHashCurrentHashOffset = 0x5408;

constexpr std::uintptr_t kPieceTypeVtableOffset = 0x20;

constexpr int kBoardWidth = 8;
constexpr int kBoardHeight = 8;
constexpr int kPawnPieceType = 1;
constexpr int kQueenPieceType = 5;
constexpr int kPawnPieceFlag = 1;
constexpr int kKnightPieceFlag = 2;
constexpr int kBishopPieceFlag = 4;
constexpr int kRookPieceFlag = 8;
constexpr int kQueenPieceFlag = 16;
constexpr int kKingPieceFlag = 32;
constexpr std::uintptr_t kSuccubusQueenSkipBytes = 114;
constexpr int kMoveExposesKingType = 3;
constexpr int kNormalMoveType = 6;
constexpr int kPromotionDefaultType = 12;
constexpr int kHumanMoveStateFirstSelectionState = 1;
constexpr int kHumanMoveStatePieceSelectedState = 2;

constexpr char kResolveCandidateHookName[] = "ResolveCandidate";
constexpr char kAIBoardNotifyMoveHookName[] = "ProcessMove";
constexpr char kPawnPromotionHookName[] = "PawnPromotion";
constexpr char kSuccubusQueenHookName[] = "SuccubusQueen";
constexpr char kForceCallResultPatchName[] = "ForceCallResult";
constexpr char kForceConditionResultPatchName[] = "ForceConditionResult";
constexpr char kHeartOnCreateDeviceHookName[] = "HeartOnCreateDevice";
constexpr char kHeartEndSceneHookName[] = "HeartEndScene";

constexpr unsigned short kControlPort = 27654;
constexpr std::size_t kMaxCommandLength = 128;

struct Game;
struct GameRules;
struct HumanMoveState;
struct DecalMgr;
namespace AI {
struct Board;
struct Hash;
}
struct Board;
struct AnimationEventArray;
struct CSound;
struct CSoundManager;

struct HeartVec3 {
    float x;
    float y;
    float z;
};

struct HeartMatrix {
    float m[4][4];
};

struct Piece {
    void* vtable;
    std::int32_t color;
    std::uint8_t moved;
    std::uint8_t padding[3];
    double removedTime;
};

struct alignas(8) AnimationEvent {
    std::int32_t type;
    std::int32_t padding04;
    Piece* piece;
    std::int32_t fromX;
    std::int32_t fromY;
    std::int32_t toX;
    std::int32_t toY;
    float state;
    std::int32_t padding24;
};

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

struct GameStr {
    std::uint64_t length;
    std::uint64_t capacity;
    wchar_t* data;
};
static_assert(sizeof(GameStr) == 0x18);

using ResolveCandidateFn = bool(__fastcall*)(
    GameRules* rules,
    Board* position,
    Move* move);
using HandleActionFn = char(__fastcall*)(
    HumanMoveState* moveState,
    const Board* board);
using AIBoardNotifyMoveFn = std::int32_t(__fastcall*)(
    AI::Board* aiBoard,
    const Move* request);
using AIBoardCommitMoveFn = void(__fastcall*)(
    AI::Board* aiBoard,
    const EngineMove* move);
using AIBoardApplyMoveTemporarilyFn = void(__fastcall*)(
    AI::Board* aiBoard,
    const EngineMove* move);
using AIBoardUndoTemporaryMoveFn = void(__fastcall*)(
    AI::Board* aiBoard);
using AIBoardQueryPostMoveStateFn = EngineMove*(__fastcall*)(
    AI::Board* aiBoard,
    Move* request,
    std::int32_t option,
    EngineMove* fallback);
using AIBoardCheckDrawConditionFn = bool(__fastcall*)(
    AI::Board* aiBoard);
using AIBoardCheckRepetitionFn = std::int32_t(__fastcall*)(
    AI::Board* aiBoard,
    std::int32_t option);
using AIBoardIsSquareAttackedFn = bool(__fastcall*)(
    AI::Board* aiBoard,
    std::int32_t position,
    std::int32_t side,
    std::uint32_t sideIsZero);
using GetPieceTypeFn = int(__fastcall*)(Piece* piece);
using OperatorNewFn = void*(__fastcall*)(std::size_t size);
using OperatorDeleteFn = void(__fastcall*)(void* pointer);
using GameStrCtorFn = GameStr*(__fastcall*)(
    GameStr* self,
    const wchar_t* text);
using ShowTipFn = void(__fastcall*)(
    void* game,
    int bubbleType,
    const GameStr* title,
    const GameStr* body,
    float duration);
using AIBoardSwapPieceFn = void(__fastcall*)(
    AI::Board* aiBoard,
    int square,
    int newPieceType);
using AnimationEventAddFn = HRESULT(__fastcall*)(
    AnimationEventArray* animationEvents,
    const AnimationEvent* event);
using AIHashFlipPieceFn = void(__fastcall*)(
    AI::Hash* hash,
    unsigned int color,
    int piece,
    unsigned int square);
using SetSizeInternalFn = std::int64_t(__fastcall*)(
    void* arrayObject,
    std::int32_t requestedSize);
using OnCreateDeviceFn = HRESULT(__fastcall*)(
    DecalMgr* self,
    IDirect3DDevice9* device);
using EndSceneFn = HRESULT(__fastcall*)(
    IDirect3DDevice9* device);
using CreateFromMemory2Fn = HRESULT(__fastcall*)(
    CSoundManager* manager,
    CSound** outSound,
    unsigned char* wavData,
    unsigned int wavSize,
    const GUID* guid);
using CSoundPlayFn = HRESULT(__fastcall*)(
    CSound* sound,
    DWORD priority,
    DWORD flags,
    LONG volume,
    LONG frequency,
    LONG pan);

struct AIBoardPostMoveFunctions {
    AIBoardQueryPostMoveStateFn queryPostMoveState;
    AIBoardCheckDrawConditionFn checkDrawCondition;
    AIBoardCheckRepetitionFn checkRepetition;
    AIBoardIsSquareAttackedFn isSquareAttacked;
};

struct HookState {
    std::atomic_bool detaching{false};
    std::atomic_bool installed{false};

    [[nodiscard]] bool IsDetaching() const noexcept {
        return detaching.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool IsInstalled() const noexcept {
        return installed.load(std::memory_order_acquire);
    }

    void PrepareInstall() noexcept {
        detaching.store(false, std::memory_order_release);
    }

    void MarkInstalled() noexcept {
        installed.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool BeginUninstall() noexcept {
        detaching.store(true, std::memory_order_release);
        return installed.exchange(false, std::memory_order_acq_rel);
    }
};

HMODULE g_gameModule = nullptr;
HMODULE g_OurDll = nullptr;
std::uintptr_t g_succubusQueenHookTarget = 0;
std::atomic_bool g_freeMoveDetaching = false;
HookState g_resolveCandidateHook;
HookState g_aiBoardNotifyMoveHook;
HookState g_pawnPromotionHook;
HookState g_succubusQueenHook;
HookState g_heartOnCreateDeviceHook;
HookState g_heartEndSceneHook;
std::atomic_bool g_stopSocketThread = false;
std::atomic<SOCKET> g_listenSocket = INVALID_SOCKET;
MemoryEditor::PatchManager g_patchManager;
std::mutex g_resultPatchMutex;

CSound* g_succubusSound = nullptr;
std::mutex g_succubusSoundMutex;

bool IsBoardCoordinate(int x, int y) noexcept {
    return x >= 0 && x < kBoardWidth && y >= 0 && y < kBoardHeight;
}

OnCreateDeviceFn g_origHeartOnCreateDevice = nullptr;
EndSceneFn g_origHeartEndScene = nullptr;
IDirect3DDevice9* g_heartDevice = nullptr;
IDirect3DTexture9* g_heartOverlayTexture = nullptr;
std::atomic_bool g_heartReady = false;

struct HeartState {
    std::atomic<int> x{-1};
    std::atomic<int> y{-1};
    std::atomic<ULONGLONG> expireAt{0};
};

HeartState g_heart;

Game* GetCurrentGame();
Piece* GetBoardPiece(int x, int y);
int GetPieceType(Piece* piece);

void ShowHeart(int x, int y) noexcept {
    if (!IsBoardCoordinate(x, y)) {
        return;
    }

    g_heart.x.store(x, std::memory_order_relaxed);
    g_heart.y.store(y, std::memory_order_relaxed);
    g_heart.expireAt.store(
        ::GetTickCount64() + 1000,
        std::memory_order_release);
}

std::uintptr_t AddressOf(const void* pointer) noexcept {
    return reinterpret_cast<std::uintptr_t>(pointer);
}

bool EnsureGameModule() noexcept {
    if (!g_gameModule) {
        g_gameModule = ::GetModuleHandleW(nullptr);
    }
    return g_gameModule != nullptr;
}

template <typename Fn>
Fn GameFunction(std::uintptr_t rva) noexcept {
    if (!g_gameModule) {
        return nullptr;
    }
    return reinterpret_cast<Fn>(AddressOf(g_gameModule) + rva);
}

AIBoardPostMoveFunctions GetAIBoardPostMoveFunctions() noexcept {
    return {
        GameFunction<AIBoardQueryPostMoveStateFn>(
            kAIBoardQueryPostMoveStateRva),
        GameFunction<AIBoardCheckDrawConditionFn>(
            kAIBoardCheckDrawConditionRva),
        GameFunction<AIBoardCheckRepetitionFn>(
            kAIBoardCheckRepetitionRva),
        GameFunction<AIBoardIsSquareAttackedFn>(
            kAIBoardIsSquareAttackedRva),
    };
}

template <typename Fn>
Fn GetOriginalHook(std::string_view name) noexcept {
    try {
        return safetyhook_manager::global().original<Fn>(name);
    } catch (...) {
        return nullptr;
    }
}


bool InitHeartAddresses() noexcept {
    return EnsureGameModule();
}

static bool ReadGameViewMatrix(HeartMatrix& out) noexcept {
    if (!g_gameModule) {
        return false;
    }

    const std::uintptr_t moduleBase = AddressOf(g_gameModule);
    Game* const game = *reinterpret_cast<Game**>(
        moduleBase + kGameRootRva);
    if (!game) {
        return false;
    }

    const auto* const src = reinterpret_cast<const HeartMatrix*>(
        AddressOf(game) + kGameViewMatrixOffset);
    if (!src) {
        return false;
    }

    out = *src;

    const float rightLenSq =
        out.m[0][0] * out.m[0][0] +
        out.m[1][0] * out.m[1][0] +
        out.m[2][0] * out.m[2][0];
    const float upLenSq =
        out.m[0][1] * out.m[0][1] +
        out.m[1][1] * out.m[1][1] +
        out.m[2][1] * out.m[2][1];

    return std::isfinite(rightLenSq) &&
        std::isfinite(upLenSq) &&
        rightLenSq > 0.0001f &&
        upLenSq > 0.0001f;
}

struct HeartVec4 {
    float x;
    float y;
    float z;
    float w;
};

struct HeartOverlayVertex {
    float x;
    float y;
    float z;
    float rhw;
    float u;
    float v;
};
static_assert(sizeof(HeartOverlayVertex) == 24);

static bool ReadGameProjectionMatrix(HeartMatrix& out) noexcept {
    if (!g_gameModule) {
        return false;
    }

    const std::uintptr_t moduleBase = AddressOf(g_gameModule);
    Game* const game = *reinterpret_cast<Game**>(
        moduleBase + kGameRootRva);
    if (!game) {
        return false;
    }

    out = *reinterpret_cast<const HeartMatrix*>(
        AddressOf(game) + kGameProjectionMatrixOffset);

    return std::isfinite(out.m[0][0]) &&
        std::isfinite(out.m[1][1]) &&
        std::isfinite(out.m[2][2]) &&
        std::fabs(out.m[0][0]) > 0.0001f &&
        std::fabs(out.m[1][1]) > 0.0001f;
}

static HeartVec4 TransformHeartPoint(
    const HeartVec4& p,
    const HeartMatrix& m) noexcept {
    return HeartVec4{
        p.x * m.m[0][0] + p.y * m.m[1][0] +
            p.z * m.m[2][0] + p.w * m.m[3][0],
        p.x * m.m[0][1] + p.y * m.m[1][1] +
            p.z * m.m[2][1] + p.w * m.m[3][1],
        p.x * m.m[0][2] + p.y * m.m[1][2] +
            p.z * m.m[2][2] + p.w * m.m[3][2],
        p.x * m.m[0][3] + p.y * m.m[1][3] +
            p.z * m.m[2][3] + p.w * m.m[3][3]
    };
}

static float HeartAnchorHeightForPieceType(int pieceType) noexcept {
    switch (pieceType) {
    case kPawnPieceFlag:
        return 8.0f;
    case kKnightPieceFlag:
        return 10.0f;
    case kBishopPieceFlag:
        return 10.8f;
    case kRookPieceFlag:
        return 9.2f;
    case kQueenPieceFlag:
        return 11.5f;
    default:
        return 9.5f;
    }
}

static bool ProjectHeartAnchorToScreen(
    IDirect3DDevice9* device,
    const HeartVec3& world,
    float& outX,
    float& outY) noexcept {
    if (!device) {
        return false;
    }

    HeartMatrix view{};
    HeartMatrix projection{};
    if (!ReadGameViewMatrix(view) ||
        !ReadGameProjectionMatrix(projection)) {
        return false;
    }

    HeartVec4 p{world.x, world.y, world.z, 1.0f};
    p = TransformHeartPoint(p, view);
    p = TransformHeartPoint(p, projection);

    if (!std::isfinite(p.w) || p.w <= 0.0001f) {
        return false;
    }

    const float ndcX = p.x / p.w;
    const float ndcY = p.y / p.w;
    if (!std::isfinite(ndcX) || !std::isfinite(ndcY)) {
        return false;
    }

    D3DVIEWPORT9 viewport{};
    if (FAILED(device->GetViewport(&viewport)) ||
        viewport.Width == 0 || viewport.Height == 0) {
        return false;
    }

    outX = static_cast<float>(viewport.X) +
        (ndcX + 1.0f) * 0.5f * static_cast<float>(viewport.Width);
    outY = static_cast<float>(viewport.Y) +
        (1.0f - ndcY) * 0.5f * static_cast<float>(viewport.Height);
    return true;
}

static bool EnsureHeartOverlayTexture(IDirect3DDevice9* device) noexcept {
    if (g_heartOverlayTexture) {
        return true;
    }
    if (!device || !g_OurDll) {
        return false;
    }

    HRSRC resource = FindResourceW(
        g_OurDll,
        L"HEART_DDS",
        L"DATA");
    if (!resource) {
        return false;
    }

    const DWORD resourceSize = SizeofResource(g_OurDll, resource);
    HGLOBAL loaded = LoadResource(g_OurDll, resource);
    const auto* bytes = static_cast<const std::byte*>(
        loaded ? LockResource(loaded) : nullptr);

    if (!bytes || resourceSize < 128 + 4 ||
        std::memcmp(bytes, "DDS ", 4) != 0) {
        return false;
    }

    auto readU32 = [bytes](std::size_t offset) noexcept {
        std::uint32_t value{};
        std::memcpy(&value, bytes + offset, sizeof(value));
        return value;
    };

    const std::uint32_t height = readU32(12);
    const std::uint32_t width = readU32(16);
    const std::uint32_t pixelFormatFlags = readU32(80);
    const std::uint32_t fourCC = readU32(84);

    constexpr std::uint32_t kDdpfFourCC = 0x4;
    constexpr auto makeFourCC = [](char a, char b, char c, char d) noexcept {
        return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24);
    };

    if (width == 0 || height == 0 ||
        (pixelFormatFlags & kDdpfFourCC) == 0 ||
        fourCC != makeFourCC('D', 'X', 'T', '5')) {
        return false;
    }

    constexpr std::size_t kDxt5BlockBytes = 16u;
    const std::size_t blockColumns =
        (static_cast<std::size_t>(width) + 3u) / 4u;
    const std::size_t rowBytes = blockColumns * kDxt5BlockBytes;
    const std::size_t rowCount =
        (static_cast<std::size_t>(height) + 3u) / 4u;

    constexpr std::size_t kDdsDataOffset = 128u;
    const std::size_t pixelBytes = rowBytes * rowCount;
    if (pixelBytes > static_cast<std::size_t>(resourceSize) - kDdsDataOffset) {
        return false;
    }

    IDirect3DTexture9* texture = nullptr;
    const HRESULT createHr = device->CreateTexture(
        width,
        height,
        1,
        0,
        D3DFMT_DXT5,
        D3DPOOL_MANAGED,
        &texture,
        nullptr);
    if (FAILED(createHr) || !texture) {
        return false;
    }

    D3DLOCKED_RECT locked{};
    const HRESULT lockHr = texture->LockRect(0, &locked, nullptr, 0);
    if (FAILED(lockHr) || !locked.pBits) {
        texture->Release();
        return false;
    }

    const auto* src = bytes + kDdsDataOffset;
    auto* dst = static_cast<std::byte*>(locked.pBits);
    if (locked.Pitch <= 0 ||
        static_cast<std::size_t>(locked.Pitch) < rowBytes) {
        texture->UnlockRect(0);
        texture->Release();
        return false;
    }
    const std::size_t dstPitch = static_cast<std::size_t>(locked.Pitch);

    for (std::size_t y = 0; y < rowCount; ++y) {
        std::memcpy(
            dst + y * dstPitch,
            src + y * rowBytes,
            rowBytes);
    }
    texture->UnlockRect(0);

    g_heartOverlayTexture = texture;
    return true;
}

static void RenderHeartOverlay(
    IDirect3DDevice9* device,
    int boardX,
    int boardY) noexcept {
    if (!device || !IsBoardCoordinate(boardX, boardY) ||
        !EnsureHeartOverlayTexture(device)) {
        return;
    }

    Piece* const piece = GetBoardPiece(boardX, boardY);
    const int pieceType = GetPieceType(piece);
    const float anchorHeight = HeartAnchorHeightForPieceType(pieceType);

    const HeartVec3 anchor{
        static_cast<float>(boardX) * 5.0f - 17.5f,
        anchorHeight,
        static_cast<float>(boardY) * 5.0f - 17.5f
    };

    float screenX = 0.0f;
    float screenY = 0.0f;
    if (!ProjectHeartAnchorToScreen(
            device,
            anchor,
            screenX,
            screenY)) {
        return;
    }

    constexpr float kHalfWidthPx = 30.0f;
    constexpr float kHalfHeightPx = 30.0f;
    constexpr float kPixelCenter = -0.5f;

    const float left = screenX - kHalfWidthPx + kPixelCenter;
    const float right = screenX + kHalfWidthPx + kPixelCenter;
    const float top = screenY - kHalfHeightPx + kPixelCenter;
    const float bottom = screenY + kHalfHeightPx + kPixelCenter;

    const HeartOverlayVertex vertices[4]{
        {left,  top,    0.0f, 1.0f, 0.0f, 0.0f},
        {right, top,    0.0f, 1.0f, 1.0f, 0.0f},
        {left,  bottom, 0.0f, 1.0f, 0.0f, 1.0f},
        {right, bottom, 0.0f, 1.0f, 1.0f, 1.0f}
    };

    IDirect3DStateBlock9* stateBlock = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &stateBlock)) ||
        !stateBlock) {
        return;
    }
    (void)stateBlock->Capture();

    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);

    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0Fu);

    device->SetTexture(0, g_heartOverlayTexture);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);

    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

    (void)device->DrawPrimitiveUP(
        D3DPT_TRIANGLESTRIP,
        2,
        vertices,
        sizeof(HeartOverlayVertex));

    (void)stateBlock->Apply();
    stateBlock->Release();
}

HRESULT __fastcall HookedHeartEndScene(
    IDirect3DDevice9* device) noexcept {
    const EndSceneFn original = g_origHeartEndScene;
    if (!original) {
        return D3DERR_INVALIDCALL;
    }

    if (!g_heartEndSceneHook.IsDetaching() &&
        g_heartReady.load(std::memory_order_acquire)) {
        const ULONGLONG expire =
            g_heart.expireAt.load(std::memory_order_acquire);

        if (expire != 0) {
            const ULONGLONG now = ::GetTickCount64();
            if (now >= expire) {
                ULONGLONG expected = expire;
                (void)g_heart.expireAt.compare_exchange_strong(
                    expected,
                    0,
                    std::memory_order_acq_rel);
            }
            else {
                const int x = g_heart.x.load(std::memory_order_relaxed);
                const int y = g_heart.y.load(std::memory_order_relaxed);
                if (IsBoardCoordinate(x, y)) {
                    RenderHeartOverlay(device, x, y);
                }
            }
        }
    }

    return original(device);
}

static bool InstallHeartEndSceneHook(IDirect3DDevice9* device) noexcept {
    if (!device || g_heartEndSceneHook.IsInstalled()) {
        return device != nullptr;
    }

    void** const vtable = *reinterpret_cast<void***>(device);
    if (!vtable || !vtable[42]) {
        return false;
    }

    try {
        g_heartEndSceneHook.PrepareInstall();
        auto result = safetyhook_manager::global().install_inline(
            kHeartEndSceneHookName,
            vtable[42],
            &HookedHeartEndScene,
            false);
        if (!result) {
            return false;
        }

        g_origHeartEndScene =
            GetOriginalHook<EndSceneFn>(kHeartEndSceneHookName);
        if (!g_origHeartEndScene ||
            !safetyhook_manager::global().enable(
                kHeartEndSceneHookName)) {
            (void)safetyhook_manager::global().uninstall(
                kHeartEndSceneHookName);
            g_origHeartEndScene = nullptr;
            return false;
        }

        g_heartEndSceneHook.MarkInstalled();
        return true;
    }
    catch (...) {
        g_origHeartEndScene = nullptr;
        return false;
    }
}

HRESULT __fastcall HookedHeartOnCreateDevice(
    DecalMgr* self,
    IDirect3DDevice9* device) noexcept {
    const OnCreateDeviceFn original = g_origHeartOnCreateDevice;
    if (!original) {
        return E_FAIL;
    }

    const HRESULT hr = original(self, device);
    if (FAILED(hr) || !device) {
        g_heartReady.store(false, std::memory_order_release);
        return hr;
    }

    if (g_heartDevice && g_heartDevice != device && g_heartOverlayTexture) {
        g_heartOverlayTexture->Release();
        g_heartOverlayTexture = nullptr;
    }
    g_heartDevice = device;

    const bool endSceneReady = InstallHeartEndSceneHook(device);
    if (endSceneReady) {
        (void)EnsureHeartOverlayTexture(device);
    }

    g_heartReady.store(endSceneReady, std::memory_order_release);
    return hr;
}

template <typename Target, typename Destination>
bool InstallInlineHook(
    HookState& state,
    std::string_view name,
    Target target,
    Destination destination) {
    state.PrepareInstall();

    auto result = safetyhook_manager::global().install_inline(
        std::string{name},
        target,
        destination,
        true);
    if (!result) {
        return false;
    }

    state.MarkInstalled();
    return true;
}

template <typename Target, typename Destination>
bool InstallMidHook(
    HookState& state,
    std::string_view name,
    Target target,
    Destination destination) {
    state.PrepareInstall();

    auto result = safetyhook_manager::global().install_mid(
        std::string{name},
        target,
        destination,
        true);
    if (!result) {
        return false;
    }

    state.MarkInstalled();
    return true;
}

void UninstallManagedHook(
    HookState& state,
    std::string_view name) noexcept {
    if (!state.BeginUninstall()) {
        return;
    }

    try {
        (void)safetyhook_manager::global().uninstall(name);
    } catch (...) {
    }
}


bool InstallHeartHooks() noexcept {
    if (g_heartOnCreateDeviceHook.IsInstalled()) {
        return true;
    }
    if (!InitHeartAddresses()) {
        return false;
    }

    try {
        g_heartOnCreateDeviceHook.PrepareInstall();
        auto result = safetyhook_manager::global().install_inline(
            kHeartOnCreateDeviceHookName,
            GameFunction<OnCreateDeviceFn>(kOnCreateDeviceRva),
            &HookedHeartOnCreateDevice,
            false);
        if (!result) {
            return false;
        }

        g_origHeartOnCreateDevice =
            GetOriginalHook<OnCreateDeviceFn>(kHeartOnCreateDeviceHookName);
        if (!g_origHeartOnCreateDevice ||
            !safetyhook_manager::global().enable(
                kHeartOnCreateDeviceHookName)) {
            (void)safetyhook_manager::global().uninstall(
                kHeartOnCreateDeviceHookName);
            g_origHeartOnCreateDevice = nullptr;
            return false;
        }

        g_heartOnCreateDeviceHook.MarkInstalled();
        return true;
    } catch (...) {
        if (g_heartOnCreateDeviceHook.IsInstalled()) {
            UninstallManagedHook(
                g_heartOnCreateDeviceHook,
                kHeartOnCreateDeviceHookName);
        }
        g_origHeartOnCreateDevice = nullptr;
        return false;
    }
}

void UninstallHeartHooks() noexcept {
    UninstallManagedHook(
        g_heartEndSceneHook,
        kHeartEndSceneHookName);
    UninstallManagedHook(
        g_heartOnCreateDeviceHook,
        kHeartOnCreateDeviceHookName);

    g_origHeartEndScene = nullptr;
    g_origHeartOnCreateDevice = nullptr;
    g_heartDevice = nullptr;

    if (g_heartOverlayTexture) {
        g_heartOverlayTexture->Release();
        g_heartOverlayTexture = nullptr;
    }

    g_heartReady.store(false, std::memory_order_release);
    g_heart.expireAt.store(0, std::memory_order_release);
}

Game* GetCurrentGame() {
    if (!g_gameModule) {
        return nullptr;
    }

    const std::uintptr_t moduleBase = AddressOf(g_gameModule);
    return MemoryEditor::ReadValue<Game*>(
        moduleBase + kGameRootRva);
}

struct EmbeddedWav {
    unsigned char* data = nullptr;
    DWORD size = 0;
};

bool GetEmbeddedSuccubusWav(EmbeddedWav& out) noexcept {
    out = {};
    if (!g_OurDll) {
        return false;
    }

    const HRSRC resource = ::FindResourceW(
        g_OurDll,
        MAKEINTRESOURCEW(kSuccubusWavResourceId),
        RT_RCDATA);
    if (!resource) {
        return false;
    }

    const HGLOBAL loaded = ::LoadResource(g_OurDll, resource);
    if (!loaded) {
        return false;
    }

    const DWORD size = ::SizeofResource(g_OurDll, resource);
    if (!size) {
        return false;
    }

    void* const data = ::LockResource(loaded);
    if (!data) {
        return false;
    }

    out.data = static_cast<unsigned char*>(data);
    out.size = size;
    return true;
}

CSoundManager* GetSoundManager(Game* game) noexcept {
    if (!game) {
        return nullptr;
    }

    return reinterpret_cast<CSoundManager*>(
        AddressOf(game) + kGameSoundManagerOffset);
}

bool IsGameSoundReady(Game* game) noexcept {
    CSoundManager* const manager = GetSoundManager(game);
    if (!manager) {
        return false;
    }

    return *reinterpret_cast<void**>(manager) != nullptr;
}

CSound* CreateSuccubusSound(Game* game) noexcept {
    if (!game || !IsGameSoundReady(game)) {
        return nullptr;
    }

    const auto createFromMemory2 =
        GameFunction<CreateFromMemory2Fn>(kCreateFromMemory2Rva);
    CSoundManager* const manager = GetSoundManager(game);
    if (!createFromMemory2 || !manager) {
        return nullptr;
    }

    EmbeddedWav wav{};
    if (!GetEmbeddedSuccubusWav(wav) || !wav.data || !wav.size) {
        return nullptr;
    }

    CSound* sound = nullptr;
    GUID guid{};

    try {
        const HRESULT hr = createFromMemory2(
            manager,
            &sound,
            wav.data,
            wav.size,
            &guid);
        if (FAILED(hr)) {
            return nullptr;
        }
    } catch (...) {
        return nullptr;
    }

    return sound;
}

void PlaySuccubusSound(Game* game) noexcept {
    if (!game) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_succubusSoundMutex);

    if (!g_succubusSound) {
        g_succubusSound = CreateSuccubusSound(game);
    }
    if (!g_succubusSound) {
        return;
    }

    const auto play = GameFunction<CSoundPlayFn>(kCSoundPlayRva);
    if (!play) {
        return;
    }

    try {
        (void)play(
            g_succubusSound, 0, 0, 0, -1, 0);
    } catch (...) {
    }
}

void ShowCustomTip(
    Game* game,
    const wchar_t* titleText,
    const wchar_t* bodyText,
    float duration = 5.0f) noexcept {
    if (!game || !titleText || !bodyText || !g_gameModule) {
        return;
    }

    const auto strCtor =
        GameFunction<GameStrCtorFn>(kGameStrCtorRva);
    const auto showTip =
        GameFunction<ShowTipFn>(kShowTipRva);
    const auto operatorDelete =
        GameFunction<OperatorDeleteFn>(kOperatorDeleteRva);
    if (!strCtor || !showTip || !operatorDelete) {
        return;
    }

    GameStr title{};
    GameStr body{};

    try {
        strCtor(&title, titleText);
        strCtor(&body, bodyText);

        showTip(
            game,
            1,
            &title,
            &body,
            duration);
    } catch (...) {
    }

    if (title.data) {
        operatorDelete(title.data);
    }
    if (body.data) {
        operatorDelete(body.data);
    }
}

AI::Board* GetAIBoard(Game* game) noexcept {
    if (!game) {
        return nullptr;
    }

    return reinterpret_cast<AI::Board*>(
        AddressOf(game) + kGameAIBoardOffset);
}

AnimationEventArray* GetAnimationEvents(Game* game) noexcept {
    if (!game) {
        return nullptr;
    }

    return reinterpret_cast<AnimationEventArray*>(
        AddressOf(game) + kGameAnimationEventsOffset);
}

Game* GetGameFromRules(GameRules* rules) noexcept {
    if (!rules) {
        return nullptr;
    }

    return reinterpret_cast<Game*>(
        AddressOf(rules) - kGameRulesOffset);
}

HumanMoveState* GetHumanMoveState(Game* game) noexcept {
    if (!game) {
        return nullptr;
    }

    return MemoryEditor::ReadValue<HumanMoveState*>(
        AddressOf(game) + kGameHumanMoveStateOffset);
}

Piece* GetBoardPiece(Board* board, int x, int y) {
    if (!board || !IsBoardCoordinate(x, y)) {
        return nullptr;
    }

    const auto index = static_cast<std::size_t>(x + y * kBoardWidth);
    return MemoryEditor::ReadValue<Piece*>(
        AddressOf(board) + index * sizeof(Piece*));
}

Board* GetCurrentBoard(Game* game) {
    if (!game) {
        return nullptr;
    }

    Board** const boardArray = MemoryEditor::ReadValue<Board**>(
        AddressOf(game) + kGameBoardArrayOffset);
    const std::int32_t boardCount = MemoryEditor::ReadValue<std::int32_t>(
        AddressOf(game) + kGameBoardCountOffset);
    if (!boardArray || boardCount <= 0) {
        return nullptr;
    }

    return MemoryEditor::ReadValue<Board*>(
        AddressOf(boardArray) +
        static_cast<std::size_t>(boardCount - 1) * sizeof(Board*));
}

Board* GetCurrentBoard() {
    return GetCurrentBoard(GetCurrentGame());
}

Piece* GetBoardPiece(int x, int y) {
    return GetBoardPiece(GetCurrentBoard(), x, y);
}

int GetPieceType(Piece* piece) {
    if (!piece) {
        return -1;
    }

    const std::uintptr_t vtable =
        MemoryEditor::ReadValue<std::uintptr_t>(AddressOf(piece));
    if (!vtable) {
        return -1;
    }

    const std::uintptr_t functionAddress =
        MemoryEditor::ReadValue<std::uintptr_t>(
            vtable + kPieceTypeVtableOffset);
    if (!functionAddress) {
        return -1;
    }

    const auto function =
        reinterpret_cast<GetPieceTypeFn>(functionAddress);
    return function(piece);
}

std::uintptr_t GetPieceVtableRva(int pieceType) noexcept {
    switch (pieceType) {
    case kPawnPieceFlag:
        return kPawnVtableRva;
    case kKnightPieceFlag:
        return kKnightVtableRva;
    case kBishopPieceFlag:
        return kBishopVtableRva;
    case kRookPieceFlag:
        return kRookVtableRva;
    case kQueenPieceFlag:
        return kQueenVtableRva;
    default:
        return 0;
    }
}

Piece* ClonePieceWithColor(
    Piece* source,
    int pieceType,
    std::int32_t color,
    OperatorNewFn operatorNew) {
    if (!source || !operatorNew || !g_gameModule) {
        return nullptr;
    }

    const std::uintptr_t vtableRva = GetPieceVtableRva(pieceType);
    if (!vtableRva) {
        return nullptr;
    }

    const std::uint8_t moved =
        MemoryEditor::ReadValue<std::uint8_t>(
            AddressOf(source) + offsetof(Piece, moved));
    const double removedTime = MemoryEditor::ReadValue<double>(
        AddressOf(source) + offsetof(Piece, removedTime));

    auto* const replacement = static_cast<Piece*>(
        operatorNew(sizeof(Piece)));
    if (!replacement) {
        return nullptr;
    }

    replacement->vtable = reinterpret_cast<void*>(
        AddressOf(g_gameModule) + vtableRva);
    replacement->color = color;
    replacement->moved = moved;
    replacement->padding[0] = 0;
    replacement->padding[1] = 0;
    replacement->padding[2] = 0;
    replacement->removedTime = removedTime;
    return replacement;
}

bool TryEvaluateAIBoardPostMove(
    AI::Board* aiBoard,
    Move* request,
    const AIBoardPostMoveFunctions& functions,
    std::int32_t& resultType) {
    if (!aiBoard ||
        !request ||
        !functions.queryPostMoveState ||
        !functions.checkDrawCondition ||
        !functions.checkRepetition ||
        !functions.isSquareAttacked) {
        return false;
    }

    const std::uintptr_t aiBoardAddress = AddressOf(aiBoard);
    if (!MemoryEditor::WriteValue<std::uint8_t>(
            aiBoardAddress + kAIBoardStateFlagOffset,
            0)) {
        return false;
    }

    EngineMove fallback{};
    EngineMove* const result = functions.queryPostMoveState(
        aiBoard,
        request,
        0,
        &fallback);
    if (!result) {
        return false;
    }

    const std::int32_t postMoveType =
        MemoryEditor::ReadValue<std::int32_t>(
            AddressOf(result) + offsetof(EngineMove, type));

    if (postMoveType >= 0 && postMoveType <= 5) {
        const std::int32_t side =
            MemoryEditor::ReadValue<std::int32_t>(
                aiBoardAddress + kAIBoardPostMoveSideOffset);

        const std::uintptr_t positionOffset = side == 0
            ? kAIBoardSideZeroPositionOffset
            : kAIBoardSideOnePositionOffset;
        const std::int32_t position =
            MemoryEditor::ReadValue<std::int32_t>(
                aiBoardAddress + positionOffset);

        const bool attacked = functions.isSquareAttacked(
            aiBoard,
            position,
            side,
            side == 0 ? 1u : 0u);

        resultType = attacked ? 1 : 2;
        return true;
    }

    if (!functions.checkDrawCondition(aiBoard)) {
        resultType = 5;
        return true;
    }

    if (MemoryEditor::ReadValue<std::int32_t>(
            aiBoardAddress + kAIBoardDrawCounterOffset) == 100) {
        resultType = 3;
        return true;
    }

    if (functions.checkRepetition(aiBoard, -1) == 2) {
        resultType = 4;
        return true;
    }

    resultType = 0;
    return true;
}

bool FlipPieceColorAndSide(
    AI::Board* board,
    int square,
    AIHashFlipPieceFn hashFlipPiece) noexcept {
    if (!board ||
        !hashFlipPiece ||
        static_cast<unsigned int>(square) > 0x7F) {
        return false;
    }

    try {
        auto* const boardBase =
            reinterpret_cast<std::uint8_t*>(board);

        AI::Hash* const hash =
            *reinterpret_cast<AI::Hash**>(
                boardBase + kAIBoardHashOffset);
        if (!hash) {
            return false;
        }

        int* const pieceTypes = reinterpret_cast<int*>(
            boardBase + kAIBoardPieceTypesOffset);
        int* const pieceColors = reinterpret_cast<int*>(
            boardBase + kAIBoardPieceColorsOffset);

        const int piece = pieceTypes[square];
        const int oldColor = pieceColors[square];

        hashFlipPiece(
            hash,
            static_cast<unsigned int>(oldColor),
            piece,
            static_cast<unsigned int>(square));

        pieceColors[square] = 0;

        hashFlipPiece(
            hash,
            0,
            piece,
            static_cast<unsigned int>(square));

        int& sideToMove = *reinterpret_cast<int*>(
            boardBase + kAIBoardPostMoveSideOffset);
        int& oppositeSide = *reinterpret_cast<int*>(
            boardBase + kAIBoardTemporarySideOffset);

        sideToMove = (sideToMove == 0);
        oppositeSide = (oppositeSide == 0);

        auto* const hashBase =
            reinterpret_cast<std::uint8_t*>(hash);
        const std::uint64_t sideToMoveKey =
            *reinterpret_cast<std::uint64_t*>(
                hashBase + kAIHashSideToMoveKeyOffset);
        std::uint64_t& currentHash =
            *reinterpret_cast<std::uint64_t*>(
                hashBase + kAIHashCurrentHashOffset);

        currentHash ^= sideToMoveKey;
        return true;
    } catch (...) {
        return false;
    }
}

void ExecuteMovePart(
    Game* game,
    Board* boardCopy,
    SetSizeInternalFn setSizeInternal) noexcept {
    if (!game || !boardCopy || !setSizeInternal) {
        return;
    }

    try {
        auto* const gameBase =
            reinterpret_cast<std::uint8_t*>(game);
        auto* const boardHistory =
            gameBase + kGameBoardArrayOffset;

        auto& boardCount =
            *reinterpret_cast<std::int32_t*>(
                boardHistory +
                (kGameBoardCountOffset - kGameBoardArrayOffset));

        const auto resizeResult = static_cast<std::int32_t>(
            setSizeInternal(
                boardHistory,
                boardCount + 1));

        if (resizeResult >= 0) {
            auto* const boardData =
                *reinterpret_cast<void***>(boardHistory);
            if (boardData) {
                boardData[boardCount++] = boardCopy;
            }
        }

        auto& currentSide =
            *reinterpret_cast<std::int32_t*>(
                gameBase + kGameTurnOffset);
        if (currentSide == 0 || currentSide == 1) {
            currentSide ^= 1;
        }
    } catch (...) {
    }
}

bool PromotePawnToQueen(
    int x,
    int y,
    bool aiBoardAlreadyQueen = false) {
    if (!IsBoardCoordinate(x, y) || !g_gameModule) {
        return false;
    }

    const auto operatorNew = GameFunction<OperatorNewFn>(kOperatorNewRva);
    const auto operatorDelete = GameFunction<OperatorDeleteFn>(kOperatorDeleteRva);
    const auto swapPiece = GameFunction<AIBoardSwapPieceFn>(kAIBoardSwapPieceRva);
    const auto addAnimationEvent = GameFunction<AnimationEventAddFn>(kAnimationEventAddRva);
    if (!operatorNew || !operatorDelete || !addAnimationEvent ||
        (!aiBoardAlreadyQueen && !swapPiece)) {
        return false;
    }

    Game* const game = GetCurrentGame();
    if (!game) {
        return false;
    }

    AI::Board* const aiBoard = GetAIBoard(game);
    AnimationEventArray* const animationEvents =
        GetAnimationEvents(game);
    Board* const board = GetCurrentBoard(game);
    if (!aiBoard || !animationEvents || !board) {
        return false;
    }

    Piece* const pawn = GetBoardPiece(board, x, y);
    if (!pawn || GetPieceType(pawn) != kPawnPieceType) {
        return false;
    }

    const std::int32_t color =
        MemoryEditor::ReadValue<std::int32_t>(
            AddressOf(pawn) + offsetof(Piece, color));

    auto* const queen = static_cast<Piece*>(
        operatorNew(sizeof(Piece)));
    if (!queen) {
        return false;
    }

    queen->vtable = reinterpret_cast<void*>(
        AddressOf(g_gameModule) + kQueenVtableRva);
    queen->color = color;
    queen->moved = 1;
    queen->padding[0] = 0;
    queen->padding[1] = 0;
    queen->padding[2] = 0;
    queen->removedTime = 0.0;

    const std::size_t index =
        static_cast<std::size_t>(x + y * kBoardWidth);
    const std::uintptr_t squareAddress =
        AddressOf(board) + index * sizeof(Piece*);

    if (!MemoryEditor::WriteValue<Piece*>(squareAddress, queen)) {
        operatorDelete(queen);
        return false;
    }

    AnimationEvent event{};
    event.type = 1;
    event.piece = queen;
    event.fromX = x;
    event.fromY = y;
    event.toX = x;
    event.toY = y;
    event.state = 0.0f;

    const HRESULT addResult = addAnimationEvent(
        animationEvents,
        &event);
    if (FAILED(addResult)) {
        (void)MemoryEditor::WriteValue<Piece*>(
            squareAddress,
            pawn);
        operatorDelete(queen);
        return false;
    }

    operatorDelete(pawn);

    if (!aiBoardAlreadyQueen) {
        swapPiece(
            aiBoard,
            x + 16 * y,
            kQueenPieceType);
    }

    return true;
}

enum class PawnPromotionPrepareResult {
    Failed,
    Ready,
    OpponentKingAttacked,
};

PawnPromotionPrepareResult PreparePawnPromotionOnAIBoard(
    int x,
    int y,
    std::int32_t playerSide) noexcept {
    if (!IsBoardCoordinate(x, y) ||
        (playerSide != 0 && playerSide != 1) ||
        !g_gameModule) {
        return PawnPromotionPrepareResult::Failed;
    }

    const auto swapPiece =
        GameFunction<AIBoardSwapPieceFn>(kAIBoardSwapPieceRva);
    const auto isSquareAttacked =
        GameFunction<AIBoardIsSquareAttackedFn>(
            kAIBoardIsSquareAttackedRva);
    if (!swapPiece || !isSquareAttacked) {
        return PawnPromotionPrepareResult::Failed;
    }

    Game* const game = GetCurrentGame();
    AI::Board* const aiBoard = GetAIBoard(game);
    if (!game || !aiBoard) {
        return PawnPromotionPrepareResult::Failed;
    }

    const std::int32_t opponentSide = playerSide ^ 1;
    const std::uintptr_t aiBoardAddress = AddressOf(aiBoard);
    const std::uintptr_t kingPositionOffset =
        opponentSide == 0
            ? kAIBoardSideZeroPositionOffset
            : kAIBoardSideOnePositionOffset;
    const std::int32_t kingPosition =
        MemoryEditor::ReadValue<std::int32_t>(
            aiBoardAddress + kingPositionOffset);
    const int square = x + 16 * y;

    bool swappedToQueen = false;
    try {
        swapPiece(aiBoard, square, kQueenPieceType);
        swappedToQueen = true;

        const bool opponentKingAttacked = isSquareAttacked(
            aiBoard,
            kingPosition,
            opponentSide,
            opponentSide == 0 ? 1u : 0u);

        if (opponentKingAttacked) {
            swapPiece(aiBoard, square, kPawnPieceType);
            swappedToQueen = false;
            return PawnPromotionPrepareResult::OpponentKingAttacked;
        }

        return PawnPromotionPrepareResult::Ready;
    } catch (...) {
        if (swappedToQueen) {
            try {
                swapPiece(aiBoard, square, kPawnPieceType);
            } catch (...) {
            }
        }
        return PawnPromotionPrepareResult::Failed;
    }
}

char CallOriginalHandleAction(
    HumanMoveState* moveState,
    const Board* board) noexcept {
    const HandleActionFn original =
        GetOriginalHook<HandleActionFn>(kPawnPromotionHookName);
    if (!original ||
        g_pawnPromotionHook.IsDetaching()) {
        return 0;
    }

    try {
        return original(moveState, board);
    } catch (...) {
        return 0;
    }
}

char __fastcall HookedHandleAction(
    HumanMoveState* moveState,
    const Board* board) noexcept {
    if (!moveState || !board ||
        g_pawnPromotionHook.IsDetaching()) {
        return CallOriginalHandleAction(moveState, board);
    }

    try {
        const std::uintptr_t moveStateAddress = AddressOf(moveState);
        const int x = MemoryEditor::ReadValue<std::int32_t>(
            moveStateAddress + kHumanMoveStateToXOffset);
        const int y = MemoryEditor::ReadValue<std::int32_t>(
            moveStateAddress + kHumanMoveStateToYOffset);

        if (IsBoardCoordinate(x, y)) {
            const std::size_t index =
                static_cast<std::size_t>(x + y * kBoardWidth);
            Piece* const piece = MemoryEditor::ReadValue<Piece*>(
                AddressOf(board) + index * sizeof(Piece*));

            if (piece) {
                const std::int32_t playerSide =
                    MemoryEditor::ReadValue<std::int32_t>(
                        moveStateAddress + kHumanMoveStateSideOffset);
                const std::int32_t pieceSide =
                    MemoryEditor::ReadValue<std::int32_t>(
                        AddressOf(piece) + offsetof(Piece, color));

                if (pieceSide == playerSide) {
                    const std::int32_t state =
                        MemoryEditor::ReadValue<std::int32_t>(
                            moveStateAddress + kHumanMoveStateStateOffset);
                    const bool firstSelection =
                        state == kHumanMoveStateFirstSelectionState;
                    const bool switchSelection =
                        state == kHumanMoveStatePieceSelectedState &&
                        (MemoryEditor::ReadValue<std::int32_t>(
                             moveStateAddress +
                             kHumanMoveStateFromXOffset) != x ||
                         MemoryEditor::ReadValue<std::int32_t>(
                             moveStateAddress +
                             kHumanMoveStateFromYOffset) != y);

                    if (firstSelection || switchSelection) {
                        if (GetPieceType(piece) == kPawnPieceType) {
                            const PawnPromotionPrepareResult prepareResult =
                                PreparePawnPromotionOnAIBoard(
                                    x,
                                    y,
                                    playerSide);

                            if (prepareResult ==
                                PawnPromotionPrepareResult::OpponentKingAttacked) {
                                ShowCustomTip(
                                    GetCurrentGame(),
                                    L"无法升变",
                                    L"升变会导致对方王受到攻击，因此无法升变。");
                            }
                            else if (prepareResult ==
                                PawnPromotionPrepareResult::Ready) {
                                bool promotionSucceeded = false;
                                try {
                                    promotionSucceeded =
                                        PromotePawnToQueen(x, y, true);
                                } catch (...) {
                                }

                                if (!promotionSucceeded) {
                                    const auto swapPiece =
                                        GameFunction<AIBoardSwapPieceFn>(
                                            kAIBoardSwapPieceRva);
                                    Game* const game = GetCurrentGame();
                                    AI::Board* const aiBoard = GetAIBoard(game);
                                    if (swapPiece && aiBoard) {
                                        try {
                                            swapPiece(
                                                aiBoard,
                                                x + 16 * y,
                                                kPawnPieceType);
                                        } catch (...) {
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    } catch (...) {
    }

    return CallOriginalHandleAction(moveState, board);
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

bool CallOriginalResolveCandidate(
    GameRules* rules,
    Board* position,
    Move* move) noexcept {
    const ResolveCandidateFn original =
        GetOriginalHook<ResolveCandidateFn>(kResolveCandidateHookName);
    if (!original ||
        g_freeMoveDetaching.load(std::memory_order_acquire) ||
        g_resolveCandidateHook.IsDetaching()) {
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
    Game* game,
    Board* position,
    Move* move) {
    if (!game || !position || !move) {
        return HumanTurnResult::UseOriginal;
    }

    if (move->type >= kNormalMoveType) {
        return HumanTurnResult::AcceptCandidate;
    }

    const auto applyMoveTemporarily =
        GameFunction<AIBoardApplyMoveTemporarilyFn>(
            kAIBoardApplyMoveTemporarilyRva);
    const auto undoTemporaryMove =
        GameFunction<AIBoardUndoTemporaryMoveFn>(
            kAIBoardUndoTemporaryMoveRva);
    const auto isSquareAttacked =
        GameFunction<AIBoardIsSquareAttackedFn>(
            kAIBoardIsSquareAttackedRva);
    if (!applyMoveTemporarily || !undoTemporaryMove || !isSquareAttacked) {
        return HumanTurnResult::UseOriginal;
    }

    HumanMoveState* const humanMoveState = GetHumanMoveState(game);
    if (!humanMoveState) {
        return HumanTurnResult::UseOriginal;
    }

    const std::uintptr_t moveStateAddress = AddressOf(humanMoveState);
    const std::int32_t selectedFromX =
        MemoryEditor::ReadValue<std::int32_t>(
            moveStateAddress + kHumanMoveStateFromXOffset);
    const std::int32_t selectedFromY =
        MemoryEditor::ReadValue<std::int32_t>(
            moveStateAddress + kHumanMoveStateFromYOffset);
    const std::int32_t selectedToX =
        MemoryEditor::ReadValue<std::int32_t>(
            moveStateAddress + kHumanMoveStateToXOffset);
    const std::int32_t selectedToY =
        MemoryEditor::ReadValue<std::int32_t>(
            moveStateAddress + kHumanMoveStateToYOffset);

	Piece* const targetPiece = GetBoardPiece(
		position,
		selectedToX,
		selectedToY);

	if (targetPiece && GetPieceType(targetPiece) == kKingPieceFlag) {
		move->type = 1;
		return HumanTurnResult::RejectCandidate;
	}

    EngineMove engineMove{};
    engineMove.fromCode =
        EncodePosition(selectedFromX, selectedFromY);
    engineMove.toCode =
        EncodePosition(selectedToX, selectedToY);
    engineMove.type = move->type;
    engineMove.auxiliary = 0;

    AI::Board* const aiBoard = GetAIBoard(game);
    if (!aiBoard) {
        return HumanTurnResult::UseOriginal;
    }

    applyMoveTemporarily(aiBoard, &engineMove);

    bool kingAttacked = false;
    try {
        const std::uintptr_t aiBoardAddress = AddressOf(aiBoard);

        const std::int32_t sideToCheck =
            MemoryEditor::ReadValue<std::int32_t>(
                aiBoardAddress + kAIBoardTemporarySideOffset);

        const std::uintptr_t kingPositionOffset =
            sideToCheck == 0
                ? kAIBoardSideZeroPositionOffset
                : kAIBoardSideOnePositionOffset;
        const std::int32_t kingPosition =
            MemoryEditor::ReadValue<std::int32_t>(
                aiBoardAddress + kingPositionOffset);

        kingAttacked = isSquareAttacked(
            aiBoard,
            kingPosition,
            sideToCheck,
            sideToCheck == 0 ? 1u : 0u);
    } catch (...) {
        undoTemporaryMove(aiBoard);
        throw;
    }

    undoTemporaryMove(aiBoard);

    if (kingAttacked) {
        move->type = kMoveExposesKingType;
        return HumanTurnResult::RejectCandidate;
    }

    move->fromX = selectedFromX;
    move->fromY = selectedFromY;
    move->toX = selectedToX;
    move->toY = selectedToY;

    Piece* const piece = GetBoardPiece(
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
    GameRules* rules,
    Board* position,
    Move* move) noexcept {
    if (!rules || !position || !move ||
        g_freeMoveDetaching.load(std::memory_order_acquire) ||
        g_resolveCandidateHook.IsDetaching()) {
        return CallOriginalResolveCandidate(rules, position, move);
    }

    try {
        Game* const game = GetGameFromRules(rules);
        if (!game) {
            return CallOriginalResolveCandidate(rules, position, move);
        }

        const int turn = MemoryEditor::ReadValue<std::int32_t>(
            AddressOf(game) + kGameTurnOffset);

        if (turn != 0) {
            return CallOriginalResolveCandidate(rules, position, move);
        }

        switch (HandleHumanTurn(game, position, move)) {
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

std::int32_t CallOriginalAIBoardNotifyMove(
    AI::Board* aiBoard,
    const Move* request) noexcept {
    const AIBoardNotifyMoveFn original =
        GetOriginalHook<AIBoardNotifyMoveFn>(kAIBoardNotifyMoveHookName);
    if (!original ||
        g_freeMoveDetaching.load(std::memory_order_acquire) ||
        g_aiBoardNotifyMoveHook.IsDetaching()) {
        return 0;
    }

    try {
        return original(aiBoard, request);
    } catch (...) {
        return 0;
    }
}

std::int32_t __fastcall HookedAIBoardNotifyMove(
    AI::Board* aiBoard,
    const Move* request) noexcept {
    if (!aiBoard || !request ||
        g_freeMoveDetaching.load(std::memory_order_acquire) ||
        g_aiBoardNotifyMoveHook.IsDetaching()) {
        return CallOriginalAIBoardNotifyMove(aiBoard, request);
    }

    const auto commitMove =
        GameFunction<AIBoardCommitMoveFn>(kAIBoardCommitMoveRva);
    const auto postMoveFunctions = GetAIBoardPostMoveFunctions();
    if (!commitMove ||
        !postMoveFunctions.queryPostMoveState ||
        !postMoveFunctions.checkDrawCondition ||
        !postMoveFunctions.checkRepetition ||
        !postMoveFunctions.isSquareAttacked) {
        return CallOriginalAIBoardNotifyMove(aiBoard, request);
    }

    bool moveCommitted = false;

    try {
        Move requestCopy = MemoryEditor::ReadValue<Move>(
            AddressOf(request));

        EngineMove engineMove{};
        engineMove.fromCode = EncodePosition(
            requestCopy.fromX,
            requestCopy.fromY);
        engineMove.toCode = EncodePosition(
            requestCopy.toX,
            requestCopy.toY);
        engineMove.type = requestCopy.type;
        engineMove.auxiliary = 0;

        commitMove(aiBoard, &engineMove);
        moveCommitted = true;

        std::int32_t resultType = 0;
        if (!TryEvaluateAIBoardPostMove(
                aiBoard,
                &requestCopy,
                postMoveFunctions,
                resultType)) {
            return 0;
        }

        return resultType;
    } catch (...) {
        if (!moveCommitted) {
            return CallOriginalAIBoardNotifyMove(aiBoard, request);
        }
        return 0;
    }
}

void FinishSuccubusQueenMove(
    SafetyHookContext& context,
    std::int32_t resultType) noexcept {
    context.rax = resultType;
    context.r13 = 1;
    context.rip =
        g_succubusQueenHookTarget +
        kSuccubusQueenSkipBytes;
}

void HookedSuccubusQueen(SafetyHookContext& context) noexcept {
    if (g_succubusQueenHook.IsDetaching() ||
        !g_succubusQueenHookTarget) {
        return;
    }

    const auto operatorNew = GameFunction<OperatorNewFn>(kOperatorNewRva);
    const auto operatorDelete = GameFunction<OperatorDeleteFn>(kOperatorDeleteRva);
    const auto hashFlipPiece = GameFunction<AIHashFlipPieceFn>(kAIHashFlipPieceRva);
    const auto setSizeInternal = GameFunction<SetSizeInternalFn>(kSetSizeInternalRva);
    const auto postMoveFunctions = GetAIBoardPostMoveFunctions();
    if (!operatorNew || !operatorDelete || !hashFlipPiece ||
        !setSizeInternal ||
        !postMoveFunctions.queryPostMoveState ||
        !postMoveFunctions.checkDrawCondition ||
        !postMoveFunctions.checkRepetition ||
        !postMoveFunctions.isSquareAttacked) {
        return;
    }

    bool customMoveApplied = false;

    try {
        Game* const game = GetCurrentGame();
        AI::Board* const aiBoard = GetAIBoard(game);
        if (!game || !aiBoard) {
            return;
        }
		const int turn = MemoryEditor::ReadValue<std::int32_t>(
			AddressOf(game) + kGameTurnOffset);
		
		if (turn != 0) {
			return;
		}
		
        auto* const boardCopy =
            reinterpret_cast<Board*>(context.rcx);
        auto* const request =
            reinterpret_cast<Move*>(context.rdx);
        if (!boardCopy || !request) {
            return;
        }

        Move requestCopy = MemoryEditor::ReadValue<Move>(
            AddressOf(request));
        if (!IsBoardCoordinate(requestCopy.fromX, requestCopy.fromY) ||
            !IsBoardCoordinate(requestCopy.toX, requestCopy.toY)) {
            return;
        }

        Piece* const movingPiece = GetBoardPiece(
            boardCopy,
            requestCopy.fromX,
            requestCopy.fromY);
        if (!movingPiece ||
            GetPieceType(movingPiece) != kQueenPieceFlag) {
            return;
        }

        Piece* const capturedPiece = GetBoardPiece(
            boardCopy,
            requestCopy.toX,
            requestCopy.toY);
        if (!capturedPiece) {
            return;
        }

        const int capturedPieceType = GetPieceType(capturedPiece);
        if (capturedPieceType == kKingPieceFlag) {
            return;
        }
        if (!GetPieceVtableRva(capturedPieceType)) {
            return;
        }

        Piece* const convertedPiece = ClonePieceWithColor(
            capturedPiece,
            capturedPieceType,
            0,
            operatorNew);
        if (!convertedPiece) {
            return;
        }

        const std::size_t destinationIndex =
            static_cast<std::size_t>(
                requestCopy.toX +
                requestCopy.toY * kBoardWidth);
        const std::uintptr_t destinationAddress =
            AddressOf(boardCopy) +
            destinationIndex * sizeof(Piece*);

        if (!MemoryEditor::WriteValue<Piece*>(
                destinationAddress,
                convertedPiece)) {
            operatorDelete(convertedPiece);
            return;
        }

        const int square = EncodePosition(
            requestCopy.toX,
            requestCopy.toY);
        if (!FlipPieceColorAndSide(aiBoard, square, hashFlipPiece)) {
            const bool restored = MemoryEditor::WriteValue<Piece*>(
                destinationAddress,
                capturedPiece);
            if (restored) {
                operatorDelete(convertedPiece);
            }
            return;
        }

        customMoveApplied = true;
        ShowHeart(requestCopy.toX, requestCopy.toY);
        operatorDelete(capturedPiece);

        ExecuteMovePart(game, boardCopy, setSizeInternal);

        std::int32_t resultType = 0;
        (void)TryEvaluateAIBoardPostMove(
            aiBoard,
            &requestCopy,
            postMoveFunctions,
            resultType);

		request->type = 13;

        PlaySuccubusSound(game);

        FinishSuccubusQueenMove(context, resultType);
    } catch (...) {
        if (customMoveApplied && g_succubusQueenHookTarget) {
            FinishSuccubusQueenMove(context, 0);
        }
    }
}

bool InstallSuccubusQueenHook() noexcept {
    if (g_succubusQueenHook.IsInstalled()) {
        return true;
    }
    if (!EnsureGameModule()) {
        return false;
    }

    try {
        const MemoryEditor::PatternScan scan{
            MemoryEditor::Pattern{
                "4C 8B E7 48 8B D6 49 8B CC E8 ?? ?? ?? ??"},
            MemoryEditor::Offset(9)};

        void* const target = scan.Scan<void*>(g_gameModule);
        if (!target) {
            return false;
        }

        g_succubusQueenHookTarget = AddressOf(target);
        if (!InstallMidHook(
                g_succubusQueenHook,
                kSuccubusQueenHookName,
                target,
                &HookedSuccubusQueen)) {
            g_succubusQueenHookTarget = 0;
            return false;
        }

        return true;
    } catch (...) {
        g_succubusQueenHookTarget = 0;
        return false;
    }
}

void UninstallSuccubusQueenHook() noexcept {
    UninstallManagedHook(
        g_succubusQueenHook,
        kSuccubusQueenHookName);
    g_succubusQueenHookTarget = 0;
}

bool InstallPawnPromotionHook() noexcept {
    if (g_pawnPromotionHook.IsInstalled()) {
        return true;
    }
    if (!EnsureGameModule()) {
        return false;
    }

    try {
        return InstallInlineHook(
            g_pawnPromotionHook,
            kPawnPromotionHookName,
            GameFunction<HandleActionFn>(kHandleActionRva),
            &HookedHandleAction);
    } catch (...) {
        return false;
    }
}

void UninstallPawnPromotionHook() noexcept {
    UninstallManagedHook(
        g_pawnPromotionHook,
        kPawnPromotionHookName);
}

bool InstallResolveCandidateHookInternal() noexcept {
    if (g_resolveCandidateHook.IsInstalled()) {
        return true;
    }
    if (!EnsureGameModule()) {
        return false;
    }

    return InstallInlineHook(
        g_resolveCandidateHook,
        kResolveCandidateHookName,
        GameFunction<ResolveCandidateFn>(kResolveCandidateRva),
        &HookedResolveCandidate);
}

bool InstallAIBoardNotifyMoveHookInternal() noexcept {
    if (g_aiBoardNotifyMoveHook.IsInstalled()) {
        return true;
    }
    if (!EnsureGameModule()) {
        return false;
    }

    return InstallInlineHook(
        g_aiBoardNotifyMoveHook,
        kAIBoardNotifyMoveHookName,
        GameFunction<AIBoardNotifyMoveFn>(kAIBoardNotifyMoveRva),
        &HookedAIBoardNotifyMove);
}

void UninstallResolveCandidateHookInternal() noexcept {
    UninstallManagedHook(
        g_resolveCandidateHook,
        kResolveCandidateHookName);
}

void UninstallAIBoardNotifyMoveHookInternal() noexcept {
    UninstallManagedHook(
        g_aiBoardNotifyMoveHook,
        kAIBoardNotifyMoveHookName);
}

bool InstallHooks() noexcept {
    g_freeMoveDetaching.store(false, std::memory_order_release);

    const bool resolveAlreadyInstalled =
        g_resolveCandidateHook.IsInstalled();

    if (!InstallResolveCandidateHookInternal()) {
        return false;
    }

    if (!InstallAIBoardNotifyMoveHookInternal()) {
        if (!resolveAlreadyInstalled) {
            UninstallResolveCandidateHookInternal();
        }
        return false;
    }

    return true;
}

void UninstallHooks() noexcept {
    g_freeMoveDetaching.store(true, std::memory_order_release);
    UninstallAIBoardNotifyMoveHookInternal();
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

        if (command == "open pawn promotion") {
            return InstallPawnPromotionHook()
                ? "OK\n"
                : "ERR install_failed\n";
        }

        if (command == "close pawn promotion") {
            UninstallPawnPromotionHook();
            return "OK\n";
        }

        if (command == "open succubus queen") {
            return InstallSuccubusQueenHook()
                ? "OK\n"
                : "ERR install_failed\n";
        }

        if (command == "close succubus queen") {
            UninstallSuccubusQueenHook();
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


DWORD WINAPI InitializationThreadProc(void*) noexcept {
    (void)InstallHeartHooks();
    return SocketThreadProc(nullptr);
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
        g_OurDll = module;
        g_gameModule = ::GetModuleHandleW(nullptr);
        g_stopSocketThread.store(false, std::memory_order_release);
        ::DisableThreadLibraryCalls(module);

        const HANDLE thread = ::CreateThread(
            nullptr,
            0,
            &InitializationThreadProc,
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
            UninstallHeartHooks();
            UninstallSuccubusQueenHook();
            UninstallPawnPromotionHook();
            UninstallHooks();
        }
        break;

    default:
        break;
    }

    return TRUE;
}
