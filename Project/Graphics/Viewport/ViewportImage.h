#pragma once
#include <cstdint>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * ViewportImage
 * - UI backendが解釈する読み取り専用の画像参照
 * - Resourceやdescriptorの所有権を渡さず、次のResizeまで有効
 *---------------------------------------------------------------------------------------*/
struct ViewportImage {
    uintptr_t backendReference = 0; //< GraphicsとUI backend間の不透明参照
    uint32_t width = 0, height = 0; //< 実際に確保した描画解像度
};
}
