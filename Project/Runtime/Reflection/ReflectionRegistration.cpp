#include "ReflectionRegistration.h"
#include "ComponentReflection.h"
#include "NativeComponentMetadata.h"
#include "Runtime/Scene/Components/GameComponentData.h"
#include <stdexcept>

namespace NexusEngine {
void ReflectionRegistration::RegisterComponents(ComponentReflection& registry) const {
    if(components) components(registry);
    // 過去Levelに保存された未知のDLL値も、Metadataなしで保持・再保存できるようにする。
    if(!registry.Types().contains(ReflectionTraits<GameComponentData>::Id)) registry.Register<GameComponentData>();
}
void ReflectionRegistration::RegisterInspector(TypeRegistry& registry) const {
    if(inspector) inspector(registry);
    if(nativeComponents && !nativeComponents->Register(registry)) throw std::runtime_error("Native Component metadata collides with host Reflection.");
}
}
