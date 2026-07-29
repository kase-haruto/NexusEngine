#pragma once

// c++
#include <Windows.h>

// engine
#include "WindowDetails.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * ProjectWindow
	 * - プロジェクトウィンドウ
	 * - プロジェクトのウィンドウを管理する
	 *---------------------------------------------------------------------------------------*/
	class ProjectWindow {
	public:
		//===================================================================*/
		//                    public methods
		//===================================================================*/
		ProjectWindow() noexcept;
		~ProjectWindow() noexcept;

		/**
		 * @brief ウィンドウの初期化
		 * @param windowDetail ウィンドウの詳細情報
		 */
		void Initialize(const WindowDetail& detail = {});
		/**
		 * @brief ウィンドウのシャットダウン
		 */
		void Shutdown();
		/**
		 * @brief ウィンドウの更新
		 */
		void Update();

	private:
		//===================================================================*/
		//                    private members
		//===================================================================*/
		RECT	 rect_		  = {0, 0, 0, 0}; //< ウィンドウのサイズを表す構造体
		WNDCLASS windowClass_ = {};			  //< ウィンドウクラスの情報を表す構造体
	};

} // namespace NexusEngine
