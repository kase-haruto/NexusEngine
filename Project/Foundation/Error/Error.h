#pragma once

// c++
#include <cstdint>
#include <memory>
#include <source_location>
#include <string>

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * ErrorCategory
	 * - エンジン共通エラーの発生領域を表す
	 *---------------------------------------------------------------------------------------*/
	enum class ErrorCategory : uint8_t {
		General,
		Framework,
		Window,
		Graphics,
		DirectX,
		FileSystem,
		Resource,
		Scene
	};

	/*-----------------------------------------------------------------------------------------
	 * Error
	 * - エンジン境界を越えて伝播するエラー情報を所有する
	 * - DirectX固有値は任意のNativeErrorCodeへ変換して保持する
	 *---------------------------------------------------------------------------------------*/
	class Error {
	public:
		using NativeErrorCode = int64_t;

		/**
		 * \brief エラー情報を生成する
		 * \param category エラーの分類
		 * \param code 分類内のエラーコード
		 * \param message エラーの説明
		 * \param nativeCode OSやAPI固有のエラーコード
		 * \param location エラー発生位置
		 */
		Error(
			ErrorCategory category,
			int32_t code,
			std::string message,
			NativeErrorCode nativeCode = 0,
			std::source_location location = std::source_location::current());

		[[nodiscard]] ErrorCategory GetCategory() const noexcept;
		[[nodiscard]] int32_t GetCode() const noexcept;
		[[nodiscard]] NativeErrorCode GetNativeCode() const noexcept;
		[[nodiscard]] const std::string& GetMessageText() const noexcept;
		[[nodiscard]] const std::source_location& GetLocation() const noexcept;
		[[nodiscard]] const Error* GetCause() const noexcept;

		/**
		 * \brief 元になったエラーを設定する
		 * \param cause 所有権を共有する原因エラー
		 * \return 原因情報を設定した自身
		 */
		Error& SetCause(std::shared_ptr<const Error> cause) noexcept;

	private:
		ErrorCategory				 category_	  = ErrorCategory::General; //< エラーの発生領域
		int32_t						 code_		  = 0;						//< 分類内の識別コード
		NativeErrorCode				 nativeCode_  = 0;						//< OSやAPI固有の補助コード
		std::string					 message_;								//< 呼び出し元へ伝える説明
		std::source_location			 location_;								//< 生成マクロから取得した発生位置
		std::shared_ptr<const Error> cause_;								//< 必要な場合だけ所有する原因エラー
	};

	/**
	 * \brief HRESULT相当の値からDirectXエラーを生成する
	 * \param code エンジン内の識別コード
	 * \param nativeCode HRESULTを符号付き整数へ変換した値
	 * \param message エラーの説明
	 * \param location エラー発生位置
	 * \return DirectX分類の共通エラー
	 */
	[[nodiscard]] Error MakeDirectXError(
		int32_t code,
		Error::NativeErrorCode nativeCode,
		std::string message,
		std::source_location location = std::source_location::current());

} // namespace NexusEngine
