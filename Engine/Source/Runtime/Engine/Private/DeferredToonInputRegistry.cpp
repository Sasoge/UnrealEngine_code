#include "Engine/DeferredToonInputRegistry.h"

namespace DeferredToonInputRegistryPrivate
{
	// [DeferredToon] Keep the built-in table in one place.  Adding a provider
	// appends metadata here or calls RegisterDescriptor during module setup;
	// neither operation changes FDeferredToonInputStack's packed ABI.
	static TArray<FDeferredToonInputDescriptor>& MutableDescriptors()
	{
		static TArray<FDeferredToonInputDescriptor> Descriptors = []
		{
			TArray<FDeferredToonInputDescriptor> Result;
			Result.Reserve(20); // [DeferredToon] Built-in semantics plus retired descriptor.

			auto Add = [&Result](
				EDeferredToonInputId Id,
				const TCHAR* Name,
				EDeferredToonInputSource Source,
				EDeferredToonInputChannel Channel,
				EDeferredToonInputDecode Decode,
				EDeferredToonInputColorSpace ColorSpace,
				const FVector4f& DefaultValue,
				bool bAvailable,
				bool bDeprecated = false,
				EDeferredToonInputId FallbackId = EDeferredToonInputId::Unknown)
			{
				FDeferredToonInputDescriptor Descriptor;
				Descriptor.Id = Id;
				Descriptor.Name = FName(Name);
				Descriptor.Source = Source;
				Descriptor.Channel = Channel;
				Descriptor.Decode = Decode;
				Descriptor.ColorSpace = ColorSpace;
				Descriptor.DefaultValue = DefaultValue;
				Descriptor.bAvailable = bAvailable;
				Descriptor.bDeprecated = bDeprecated;
				Descriptor.FallbackId = FallbackId;
				Result.Add(Descriptor);
			};

			Add(EDeferredToonInputId::DirectLight, TEXT("DirectLight"), EDeferredToonInputSource::LightingBuffer, EDeferredToonInputChannel::RGB, EDeferredToonInputDecode::Linear, EDeferredToonInputColorSpace::Linear, FVector4f(0.0f, 0.0f, 0.0f, 1.0f), true);
			Add(EDeferredToonInputId::NativeShadow, TEXT("NativeShadow"), EDeferredToonInputSource::ShadowBuffer, EDeferredToonInputChannel::R, EDeferredToonInputDecode::Normalized, EDeferredToonInputColorSpace::Linear, FVector4f(1.0f, 1.0f, 1.0f, 1.0f), true);
			Add(EDeferredToonInputId::ShadowChannel, TEXT("ShadowChannel"), EDeferredToonInputSource::LightingBuffer, EDeferredToonInputChannel::A, EDeferredToonInputDecode::Normalized, EDeferredToonInputColorSpace::Linear, FVector4f(0.0f, 0.0f, 0.0f, 1.0f), true);
			Add(EDeferredToonInputId::AmbientOcclusion, TEXT("AmbientOcclusion"), EDeferredToonInputSource::SceneTextures, EDeferredToonInputChannel::R, EDeferredToonInputDecode::Normalized, EDeferredToonInputColorSpace::Linear, FVector4f(1.0f, 1.0f, 1.0f, 1.0f), true);
			Add(EDeferredToonInputId::BaseColor, TEXT("BaseColor"), EDeferredToonInputSource::GBuffer, EDeferredToonInputChannel::RGB, EDeferredToonInputDecode::SRGBToLinear, EDeferredToonInputColorSpace::SRGB, FVector4f(1.0f, 1.0f, 1.0f, 1.0f), true);
			Add(EDeferredToonInputId::WorldNormal, TEXT("WorldNormal"), EDeferredToonInputSource::GBuffer, EDeferredToonInputChannel::RGB, EDeferredToonInputDecode::Normalized, EDeferredToonInputColorSpace::Encoded, FVector4f(0.0f, 0.0f, 1.0f, 1.0f), true);
			Add(EDeferredToonInputId::Roughness, TEXT("Roughness"), EDeferredToonInputSource::GBuffer, EDeferredToonInputChannel::R, EDeferredToonInputDecode::Normalized, EDeferredToonInputColorSpace::Linear, FVector4f(1.0f, 1.0f, 1.0f, 1.0f), true);
			Add(EDeferredToonInputId::ShadingModel, TEXT("ShadingModel"), EDeferredToonInputSource::GBuffer, EDeferredToonInputChannel::R, EDeferredToonInputDecode::Normalized, EDeferredToonInputColorSpace::Encoded, FVector4f(0.0f, 0.0f, 0.0f, 1.0f), true);
			Add(EDeferredToonInputId::Depth, TEXT("Depth"), EDeferredToonInputSource::SceneTextures, EDeferredToonInputChannel::R, EDeferredToonInputDecode::DeviceZToLinear, EDeferredToonInputColorSpace::Linear, FVector4f(1.0f, 1.0f, 1.0f, 1.0f), true);
			// [DeferredToon] The uniform always supplies a velocity texture (a black
			// fallback when motion vectors are disabled), so the semantic remains
			// selectable without a shader permutation or a special asset case.
			Add(EDeferredToonInputId::Velocity, TEXT("Velocity"), EDeferredToonInputSource::SceneTextures, EDeferredToonInputChannel::RGB, EDeferredToonInputDecode::Normalized, EDeferredToonInputColorSpace::Encoded, FVector4f(0.0f, 0.0f, 0.0f, 1.0f), true);
			// [DeferredToon] Keep specular as a separate semantic input so DirectLight
			// remains a diffuse mask independent of material roughness.
			Add(EDeferredToonInputId::SpecularDirectLight, TEXT("SpecularDirectLight"), EDeferredToonInputSource::SpecularBuffer, EDeferredToonInputChannel::RGB, EDeferredToonInputDecode::Linear, EDeferredToonInputColorSpace::Linear, FVector4f(0.0f, 0.0f, 0.0f, 1.0f), true);
			// [DeferredToon] A missing facing provider is unlit, including at threshold zero.
			Add(EDeferredToonInputId::DirectLightFacing, TEXT("DirectLightFacing"), EDeferredToonInputSource::SpecularBuffer, EDeferredToonInputChannel::R, EDeferredToonInputDecode::Normalized, EDeferredToonInputColorSpace::Linear, FVector4f(0.0f, 0.0f, 0.0f, 0.0f), true);
			// [DeferredToon] Shader decodes pre-exposure before exposing these semantic values.
			Add(EDeferredToonInputId::EffectiveDirectLight, TEXT("EffectiveDirectLight"), EDeferredToonInputSource::EffectiveLightBuffer, EDeferredToonInputChannel::RGB, EDeferredToonInputDecode::Linear, EDeferredToonInputColorSpace::Linear, FVector4f(0.0f, 0.0f, 0.0f, 0.0f), true);
			Add(EDeferredToonInputId::EffectiveLightResponse, TEXT("EffectiveLightResponse"), EDeferredToonInputSource::EffectiveLightBuffer, EDeferredToonInputChannel::RGB, EDeferredToonInputDecode::Linear, EDeferredToonInputColorSpace::Linear, FVector4f(0.0f, 0.0f, 0.0f, 0.0f), true);
			// [DeferredToon] Runtime availability is additionally checked per frame/pixel.
			Add(EDeferredToonInputId::IndirectDiffuse, TEXT("IndirectDiffuse"), EDeferredToonInputSource::IndirectDiffuseBuffer, EDeferredToonInputChannel::RGB, EDeferredToonInputDecode::Linear, EDeferredToonInputColorSpace::Linear, FVector4f(0, 0, 0, 0), true);
			// [DeferredToon] Shared provider, distinct semantic decoding and neutral fallbacks.
			Add(EDeferredToonInputId::SkyDiffuseBeforeAO, TEXT("SkyDiffuseBeforeAO"), EDeferredToonInputSource::SkyDiffuseBeforeAOBuffer, EDeferredToonInputChannel::RGB, EDeferredToonInputDecode::Linear, EDeferredToonInputColorSpace::Linear, FVector4f(0, 0, 0, 0), true);
			Add(EDeferredToonInputId::SkyOcclusion, TEXT("SkyOcclusion"), EDeferredToonInputSource::SkyDiffuseBeforeAOBuffer, EDeferredToonInputChannel::R, EDeferredToonInputDecode::Normalized, EDeferredToonInputColorSpace::Linear, FVector4f(1, 1, 1, 1), true);
            // [DeferredToon] Source choice is deferred to the shader, including pixel validity.
            Add(EDeferredToonInputId::IndirectOcclusion, TEXT("IndirectOcclusion"), EDeferredToonInputSource::IndirectOcclusionBuffers, EDeferredToonInputChannel::R, EDeferredToonInputDecode::Normalized, EDeferredToonInputColorSpace::Linear, FVector4f(1, 1, 1, 1), true);
            Add(EDeferredToonInputId::IndirectDiffuseBeforeOcclusion, TEXT("IndirectDiffuseBeforeOcclusion"), EDeferredToonInputSource::IndirectOcclusionBuffers, EDeferredToonInputChannel::RGB, EDeferredToonInputDecode::Linear, EDeferredToonInputColorSpace::Linear, FVector4f(0, 0, 0, 0), true);
			Add(EDeferredToonInputId::DeprecatedShadowBuffer, TEXT("DeprecatedShadowBuffer"), EDeferredToonInputSource::ShadowBuffer, EDeferredToonInputChannel::R, EDeferredToonInputDecode::Normalized, EDeferredToonInputColorSpace::Linear, FVector4f(1.0f, 1.0f, 1.0f, 1.0f), false, true, EDeferredToonInputId::NativeShadow);
			return Result;
		}();
		return Descriptors;
	}
}

