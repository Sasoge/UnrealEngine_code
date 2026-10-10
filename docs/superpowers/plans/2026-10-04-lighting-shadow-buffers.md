# Lighting and Shadow Buffers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add separate deferred-rendering buffers for unshadowed direct lighting and accumulated shadow contribution, ready for later world-space box color mapping.

**Architecture:** When `r.Toon.LightingShadowBuffers` is enabled, Deferred Light writes SceneColor plus two MRTs: `LightingBuffer` (`PF_FloatRGBA`) stores direct lighting with native shadow visibility neutralized, and `ShadowBuffer` (`PF_R16F`) stores the corresponding occlusion contribution. All Deferred lights that contribute direct lighting are temporarily routed through the standard non-clustered Deferred Light path; Clustered Deferred and MegaLights support is deferred to a later phase.

**Tech Stack:** UE 5.6 C++, RDG, `FSceneTextures`, `FSceneTextureUniformParameters`, Deferred Light global shaders, HLSL, Python source-contract tests, Rider/UBT build.

**Spec:** `D:/UnrealEngine-release/docs/superpowers/specs/2026-10-04-shadow-channel-buffer-design.md`

## Global Constraints

- The feature is Deferred Shading Path only; mobile and forward paths use black fallbacks.
- `LightingBuffer` stores HDR direct lighting with native shadow visibility set to `1` while preserving local light influence and material response.
- `ShadowBuffer` stores positive accumulated shadow contribution, not final color; values may exceed `1` before later mapping.
- A light whose Lighting Channels do not match but whose Shadow Channel matches writes no `LightingBuffer` contribution and may write Shadow Channel shadow contribution.
- Existing SceneColor lighting and Shadow Channel subtraction remain unchanged in this phase.
- When enabled, all Deferred lights that write either buffer temporarily leave Clustered Deferred and MegaLights.
- All additions must use `[ShadowChannelPrototype]` or `[LightingShadowBuffer]` comments.

## Review Focus

- **Unshadowed lighting correctness:** `LightingBuffer` must keep local light range and material response while removing only native shadow visibility.
- **Shadow-only lights:** a Lighting-Channel mismatch must not create direct-light buffer data, while a matching Shadow Channel can still create shadow data.
- **MRT slot collisions:** Toon Main Light, ToonShadow, Substrate and the two new outputs must bind deterministic targets.
- **Accumulation and clear:** both buffers clear once per view, then load and add for every contributing light.
- **Routing completeness:** no Deferred light that should write either buffer may bypass through Clustered Deferred or MegaLights.

---

### Task 1: Add LightingBuffer and ShadowBuffer scene textures

