# Deferred Toon 直接光照漫反射/高光拆分 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 Deferred Light 的未受阴影直接光照拆分为漫反射和高光两个可独立采样的 RT，使 `DirectLightMask` 不再受 Roughness 高光影响。

**Architecture:** 保留现有 `LightingBuffer` 和 `ShadowBuffer` 的资源位置与阴影语义，把 `LightingBuffer.rgb` 改为直接漫反射，并新增 `SpecularBuffer.rgb` 保存直接高光。Deferred Light 使用 UE 已有的 split lighting 结果写入两个 MRT，盒体语义层分别读取两个资源；最终合成暂不改变，只扩展输入和调试能力。

**Tech Stack:** Unreal Engine C++ renderer, RDG scene textures, HLSL/USF deferred-light shaders, Python contract tests, Unreal Build Accelerator.

**Spec:** `docs/superpowers/specs/2026-10-13-deferred-toon-diffuse-specular-design.md`

## Global Constraints

- `ShadowBuffer` 的格式、清理方式、采样方式和当前帧语义不变。
- `LightingBuffer.a` 继续保存 Shadow Channel subtraction mask。
- 新增和修改的 UE 源码行使用 `[DeferredToon]` 或 `[LightingShadowBuffer]` 注释标记。
- `r.Toon.LightingShadowBuffers` 继续控制自定义缓冲分配和写入。
- 直接光拆分只覆盖 Deferred Light 产生的直接光，不包含间接光、环境反射或后处理反射。
- 盒体 pass 仍在 TAA/TSR 之前执行，不能改变现有 pass 顺序。

## Review Focus

- 资源不可用或功能开关关闭时，`SpecularBufferTexture` 必须绑定黑色 fallback，不能读取无效 RDG 资源；由 Task 2 测试。
- 多个光源的漫反射和高光必须使用 additive MRT 累积；由 Task 3 测试。
- Substrate 和非 Substrate 必须使用真实 split 结果，不能通过总颜色减法近似；由 Task 3 测试。
- Hair 路径的输出槽位和 pre-exposure/coverage 必须与普通路径一致；由 Task 3 测试。
- `DirectLightMask` 只能读取漫反射输入，Specular 调试视图必须读取新 RT，且不能改变 ShadowBuffer 视图；由 Task 4 测试。

---

### Task 1: Add contract tests for split direct-light resources and semantics

**Files:**
- Create: `tests/test_deferred_toon_lighting_split_contract.py`
- Modify: `tests/test_deferred_toon_direct_light_contract.py`
- Modify: `tests/test_deferred_toon_style_layers_contract.py`

**Interfaces:**
- Consumes: current `FSceneTextures`, `ESceneTextureSetupMode`, deferred-light MRT declarations, and volume semantic/debug enums.
- Produces: failing assertions that pin `SpecularBuffer`, split shader outputs, semantic input ID, and the next debug-view range.

- [ ] **Step 1: Write the failing tests**

  Assert that:
  - `FSceneTextures` declares `SpecularBuffer`.
  - `ESceneTexture` and `ESceneTextureSetupMode` expose `SpecularBuffer`.
  - Scene texture uniform parameters expose `SpecularBufferTexture` and bind black fallback when unavailable.
  - Deferred-light shader declares a `SpecularContribution` MRT output and initializes/writes it in both ordinary and hair paths.
  - `LightingContribution.rgb` is sourced from diffuse split values, not `SceneColor`/mixed radiance.
  - Volume shader declares and samples `SpecularBufferTexture` through a `SpecularDirectLight` semantic.
  - A new specular debug view exists after the current views and global/per-volume clamps include it.

- [ ] **Step 2: Run the focused tests to verify failure**

  Run:
  ```text
  python -m unittest tests.test_deferred_toon_lighting_split_contract tests.test_deferred_toon_direct_light_contract tests.test_deferred_toon_style_layers_contract
  ```

  Expected: FAIL because the new resource, MRT output, semantic, and debug view do not exist yet.

- [ ] **Step 3: Commit the failing contract tests**

  ```text
  git add tests/test_deferred_toon_lighting_split_contract.py tests/test_deferred_toon_direct_light_contract.py tests/test_deferred_toon_style_layers_contract.py
  git commit -m "test: define split direct-light buffer contract"
  ```

### Task 2: Add `SpecularBuffer` to SceneTextures and uniform bindings

**Files:**
- Modify: `Engine/Source/Runtime/Renderer/Internal/SceneTextures.h`
- Modify: `Engine/Source/Runtime/Renderer/Public/SceneRenderTargetParameters.h`
- Modify: `Engine/Source/Runtime/Renderer/Private/SceneTextures.cpp`
- Modify: `Engine/Source/Runtime/Renderer/Public/SceneTextureParameters.h` or the generated/declaring header containing `FSceneTextureUniformParameters`