const TArray<FDeferredToonInputDescriptor>& FDeferredToonInputRegistry::GetDescriptors()
{
	return DeferredToonInputRegistryPrivate::MutableDescriptors();
}

const FDeferredToonInputDescriptor* FDeferredToonInputRegistry::ResolveDescriptor(EDeferredToonInputId InputId)
{
	return GetDescriptors().FindByPredicate([InputId](const FDeferredToonInputDescriptor& Descriptor)
	{
		return Descriptor.Id == InputId;
	});
}

const FDeferredToonInputDescriptor* FDeferredToonInputRegistry::ResolveDescriptor(uint16 StableId)
{
	return ResolveDescriptor(static_cast<EDeferredToonInputId>(StableId));
}

EDeferredToonInputId FDeferredToonInputRegistry::ResolveDeprecatedId(EDeferredToonInputId InputId)
{
	const FDeferredToonInputDescriptor* Descriptor = ResolveDescriptor(InputId);
	return Descriptor && Descriptor->bDeprecated && Descriptor->FallbackId != EDeferredToonInputId::Unknown
		? Descriptor->FallbackId
		: InputId;
}

bool FDeferredToonInputRegistry::IsAvailable(EDeferredToonInputId InputId)
{
	const FDeferredToonInputDescriptor* Descriptor = ResolveDescriptor(ResolveDeprecatedId(InputId));
	return Descriptor && Descriptor->bAvailable;
}

bool FDeferredToonInputRegistry::RegisterDescriptor(const FDeferredToonInputDescriptor& Descriptor)
{
	if (Descriptor.Id == EDeferredToonInputId::Unknown)
	{
		return false;
	}

	TArray<FDeferredToonInputDescriptor>& Descriptors = DeferredToonInputRegistryPrivate::MutableDescriptors();
	if (FDeferredToonInputDescriptor* Existing = Descriptors.FindByPredicate([&Descriptor](const FDeferredToonInputDescriptor& Candidate)
	{
		return Candidate.Id == Descriptor.Id;
	}))
	{
		*Existing = Descriptor;
	}
	else
	{
		Descriptors.Add(Descriptor);
	}
	return true;
}

FDeferredToonInputLayer FDeferredToonInputRegistry::MakeFallbackLayer(EDeferredToonInputId InputId)
{
	FDeferredToonInputLayer Layer;
	Layer.InputId = ResolveDeprecatedId(InputId);
	Layer.Weight = IsAvailable(Layer.InputId) ? 1.0f : 0.0f;
	return Layer;
}
