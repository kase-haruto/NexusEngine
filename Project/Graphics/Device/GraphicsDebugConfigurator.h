#pragma once

// c++
#include <d3d12.h>

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * GraphicsDebugConfigurator
	 * - Device生成前のDebug LayerとDRED、生成後のInfo Queueを設定する
	 * - DREDレポート解析やGPUクラッシュレポートの保管は管理しない
	 *---------------------------------------------------------------------------------------*/
	class GraphicsDebugConfigurator final {
	public:
		/**
		 * \brief Device生成前にD3D12 Debug Layerを設定する
		 * \param enableDebugLayer Debug Layerを要求する場合true
		 * \param enableGpuBasedValidation GPU Based Validationを要求する場合true
		 */
		static void ConfigureDebugLayer(bool enableDebugLayer, bool enableGpuBasedValidation) noexcept;

		/**
		 * \brief Device生成前にDREDの収集設定を有効化する
		 * \param enableDred DREDを要求する場合true
		 */
		static void ConfigureDred(bool enableDred) noexcept;

		/**
		 * \brief Device生成後のInfo QueueへBreak Severityを設定する
		 * \param device 設定対象の非所有Device
		 */
		static void ConfigureInfoQueue(ID3D12Device* device) noexcept;
	};

} // namespace NexusEngine