**Interfaces:**
- Consumes: existing `LightingBuffer`/`ShadowBuffer` allocation and setup flags.
- Produces: `FSceneTextures::SpecularBuffer`, `ESceneTexture::SpecularBuffer`, `ESceneTextureSetupMode::SpecularBuffer`, and `SpecularBufferTexture` uniform binding.

- [ ] **Step 1: Add `SpecularBuffer` declarations and enum flags**

  Keep existing Lighting/Shadow enum values stable and append the new Specular entry. Use `PF_FloatRGBA`, render-targetable plus shader-resource flags, black clear, and the same extent/multiview settings as `LightingBuffer`.

- [ ] **Step 2: Bind actual or black fallback resources**

  Add `SpecularBuffer` to `GetSceneTexture`, visualization enumeration, uniform defaults, and produced-resource setup. Feature-off/mobile/forward paths must bind `SystemTextures.Black`.

- [ ] **Step 3: Run the focused contract tests**

  Run the Task 1 focused unittest command. Expected: SceneTextures/resource assertions pass; MRT/shader/semantic assertions remain failing.

- [ ] **Step 4: Commit the SceneTextures slice**

  ```text
  git add Engine/Source/Runtime/Renderer/Internal/SceneTextures.h Engine/Source/Runtime/Renderer/Public/SceneRenderTargetParameters.h Engine/Source/Runtime/Renderer/Private/SceneTextures.cpp Engine/Source/Runtime/Renderer/Public/SceneTextureParameters.h
  git commit -m "feat: allocate deferred toon specular buffer"
  ```

### Task 3: Split Deferred Light MRT output into diffuse and specular

**Files:**
- Modify: `Engine/Source/Runtime/Renderer/Private/LightRendering.h`
- Modify: `Engine/Source/Runtime/Renderer/Private/LightRendering.cpp`
- Modify: `Engine/Shaders/Private/DeferredLightPixelShaders.usf`
- Modify: `Engine/Shaders/Private/DeferredLightingCommon.ush` only if a helper signature is required by the existing split API
- Modify: `Engine/Shaders/Private/LightAccumulator.ush` only if the existing Substrate totals need a documented helper

**Interfaces:**
- Consumes: `FSceneTextures::SpecularBuffer`, existing Lighting/Shadow MRTs, `GetDynamicLightingSplit`, `FSubstrateDeferredLighting.TotalDiffuseLighting`, and `TotalSpecularLighting`.
- Produces: additive `LightingContribution` (diffuse RGB + Shadow Channel alpha), additive `SpecularContribution` (specular RGB), and unchanged `ShadowContribution` (native scalar).

- [ ] **Step 1: Update MRT slot declarations and parameter plumbing**

  Add a `SpecularBuffer` slot without moving the existing SceneColor, LightingBuffer, or ShadowBuffer slots. Shift Toon/Substrate slots after the new target. Pass the actual texture and bind it in ordinary, simple-light, hair, and all relevant deferred-light paths. Update PSO render-target format/count metadata.

- [ ] **Step 2: Write split outputs for non-Substrate pixels**

  Replace the `GetDynamicLighting` call used for the custom buffer with `GetDynamicLightingSplit`. Accumulate `DiffuseLighting.rgb` into `LightingContribution.rgb` and `SpecularLighting.rgb` into `SpecularContribution.rgb`; keep alpha zero until Shadow Channel accumulation. Do not include native visibility or contact-shadow terms in either buffer.

- [ ] **Step 3: Write split outputs for Substrate pixels**

  Keep the existing shadow-neutralized `SubstrateDeferredLighting` evaluation, but accumulate `TotalDiffuseLighting` and `TotalSpecularLighting` separately. Preserve the existing Substrate scene-color path and opaque/subsurface target behavior.

- [ ] **Step 4: Apply hair scaling and initialize all outputs**

  Add and initialize the new output in the hair entry point. Apply atmospheric transmittance, light profile attenuation, coverage, and pre-exposure equally to diffuse and specular hair contributions. Keep `ShadowContribution` scalar behavior unchanged.

- [ ] **Step 5: Run focused tests and compile the affected shaders**

  Run the Task 1 focused unittest command, then run the UnrealEditor incremental build. Expected: all split contract tests pass and the shader compiler reports no errors.

- [ ] **Step 6: Commit the Deferred Light slice**

  ```text
  git add Engine/Source/Runtime/Renderer/Private/LightRendering.h Engine/Source/Runtime/Renderer/Private/LightRendering.cpp Engine/Shaders/Private/DeferredLightPixelShaders.usf Engine/Shaders/Private/DeferredLightingCommon.ush Engine/Shaders/Private/LightAccumulator.ush
  git commit -m "feat: split deferred direct diffuse and specular outputs"
  ```

