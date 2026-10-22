#pragma once
#include "Foundation/Error/Result.h"
#include "Graphics/Resource/MeshResource.h"
#include <filesystem>
#include <memory>
#include <span>

namespace NexusEngine {
class GraphicsContext;
class GraphicsResourceFactory;
class ConstantBuffer;
struct SelectionOutlineDraw {
    const MeshResource* mesh = nullptr; //< Renderer所有。Frameの記録中だけ借用
    const ConstantBuffer* constants = nullptr; //< 通常描画と同一のWorld/姿勢/Camera
    SubmeshRange submesh;
};
/*-----------------------------------------------------------------------------------------
 * SelectionOutline
 * - 選択Geometryの独立マスクと画面上の外周合成を所有する
 * - SceneのDepthを参照せず、通常描画完了後に遮蔽物を越えて輪郭を表示する
 *---------------------------------------------------------------------------------------*/
class SelectionOutline final {
public:
    SelectionOutline() noexcept;
    ~SelectionOutline() noexcept;
    /** \brief マスク/合成PSOを作る。Deviceの所有はFactory側にある */
    Result<void> Initialize(GraphicsResourceFactory& resources, const std::filesystem::path& shaders);
    /** \brief マスクを深度なしで描き、Sceneへ外周だけを合成する */
    Result<void> Render(GraphicsContext& context, std::span<const SelectionOutlineDraw> draws);
    /** \brief GPU idle保証後にAllocator/Deviceより先に解放する */
    void Shutdown() noexcept;
private:
    class Impl;
    std::unique_ptr<Impl> impl_; //< BackendのMask画像・Descriptor・PSOの所有者
};
}
