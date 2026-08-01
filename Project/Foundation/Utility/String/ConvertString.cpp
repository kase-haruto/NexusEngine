#include "ConvertString.h"

#include <string>
#include <windows.h>

namespace NexusEngine {

	std::wstring ConvertString(const std::string& str) {
		// Win32変換APIへ長さ0のバッファを渡さず、空入力はそのまま空出力として扱う。
		if(str.empty()) {
			return std::wstring();
		}

		// 必要文字数を先に問い合わせ、終端nullを含まない入力長に対応した領域だけ確保する。
		auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
		if(sizeNeeded == 0) {
			// 既存APIとの互換性を維持し、変換不能な入力は空文字列として返す。
			return std::wstring();
		}
		std::wstring result(sizeNeeded, 0);
		// サイズ問い合わせと同じCode Page・入力長を用い、確保済み領域へ一度だけ変換する。
		MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
		return result;
	}

	std::string ConvertString(const std::wstring& str) {
		// 空入力では変換APIと動的確保を省略する。
		if(str.empty()) {
			return std::string();
		}

		// UTF-8へ変換後のバイト数は入力文字数と一致しないため、先に必要量を問い合わせる。
		auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
		if(sizeNeeded == 0) {
			return std::string();
		}
		std::string result(sizeNeeded, 0);
		// 問い合わせ結果と同じサイズの連続領域へ変換し、余分な終端領域を持たせない。
		WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
		return result;
	}

	std::wstring ConvertString(const std::strong_ordering& str) {
		// 比較結果をログや診断表示で扱える固定文字列へ変換する。
		if(str == std::strong_ordering::equal) {
			return L"equal";
		} else if(str == std::strong_ordering::greater) {
			return L"greater";
		} else {
			return L"less";
		}
	}

} // namespace NexusEngine
