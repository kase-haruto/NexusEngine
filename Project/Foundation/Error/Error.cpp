#include "Error.h"

#include <utility>

namespace NexusEngine {

	/////////////////////////////////////////////////////////////////////////////////////////
	//	エラー情報を生成する
	/////////////////////////////////////////////////////////////////////////////////////////
	Error::Error(
		const ErrorCategory category,
		const int32_t code,
		std::string message,
		const NativeErrorCode nativeCode,
		const std::source_location location)
		: category_(category),
		  code_(code),
		  nativeCode_(nativeCode),
		  message_(std::move(message)),
		  location_(location) {
	}

	ErrorCategory Error::GetCategory() const noexcept {
		return category_;
	}

	int32_t Error::GetCode() const noexcept {
		return code_;
	}

	Error::NativeErrorCode Error::GetNativeCode() const noexcept {
		return nativeCode_;
	}

	const std::string& Error::GetMessageText() const noexcept {
		return message_;
	}

	const std::source_location& Error::GetLocation() const noexcept {
		return location_;
	}

	const Error* Error::GetCause() const noexcept {
		return cause_.get();
	}

	Error& Error::SetCause(std::shared_ptr<const Error> cause) noexcept {
		// 原因が必要な場合だけ共有所有を追加し、単独エラーの生成コストを増やさない。
		cause_ = std::move(cause);
		return *this;
	}

	Error MakeDirectXError(
		const int32_t code,
		const Error::NativeErrorCode nativeCode,
		std::string message,
		const std::source_location location) {
		// HRESULTを公開APIへ漏らさず、共通エラーのNativeErrorCodeへ格納する。
		return Error(ErrorCategory::DirectX, code, std::move(message), nativeCode, location);
	}

} // namespace NexusEngine
