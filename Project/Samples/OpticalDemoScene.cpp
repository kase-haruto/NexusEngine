#include "OpticalDemoScene.h"
#include "Graphics/Model/PrimitiveMeshBuilder.h"

namespace NexusEngine {
	Result<OpticalSphereMaterialSlots> AppendSponzaOpticalSpheres(ModelAssetData& model) {
		const uint32_t firstMaterial = static_cast<uint32_t>(model.materials.size());
		auto mirror = PrimitiveMeshBuilder::Sphere(0.7f, 32, 64, firstMaterial);
		if(!mirror) return std::unexpected(std::move(mirror.error()));
		auto glass = PrimitiveMeshBuilder::Sphere(0.7f, 32, 64, firstMaterial + 1);
		if(!glass) return std::unexpected(std::move(glass.error()));
		ModelMaterialAssetData mirrorMaterial; mirrorMaterial.name = "Mirror Sphere";
		mirrorMaterial.optics = {1, 0, 1.5f, {0,0,0}}; model.materials.push_back(mirrorMaterial);
		ModelMaterialAssetData glassMaterial; glassMaterial.name = "Glass Sphere";
		glassMaterial.optics = {0, 1, 1.5f, {0.35f,0.08f,0.02f}}; model.materials.push_back(glassMaterial);
		auto add = [&](const char* name, MeshAssetData mesh, Vector3 position) {
			ModelNodeAssetData node; node.name = name;
			node.meshIndices = {static_cast<uint32_t>(model.meshes.size())};
			// Sponza's imported root is scaled by .008. Independent roots preserve the
			// sphere radius and World placement instead of inheriting that asset conversion.
			node.localTransform.translation = position;
			model.meshes.push_back(std::move(mesh)); model.nodes.push_back(std::move(node));
		};
		add("Reflection Test Sphere (Left)", std::move(*mirror), {-3.0f,0.75f,0.9f});
		add("Transmission Test Sphere (Right)", std::move(*glass), {-3.0f,0.75f,-0.9f});
		return OpticalSphereMaterialSlots {firstMaterial, firstMaterial + 1};
	}
	Result<ModelAssetData> CreateOpticalDemoScene() {
		ModelAssetData model;
		auto material = [&](const char* name, std::array<float, 4> color, OpticalMaterial optics = {}) {
			ModelMaterialAssetData value; value.name = name; value.baseColorFactor = color; value.optics = optics;
			model.materials.push_back(value); return static_cast<uint32_t>(model.materials.size() - 1);
		};
		const uint32_t white = material("Neutral Floor", {0.45f, 0.45f, 0.45f, 1});
		const uint32_t red = material("Red Reflection Reference", {0.9f, 0.04f, 0.04f, 1});
		const uint32_t green = material("Green Reflection Reference", {0.04f, 0.8f, 0.12f, 1});
		const uint32_t blue = material("Blue Transmission Reference", {0.04f, 0.15f, 0.95f, 1});
		const uint32_t yellow = material("Yellow Transmission Reference", {0.95f, 0.75f, 0.05f, 1});
		const uint32_t mirror = material("Mirror Sphere", {1, 1, 1, 1}, {1, 0, 1.5f, {0,0,0}});
		const uint32_t glass = material("Glass Sphere", {1, 1, 1, 1}, {0, 1, 1.5f, {0.35f,0.08f,0.02f}});
		auto add = [&](const char* name, MeshAssetData mesh, Vector3 position, Vector3 scale) {
			const uint32_t index = static_cast<uint32_t>(model.meshes.size()); model.meshes.push_back(std::move(mesh));
			ModelNodeAssetData node; node.name = name; node.meshIndices = {index};
			node.localTransform.translation = position; node.localTransform.scale = scale; model.nodes.push_back(node);
		};
		add("Floor", PrimitiveMeshBuilder::Box(white), {0,-0.15f,2}, {12,0.3f,14});
		add("Red Wall", PrimitiveMeshBuilder::Box(red), {-5,2.5f,2}, {0.2f,5,12});
		add("Green Wall", PrimitiveMeshBuilder::Box(green), {5,2.5f,2}, {0.2f,5,12});
		for(int i = 0; i < 8; ++i) add("Refraction Stripe", PrimitiveMeshBuilder::Box(i % 2 ? blue : yellow),
			{-4.375f + static_cast<float>(i) * 1.25f,2.5f,6}, {1.25f,5,0.2f});
		auto sphere = PrimitiveMeshBuilder::Sphere(1, 32, 64, mirror);
		if(!sphere) return std::unexpected(std::move(sphere.error()));
		add("Reflection Test Sphere (Left)", std::move(*sphere), {-1.55f,1.05f,1}, {1,1,1});
		sphere = PrimitiveMeshBuilder::Sphere(1, 32, 64, glass);
		if(!sphere) return std::unexpected(std::move(sphere.error()));
		add("Transmission Test Sphere (Right)", std::move(*sphere), {1.55f,1.05f,1}, {1,1,1});
		return model;
	}
}