### Task 4: Consume the split buffers in Deferred Toon Volume

**Files:**
- Modify: `Engine/Shaders/Private/PostProcessDeferredToonVolumes.usf`
- Modify: `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessDeferredToonVolumes.cpp`
- Modify: `Engine/Source/Runtime/Engine/Classes/Components/DeferredToonVolumeComponent.h`
- Modify: `Engine/Source/Runtime/Engine/Private/Components/DeferredToonVolumeComponent.cpp`
- Modify: `Engine/Source/Runtime/Renderer/Private/DeferredToonVolumeSceneProxy.h`

**Interfaces:**
- Consumes: `SpecularBufferTexture` from Task 2 and the split outputs from Task 3.
- Produces: `SpecularDirectLight` semantic, `DirectLightMask` based on diffuse-only `DirectLightInput`, and a dedicated specular debug view.

- [ ] **Step 1: Add semantic and debug-view contract values**

  Append `SpecularDirectLight` to the input layer table without renumbering existing semantic IDs. Append `SpecularDirectLight` (or `SpecularBuffer`) to the debug enum after 17, update CVar help and all clamp ranges, and add the editor display name.

- [ ] **Step 2: Bind and sample `SpecularBufferTexture`**

  Add the RDG texture parameter and bind the produced resource or black fallback. In `EvaluateDeferredToonInput`, map the new semantic to a point-clamped sample of `SpecularBufferTexture`. Keep `DirectLight` mapped to `LightingBufferTexture.rgb` and `ShadowChannel` mapped to `LightingBufferTexture.a`.

- [ ] **Step 3: Keep final composition stable**

  Leave the current final composition behavior unchanged except that `LightingBufferSample.rgb` now contains diffuse-only direct light. Keep specular input alive for future style mapping and expose it through the debug view; do not add a new hard-coded specular style color.

- [ ] **Step 4: Run focused tests**

  Run the Task 1 unittest command. Expected: all contract tests pass.

- [ ] **Step 5: Commit the volume-consumer slice**

  ```text
  git add Engine/Shaders/Private/PostProcessDeferredToonVolumes.usf Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessDeferredToonVolumes.cpp Engine/Source/Runtime/Engine/Classes/Components/DeferredToonVolumeComponent.h Engine/Source/Runtime/Engine/Private/Components/DeferredToonVolumeComponent.cpp Engine/Source/Runtime/Renderer/Private/DeferredToonVolumeSceneProxy.h
  git commit -m "feat: expose split direct-light inputs to toon volumes"
  ```

### Task 5: Update records and run full verification

**Files:**
- Modify: `C:/Users/ASUS/Documents/Codex/2026-10-02/qin/outputs/deferred-toon-debug-view-矩形残影记录.md`
- Modify: `tests/test_deferred_toon_lighting_split_contract.py` if verification found missing stable assertions

- [ ] **Step 1: Update the record**

  Document the new resource names, channel semantics, debug command, Roughness verification, and the fact that indirect/environment reflections are not included.

- [ ] **Step 2: Run the full test suite**

  Run:
  ```text
  python -m unittest discover -s tests -p "test_*.py" -v
  ```

  Expected: all tests pass, with the count reported in the final status.

- [ ] **Step 3: Run `git diff --check` and inspect the diff**

  Confirm no whitespace errors, stale mixed-light references, or missing `[DeferredToon]`/`[LightingShadowBuffer]` markers.

- [ ] **Step 4: Build UnrealEditor**

  Run:
  ```text
  D:\UnrealEngine-release\Engine\Build\BatchFiles\Build.bat UnrealEditor Win64 Development -NoHotReload -NoHotReloadFromIDE -NoUBTMakefiles
  ```

  Expected: `Result: Succeeded` with no shader or PSO compilation failure.

- [ ] **Step 5: Start `NewMap` and verify runtime paths**

  Start `D:\UnrealEngine-release\Engine\Binaries\Win64\UnrealEditor.exe` with `D:\UnrealEngine-release\Project\test\test.uproject` and `/Game/NewMap -game`. Verify the editor/game reaches map load, `r.Toon.LightingShadowBuffers 1` enables the resources, DebugView 14 remains stable when Roughness changes, and the new Specular debug view responds to Roughness.

- [ ] **Step 6: Commit records and verification updates**

  ```text
  git add C:/Users/ASUS/Documents/Codex/2026-10-02/qin/outputs/deferred-toon-debug-view-矩形残影记录.md tests/test_deferred_toon_lighting_split_contract.py
  git commit -m "docs: record split direct-light buffer verification"
  ```

- [ ] **Step 7: Push the completed branch**

  ```text
  git push origin main
  ```
