#pragma once

namespace NexusEngine {
	/*-----------------------------------------------------------------------------------------
	 * Environment
	 * - 環境情報を提供する
	 *---------------------------------------------------------------------------------------*/
	class Environment {
	public:
		//===================================================================*/
		//                    public methods
		//===================================================================*/
		static const char* GetPlatformName() noexcept;
		static const char* GetPlatformVersion() noexcept;
		static const char* GetCompilerName() noexcept;
		static const char* GetCompilerVersion() noexcept;
		static const char* GetLanguageStandardVersion() noexcept;
	};
} // namespace NexusEngine