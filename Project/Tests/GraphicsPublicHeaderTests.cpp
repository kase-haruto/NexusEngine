// Compile this translation unit with no Windows/DirectX pre-includes. A transitive native
// include is a dependency regression even when Renderer does not name the type itself.
#include "Graphics/Renderer/PrimitiveRenderer.h"
#include "Graphics/Renderer/GraphicsSystem.h"
#include "Graphics/Shadow/ShadowSystem.h"
#include "Graphics/RayTracing/OpticalSystem.h"
#include "Runtime/Rendering/RenderSceneExtractor.h"
#include "Runtime/Level/Level.h"

#if defined(__d3d12_h__) || defined(__dxgi_h__) || defined(__dxgi1_6_h__)
#error DirectX headers leaked through the public Renderer/Scene boundary.
#endif
