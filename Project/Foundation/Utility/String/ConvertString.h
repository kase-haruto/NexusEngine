#pragma once

// c++
#include <compare>
#include <string>

namespace NexusEngine {
	/**
	 * @brief ログ出力
	 * @param message
	 */
	void Log(const std::string& message);
	/**
	 * @brief string 型変換
	 * @param str
	 * @return
	 */
	std::wstring ConvertString(const std::string& str);
	std::string	 ConvertString(const std::wstring& str);
	std::wstring ConvertString(const std::strong_ordering& str);
} // namespace NexusEngine
