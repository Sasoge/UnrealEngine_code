"""Offscreen integration checks. All edits are transient; no level/asset is saved."""
import unreal
import time
from pathlib import Path

out = Path(unreal.Paths.project_saved_dir()) / 'Automation/DeferredToonBands'
out.mkdir(parents=True, exist_ok=True)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
world = editor.get_editor_world()
# Refresh any volume-only refinements made while global light permutations compiled.
unreal.SystemLibrary.execute_console_command(world, 'RecompileShaders Changed')
volumes = [c for a in actors for c in a.get_components_by_class(unreal.DeferredToonVolumeComponent)]
sun = next(c for a in actors for c in a.get_components_by_class(unreal.DirectionalLightComponent))
point_actor = next(a for a in actors if a.get_components_by_class(unreal.PointLightComponent))
point = point_actor.get_components_by_class(unreal.PointLightComponent)[0]
meshes = [c for a in actors if a.get_actor_label() != 'SM_SkySphere' for c in a.get_components_by_class(unreal.StaticMeshComponent)]
target = next(a for a in actors if a.get_actor_label() == 'Sphere').get_actor_location()
camera = target + unreal.Vector(-400, -550, 400)
editor.set_level_viewport_camera_info(camera, unreal.MathLibrary.find_look_at_rotation(camera, target))
for a in actors:
    for c in a.get_components_by_class(unreal.LightComponent):
        c.set_visibility(c == sun or c == point)
for c in meshes + [sun, point]:
    c.set_shadow_channel_mask(True, False, False)
sun.set_intensity(10.0)
point.set_intensity(1.0)
point.set_editor_property('use_inverse_squared_falloff', False)
point.set_attenuation_radius(1500.0)
point.set_editor_property('lighting_channels', unreal.LightingChannels(True, False, False))
point_actor.set_actor_location(target + unreal.Vector(-220, 50, 150), False, False)
for cmd in ['r.Toon.LightingShadowBuffers 1', 'r.Toon.DeferredVolumes.DebugView 0', 'r.EyeAdaptationQuality 0']:
    unreal.SystemLibrary.execute_console_command(world, cmd)
for v in volumes:
    v.set_world_location(target, False, False)
    v.set_world_scale3d(unreal.Vector(1.0, 1.0, 1.0))
    v.set_box_extent(unreal.Vector(2000.0, 2000.0, 2000.0))
    v.set_edge_falloff(0.0)
    v.set_effective_light_threshold(0.1)
    v.set_base_color_weight(0.0)
    v.set_output_mode(unreal.DeferredToonVolumeOutputMode.TOON_RECOMPOSE)