**Files:**
- Modify: `Engine/Source/Runtime/Renderer/Public/SceneRenderTargetParameters.h`
- Modify: `Engine/Source/Runtime/Renderer/Internal/SceneTextures.h`
- Modify: `Engine/Source/Runtime/Engine/Public/SceneTexturesConfig.h`
- Modify: `Engine/Source/Runtime/Renderer/Private/SceneTextures.cpp`
- Modify: `Engine/Shaders/Private/DeferredShadingCommon.ush`
- Modify: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp`
- Test: `C:/Users/ASUS/Documents/Codex/2026-10-02/qin/tests/test_lighting_shadow_buffers_contract.py`

**Interfaces:**
- Produces `FSceneTextures::LightingBuffer` and `FSceneTextures::ShadowBuffer`.
- Adds `ESceneTexture::LightingBuffer`, `ESceneTexture::ShadowBuffer`, and matching setup flags.
- Adds `FSceneTextureUniformParameters::LightingBufferTexture` and `ShadowBufferTexture` plus HLSL fields.
- Adds `r.Toon.LightingShadowBuffers` and `bool IsLightingShadowBuffersEnabled()`.

- [ ] **Step 1: Write failing contract tests**

  Assert both enum flags, scene texture members, uniform fields, `PF_FloatRGBA`/`PF_R16F` allocation, black fallbacks, and the cvar name.

- [ ] **Step 2: Run the focused tests**

  Run: `python -m pytest tests/test_lighting_shadow_buffers_contract.py -q`

  Expected: FAIL because the new symbols do not exist.

- [ ] **Step 3: Add both texture allocations and uniform plumbing**

  Allocate both textures only for Deferred Shading when the cvar is enabled. Use render-target and shader-resource flags, clear value zero, and debug names `LightingBuffer` and `ShadowBuffer`. Bind black fallbacks when disabled or unavailable.

- [ ] **Step 4: Add one-time per-view clearing**

  Before deferred lights, issue one clear pass for both textures. Do not clear inside a light pass; all later light passes must load and accumulate.

- [ ] **Step 5: Run the focused tests**

  Run: `python -m pytest tests/test_lighting_shadow_buffers_contract.py -q`

  Expected: PASS.

### Task 2: Compute and output separated light/shadow contributions

**Files:**
- Modify: `Engine/Source/Runtime/Renderer/Private/LightRendering.cpp`
- Modify: `Engine/Shaders/Private/DeferredLightPixelShaders.usf`
- Test: `C:/Users/ASUS/Documents/Codex/2026-10-02/qin/tests/test_lighting_shadow_buffers_contract.py`

**Interfaces:**
- Adds `FLightingShadowBufferOutputDim` to `FDeferredLightPS`.
- Adds conditional `LightingContribution` and `ShadowContribution` pixel outputs under `LIGHTING_SHADOW_BUFFER_OUTPUT`.

- [ ] **Step 1: Add failing shader contract tests**

  Assert the permutation, both outputs, the unshadowed-light path, native shadow visibility input, local light influence, receiver filtering, and Shadow Channel strength/curve terms.

- [ ] **Step 2: Run the focused tests**

  Run: `python -m pytest tests/test_lighting_shadow_buffers_contract.py -q`

  Expected: FAIL on the missing permutation/output markers.

- [ ] **Step 3: Add the lighting output**

  In both Substrate and non-Substrate branches, evaluate the current light contribution with native shadow visibility neutralized while preserving local light attenuation, BRDF/material response, and light color. Write that result to `LightingBuffer` only when Lighting Channels match. Keep the existing SceneColor path unchanged.

- [ ] **Step 4: Add the shadow output**

  Convert the native visibility to positive occlusion and multiply by local light influence. Apply the existing Shadow Channel receiver match, subtraction strength, and CurveFloat factor only for Shadow Channel-only behavior; ordinary shadowing lights use weight `1`. Write zero when the receiver or light influence is invalid.

- [ ] **Step 5: Preserve existing outputs**

  Keep current SceneColor subtraction and `EncodeToonShadow` behavior unchanged. The new outputs are additional data and must not replace the current visual result.

- [ ] **Step 6: Run the focused tests**

  Run: `python -m pytest tests/test_lighting_shadow_buffers_contract.py -q`

  Expected: PASS.

### Task 3: Bind both MRTs and route all contributing lights

**Files:**
- Modify: `Engine/Source/Runtime/Renderer/Private/LightRendering.cpp`
- Modify: `Engine/Source/Runtime/Renderer/Private/LightRendering.h`
- Modify: `Engine/Shaders/Private/DeferredLightPixelShaders.usf`
- Test: `C:/Users/ASUS/Documents/Codex/2026-10-02/qin/tests/test_lighting_shadow_buffers_contract.py`

**Interfaces:**
- Extends deferred-light parameter setup with `FRDGTextureRef LightingBufferTexture`, `FRDGTextureRef ShadowBufferTexture`, and `bWriteLightingShadowBuffers`.
- Uses one deterministic MRT slot layout for SceneColor, LightingBuffer, ShadowBuffer, ToonShadow, and Substrate targets.

- [ ] **Step 1: Add failing MRT/routing tests**

  Assert both buffers use `ELoad` after the clear, the output permutation is enabled for all contributing Deferred lights, and Clustered/MegaLights routing checks the cvar rather than only Shadow Channel state.

- [ ] **Step 2: Run the focused tests**

  Run: `python -m pytest tests/test_lighting_shadow_buffers_contract.py -q`

  Expected: FAIL on the missing bindings and routing markers.

- [ ] **Step 3: Centralize target assignment**

  Keep `Target0` as SceneColor, reserve deterministic slots for LightingBuffer and ShadowBuffer, then place ToonShadow and Substrate targets according to the active permutation. Handle the combined Toon Main Light + Shadow Channel + Substrate case explicitly.

- [ ] **Step 4: Bind both buffers for contributing Deferred lights**

  Pass both textures through every standard Deferred Light call site. Bind both with `ELoad`; disabled views keep the original target layout.

- [ ] **Step 5: Force contributing lights out of Clustered Deferred and MegaLights**

  When `IsLightingShadowBuffersEnabled()` is true, route every Deferred light that contributes direct lighting or shadow data through the standard path. Leave unsupported special paths unchanged until a later integration phase.

- [ ] **Step 6: Run the focused tests**

  Run: `python -m pytest tests/test_lighting_shadow_buffers_contract.py -q`

  Expected: PASS.

### Task 4: Add separate debug visualization and update records

**Files:**
- Modify: `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessing.cpp`
- Modify: `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessing.h`
- Modify: `D:/UnrealEngine-release/ShadowChannelPrototype.md`
- Modify: `D:/UnrealEngine-release/docs/superpowers/specs/2026-10-04-shadow-channel-buffer-design.md`
- Test: `C:/Users/ASUS/Documents/Codex/2026-10-02/qin/tests/test_lighting_shadow_buffers_contract.py`

**Interfaces:**
- Adds temporary grayscale debug views for LightingBuffer and ShadowBuffer.
- Documents that box mapping is a later pass over the two separate inputs.

- [ ] **Step 1: Add failing debug/documentation tests**

  Assert both texture names are read by the debug path and that the records describe their distinct semantics and routing tradeoff.

- [ ] **Step 2: Run the focused tests**

  Run: `python -m pytest tests/test_lighting_shadow_buffers_contract.py -q`

  Expected: FAIL on the missing debug/documentation markers.

- [ ] **Step 3: Add debug views**

  Add console-controlled grayscale views for LightingBuffer and ShadowBuffer before tone mapping. Disabled mode must leave normal output unchanged.

- [ ] **Step 4: Update documentation**

  Record the two-buffer architecture, unshadowed lighting definition, Shadow Channel-only behavior, one-time clear plus additive MRT, and temporary non-clustered routing.

- [ ] **Step 5: Run the focused tests**

  Run: `python -m pytest tests/test_lighting_shadow_buffers_contract.py -q`

  Expected: PASS.

### Task 5: Build and RenderDoc validation

**Files:**
- Test: `C:/Users/ASUS/Documents/Codex/2026-10-02/qin/tests/test_lighting_shadow_buffers_contract.py`
- Build: `D:/UnrealEngine-release/UE5.sln` through Rider/UBT

- [ ] **Step 1: Run all source-contract tests**

  Run: `python -m pytest tests -q`

  Expected: PASS.

- [ ] **Step 2: Compile UnrealEditor and ShaderCompileWorker**

  Build `UnrealEditor Win64 Development` and `ShaderCompileWorker Win64 Development` in Rider. Expected: both targets complete without C++ or shader compilation errors.

- [ ] **Step 3: Recompile changed shaders**

  Run `recompileshaders changed` in the test project and confirm the two-buffer permutations compile.

- [ ] **Step 4: Validate separated outputs**

  Capture: ordinary matched light, Shadow Channel-only light, mismatched receiver, point-light radius boundary, ordinary shadow plus Shadow Channel shadow overlap, and two overlapping lights. Inspect LightingBuffer and ShadowBuffer separately in RenderDoc.

- [ ] **Step 5: Compare SceneColor against the baseline**

  Compare the same scene with `r.Toon.LightingShadowBuffers 0` and `1`. Existing SceneColor must remain unchanged except for the documented temporary routing/performance difference.
