#pragma once
#include <stdint.h>

/* Game用C ABI。公開する値は固定幅の数値とコピー可能なPODだけに限定する。
 * 呼出しはWorldを開始したthreadだけに限定する。ポインタ引数は呼出し中のみ借用し、
 * Engine / Gameの双方で保存・解放しない。構造体はWindows x64 / pack 8を契約とする。 */
#if defined(_WIN32)
#define NEXUS_GAME_CALL __cdecl
#else
#define NEXUS_GAME_CALL
#endif
#define NEXUS_GAME_ABI_VERSION 1u

typedef int32_t NexusGameStatus;
#define NEXUS_GAME_OK ((NexusGameStatus)0)
#define NEXUS_GAME_INVALID_ARGUMENT ((NexusGameStatus)1)
#define NEXUS_GAME_INVALID_HANDLE ((NexusGameStatus)2)
#define NEXUS_GAME_WRONG_THREAD ((NexusGameStatus)3)
#define NEXUS_GAME_BUFFER_TOO_SMALL ((NexusGameStatus)4)
#define NEXUS_GAME_FAILED ((NexusGameStatus)5)
#define NEXUS_GAME_INCOMPATIBLE_ABI ((NexusGameStatus)6)

#pragma pack(push, 8)
/* UTF-8の借用範囲。終端NULは不要。lengthはbyte数であり文字数ではない。 */
typedef struct NexusGameString {
    const char* data;
    uint32_t length;
} NexusGameString;

/* Worldの寿命tokenと既存EntityIdのindex / generation。Nativeポインタではない。
 * World停止後は全Handleが無効になり、別Worldで同じslotが再利用されても復活しない。 */
typedef struct NexusEntityHandle {
    uint64_t world;
    uint32_t index;
    uint32_t generation;
} NexusEntityHandle;

/* Local Transformの一括転送値。派生Matrixは既存TransformSystemが計算する。 */
typedef struct NexusLocalTransform {
    float translation[3];
    float rotation[4];
    float scale[3];
} NexusLocalTransform;

/*-----------------------------------------------------------------------------------------
 * NexusGameApiV1
 * - 同じEngine Worldを複数のゲーム実装言語から操作する公開関数表
 * - ECS Storage、Renderer、Editor、allocatorのアドレスを公開しない
 *---------------------------------------------------------------------------------------*/
typedef struct NexusGameApiV1 {
    uint32_t abiVersion; //< 関数表の互換性番号
    uint32_t structSize; //< 誤ったレイアウトを呼出前に拒否するためのbyte数
    /** \brief Entityを生成する。nameは空なら既定名。既存Levelの永続IDも付与する */
    NexusGameStatus (NEXUS_GAME_CALL *createEntity)(uint64_t world, NexusGameString name, NexusEntityHandle* output);
    /** \brief 世代とWorldを検証してEntityを破棄する */
    NexusGameStatus (NEXUS_GAME_CALL *destroyEntity)(NexusEntityHandle entity);
    /** \brief 生存時はOK、破棄済み・旧WorldならINVALID_HANDLEを返す */
    NexusGameStatus (NEXUS_GAME_CALL *isEntityAlive)(NexusEntityHandle entity);
    /** \brief capacity=0 / output=nullptrで必要数を取得する。buffer不足時は部分コピーしない */
    NexusGameStatus (NEXUS_GAME_CALL *entities)(uint64_t world, NexusEntityHandle* output, uint32_t capacity, uint32_t* count);
    /** \brief Componentのポインタではなく、現在のLocal Transformをコピーする */
    NexusGameStatus (NEXUS_GAME_CALL *getLocalTransform)(NexusEntityHandle entity, NexusLocalTransform* output);
    /** \brief 有限値検証とQuaternion正規化後、一回でLocal値を更新してInterop往復を抑える */
    NexusGameStatus (NEXUS_GAME_CALL *setLocalTransform)(NexusEntityHandle entity, const NexusLocalTransform* value);
    /** \brief 共通LoggerのGameカテゴリへ配送する。World停止後のcallbackは拒否する */
    NexusGameStatus (NEXUS_GAME_CALL *log)(uint64_t world, NexusGameString message);
} NexusGameApiV1;
#pragma pack(pop)

#if defined(__cplusplus) && defined(_WIN64)
static_assert(sizeof(NexusGameString) == 16);
static_assert(sizeof(NexusEntityHandle) == 16);
static_assert(sizeof(NexusLocalTransform) == 40);
static_assert(sizeof(NexusGameApiV1) == 64);
#endif
