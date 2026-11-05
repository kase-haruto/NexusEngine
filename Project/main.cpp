#include "Game/SampleGame.h"
#include "Samples/StartupAssets.h"
// サンプル固有の登録・旧モデル読込は構成ルートだけに残す。
#define NEXUS_PROJECT_REFLECTION NexusGame::SampleReflection()
#define NEXUS_PROJECT_SETUP NexusGame::SetupSampleGame
#define NEXUS_PROJECT_STARTUP_LOADER(assetRoot) [assetRoot] { return NexusEngine::LoadSampleStartupAsset(assetRoot); }
#include "Core/Hosting/ProjectHost.inl"