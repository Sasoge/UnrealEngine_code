#pragma once

#include "CoreMinimal.h"
#include "Math/Color.h"

// [DeferredToon] Stable semantic IDs keep assets independent from individual
// scene-texture members.  New providers can be added without changing the
// packed input-layer layout consumed by the volume pass.
enum class EDeferredToonInputId : uint16
{
	Unknown = 0,
	DirectLight = 1,
	NativeShadow = 2,
	ShadowChannel = 3,
	AmbientOcclusion = 4,
	BaseColor = 5,
	WorldNormal = 6,
	Roughness = 7,
	ShadingModel = 8,
	Depth = 9,
	Velocity = 10,
	// [DeferredToon] Unshadowed direct specular contribution from the split MRT.
	SpecularDirectLight = 11,
	// [DeferredToon] Dimensionless max positive N.L, physically packed in specular alpha.
	DirectLightFacing = 12,
	// [DeferredToon] New semantics; existing IDs and bounded layer ABI stay stable.
	EffectiveDirectLight = 13,
	EffectiveLightResponse = 14,
	IndirectDiffuse = 15, // [DeferredToon] Scene-linear material-weighted diffuse.
	SkyDiffuseBeforeAO = 16, // [DeferredToon] RGB, decoded from pre-exposure.
	SkyOcclusion = 17, // [DeferredToon] Combined sky visibility, white = unoccluded.

	// [DeferredToon] Retain retired IDs so old assets can resolve to a
	// documented fallback instead of silently changing their meaning.
	DeprecatedShadowBuffer = 100,
};

enum class EDeferredToonInputSource : uint8
{
	None = 0,
	LightingBuffer,
	ShadowBuffer,
	// [DeferredToon] Separate provider keeps roughness-dependent highlights out of DirectLight.
	SpecularBuffer,
	SceneTextures,
	GBuffer,
	EffectiveLightBuffer, // [DeferredToon]
	IndirectDiffuseBuffer, // [DeferredToon]
	SkyDiffuseBeforeAOBuffer, // [DeferredToon] Two semantic inputs share one RGBA16F resource.
};

enum class EDeferredToonInputChannel : uint8
{
	RGB = 0,
	R,
	G,
	B,
	A,
	Luminance,
};

enum class EDeferredToonInputDecode : uint8
{
	Linear = 0,
	Normalized,
	SRGBToLinear,
	DeviceZToLinear,
};

enum class EDeferredToonInputColorSpace : uint8
{
	Linear = 0,
	SRGB,
	Encoded,
};

// [DeferredToon] Layer operations are data, not shader permutations.  This
// keeps the authoring contract extensible while preserving one shader path.
enum class EDeferredToonLayerOperation : uint8
{
	Multiply = 0,
	Add,
	Replace,
};

// [DeferredToon] This descriptor is metadata only.  It deliberately contains
// no per-frame RDG/RHI handles; a provider resolves the source at render time.
struct FDeferredToonInputDescriptor
{
	EDeferredToonInputId Id = EDeferredToonInputId::Unknown;
	FName Name = NAME_None;
	EDeferredToonInputSource Source = EDeferredToonInputSource::None;
	EDeferredToonInputChannel Channel = EDeferredToonInputChannel::RGB;
	EDeferredToonInputDecode Decode = EDeferredToonInputDecode::Linear;
	EDeferredToonInputColorSpace ColorSpace = EDeferredToonInputColorSpace::Linear;
	FVector4f DefaultValue = FVector4f(0.0f, 0.0f, 0.0f, 1.0f);
	EDeferredToonInputId FallbackId = EDeferredToonInputId::Unknown;
	bool bAvailable = false;
	bool bDeprecated = false;
};

// [DeferredToon] Packed layer data is stable and bounded per volume.  Its
// capacity is independent from the number of descriptors in the registry.
struct FDeferredToonInputLayer
{
	EDeferredToonInputId InputId = EDeferredToonInputId::Unknown;
	EDeferredToonInputChannel Channel = EDeferredToonInputChannel::RGB;
	EDeferredToonLayerOperation Operation = EDeferredToonLayerOperation::Multiply;
	uint8 bInvert = 0;
	float Weight = 1.0f;
};

struct FDeferredToonInputStack
{
	// [DeferredToon] Authoring capacity, not the count of registered inputs.
	// New providers remain selectable without increasing this shader ABI.
	static constexpr int32 MaxInputLayers = 12;
	TStaticArray<FDeferredToonInputLayer, MaxInputLayers> Layers;
	uint32 NumLayers = 0;
};

// [DeferredToon] Registration is intended for renderer startup/module setup.
// Consumers only resolve semantic IDs and do not depend on registration order.
class FDeferredToonInputRegistry
{
public:
	static constexpr uint32 InputContractVersion = 2;

	static ENGINE_API const TArray<FDeferredToonInputDescriptor>& GetDescriptors();
	static ENGINE_API const FDeferredToonInputDescriptor* ResolveDescriptor(EDeferredToonInputId InputId);
	static ENGINE_API const FDeferredToonInputDescriptor* ResolveDescriptor(uint16 StableId);
	static ENGINE_API EDeferredToonInputId ResolveDeprecatedId(EDeferredToonInputId InputId);
	static ENGINE_API bool IsAvailable(EDeferredToonInputId InputId);
	static ENGINE_API bool RegisterDescriptor(const FDeferredToonInputDescriptor& Descriptor);
	static ENGINE_API FDeferredToonInputLayer MakeFallbackLayer(EDeferredToonInputId InputId);
};