# Reference materials are created in the transient package, never in /Game.
materials = []
for value in (1.0, 0.005):
    mat = unreal.new_object(unreal.Material)
    expr = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
    expr.set_editor_property('constant', unreal.LinearColor(value, value, value, 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(expr, '', unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    materials.append(mat)
saved_materials = [(m, [m.get_material(i) for i in range(m.get_num_materials())]) for m in meshes]
def set_material(index):
    for m, slots in saved_materials:
        for i in range(len(slots)):
            m.set_material(i, materials[index])
def debug(value):
    for v in volumes: v.set_debug_view(value)
def mode(value):
    for v in volumes: v.set_output_mode(value)
def point_visibility(value): point.set_visibility(value)
def native():
    debug(unreal.DeferredToonDebugView.NATIVE_SHADOW)
def final():
    debug(unreal.DeferredToonDebugView.TOON_BINARY_SHADOW_MASK)
def response():
    debug(unreal.DeferredToonDebugView.EFFECTIVE_LIGHT_RESPONSE)
def color():
    debug(unreal.DeferredToonDebugView.EFFECTIVE_DIRECT_LIGHT)
def grade():
    debug(unreal.DeferredToonDebugView.NATIVE_COMPOSITE)
    mode(unreal.DeferredToonVolumeOutputMode.DIRECT_LIGHT_GRADE)
    for v in volumes:
        v.set_lit_color_replace_weight(0.0)
        v.set_shadow_color_replace_weight(0.0)
        v.set_other_lighting_weight(0.0)
        # No curve means neutral tint for lit areas; dark uses a white texture below.
        v.set_lighting_color_curve(None)
        v.set_shadow_texture(unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
        v.set_shadow_texture_weight(1.0)
def texture():
    for v in volumes:
        v.set_shadow_color_replace_weight(1.0)
        v.set_shadow_texture(unreal.load_asset('/Engine/EngineResources/DefaultTexture'))
        v.set_shadow_texture_tiling(unreal.Vector2D(16.0,16.0))
def mismatch():
    sun.set_shadow_channel_mask(False, True, False)
    point.set_shadow_channel_mask(False, True, False)
def exposure(value):
    unreal.SystemLibrary.execute_console_command(world, 'r.EyeAdaptation.PreExposureOverride ' + str(value))

def show_levels():
    debug(unreal.DeferredToonDebugView.LIGHT_BAND_VALUE)

def two():
    for v in volumes: v.use_two_band_compatibility()
    show_levels()

def four():
    for v in volumes: v.apply_four_light_bands()
    show_levels()

def colors():
    debug(unreal.DeferredToonDebugView.NATIVE_COMPOSITE)
    mode(unreal.DeferredToonVolumeOutputMode.DIRECT_LIGHT_GRADE)
    palette = [(0.02,0.04,0.12), (0.1,0.15,0.45), (0.45,0.3,0.18), (1.0,0.8,0.5)]
    for v in volumes:
        bands=[]
        for minimum, value, rgb in zip([0,.1,.4,1], [0,.3,.6,1], palette):
            b=unreal.DeferredToonLightBand()
            b.set_editor_property('min_response', minimum)
            b.set_editor_property('value', value)
            b.set_editor_property('override_color', True)
            b.set_editor_property('color', unreal.LinearColor(*rgb,1))
            b.set_editor_property('shadow_mapping_weight', 0)
            bands.append(b)
        v.set_light_bands(bands)
        v.set_lit_color_replace_weight(1)
        v.set_shadow_color_replace_weight(1)
        v.set_other_lighting_weight(0)
        v.set_base_color_weight(0)

def reorder():
    for v in volumes: v.set_light_bands(list(reversed(v.get_editor_property('light_bands'))))

def disable_lights():
    sun.set_visibility(False)
    point.set_visibility(False)

def restore_lights():
    sun.set_visibility(True)
    point.set_visibility(True)

def eight():
    for v in volumes:
        bands=[]
        for i in range(8):
            b=unreal.DeferredToonLightBand()
            b.set_editor_property('min_response', [0,.05,.1,.2,.4,.8,1.6,3.2][i])
            b.set_editor_property('value', i/7)
            bands.append(b)
        v.set_light_bands(bands)
    show_levels()

def constant_level(value):
    # Display-reference frames let the image checker account for tonemapping,
    # bloom and vignette without mistaking them for extra shader levels.
    for v in volumes:
        bands=[]
        for minimum in (0,1):
            b=unreal.DeferredToonLightBand()
            b.set_editor_property('min_response', minimum)
            b.set_editor_property('value', value)
            bands.append(b)
        v.set_light_bands(bands)
        v.set_use_light_bands(True)
    show_levels()

phases=[
    ('two-before', [lambda:set_material(0),two]),
    ('four-white', [four]),
    ('four-black', [lambda:set_material(1)]),
    ('four-exposure', [lambda:exposure(4)]),
    ('four-color-grade', [lambda:exposure(0),colors]),
    ('four-color-recompose', [lambda:mode(unreal.DeferredToonVolumeOutputMode.TOON_RECOMPOSE)]),
    ('four-reordered', [reorder,show_levels]),
    ('four-no-light', [disable_lights]),
    ('eight', [restore_lights,eight]),
    ('two-restored', [lambda:set_material(0),two]),
    ('reference-0', [lambda:constant_level(0)]),
    ('reference-03', [lambda:constant_level(.3)]),
    ('reference-06', [lambda:constant_level(.6)]),
    ('reference-1', [lambda:constant_level(1)]),
]
phase = 0
frame = 0
capture = None
busy = False
finished = False
started = time.monotonic()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
def tick(delta):
    global phase, frame, capture, busy, finished
    if busy or finished: return
    busy = True
    try:
        if time.monotonic() - started > 300: raise RuntimeError('capture timeout')
        if capture is not None and not capture.is_task_done(): return
        if phase == len(phases):
            finished = True
            unreal.log('LIGHT_BANDS_GPU_PASS: all phases rendered; no assets saved')
            unreal.unregister_slate_post_tick_callback(handle)
            unreal.EditorPythonScripting.set_keep_python_script_alive(False)
            return
        name, actions = phases[phase]
        if frame == 0:
            for action in actions: action()
            unreal.log('LIGHT_BANDS_PHASE: ' + name)
        frame += 1
        if frame == 40:
            capture = unreal.AutomationLibrary.take_high_res_screenshot(960, 540, str(out / ('bands-' + name + '.png')))
            assert capture.is_valid_task()
        if frame >= 50:
            phase += 1
            frame = 0
    except Exception as exc:
        finished = True
        unreal.log_error('LIGHT_BANDS_GPU_FAILED: ' + str(exc))
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    finally:
        busy = False
handle = unreal.register_slate_post_tick_callback(tick)
