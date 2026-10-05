#pragma once

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * DetailsPanel
	 * - Level上で選択されたエンティティの詳細を編集する
	 * - ImGui、GPU、通信には依存しない。同一Process内の編集を担当する
	 *---------------------------------------------------------------------------------------*/
	class DetailsPanel {
	public:
		//===================================================================*/
		//                    public methods
		//===================================================================*/
		DetailsPanel()								 = default;
		~DetailsPanel()								 = default;
		DetailsPanel(const DetailsPanel&)			 = delete;
		DetailsPanel& operator=(const DetailsPanel&) = delete;


	};

} // namespace NexusEngine
