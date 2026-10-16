"""D3D12 offscreen integration checks; temporary edits only, never save assets."""
import time
from pathlib import Path
import unreal

sky_only_run = '-toonindirectskyonly' in unreal.SystemLibrary.get_command_line().lower()
out = Path(unreal.Paths.project_saved_dir()) / ('Automation/DeferredToonIndirectSkyOnly' if sky_only_run else 'Automation/DeferredToonIndirect')
out.mkdir(parents=True, exist_ok=True)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
world = editor.get_editor_world()
volumes = [c for a in actors for c in a.get_components_by_class(unreal.DeferredToonVolumeComponent)]
assert volumes, 'Test scene requires a DeferredToonVolume'
target = next(a for a in actors if a.get_actor_label() == 'Sphere').get_actor_location()
camera = target + unreal.Vector(-400, -550, 400)
editor.set_level_viewport_camera_info(camera, unreal.MathLibrary.find_look_at_rotation(camera, target))

# Explicitly exercise Lumen even when the user's saved post-process volume
# overrides the project default. Log the original values; do not save them.
for actor in actors:
    if isinstance(actor, unreal.PostProcessVolume):
        settings = actor.get_editor_property('settings')
        unreal.log('INDIRECT_TEST_ORIGINAL_GI: ' + str(settings.get_editor_property('dynamic_global_illumination_method'))
                   + ' override=' + str(settings.get_editor_property('override_dynamic_global_illumination_method')))
        settings.set_editor_property('override_dynamic_global_illumination_method', True)
        settings.set_editor_property('dynamic_global_illumination_method', unreal.DynamicGlobalIlluminationMethod.LUMEN)
        actor.set_editor_property('settings', settings)

def command(text):
    unreal.SystemLibrary.execute_console_command(world, text)

for cmd in ['r.Toon.LightingShadowBuffers 1', 'r.Toon.DeferredVolumes.DebugView 0',
            'r.EyeAdaptationQuality 0', 'r.EyeAdaptation.PreExposureOverride 1',
            'r.DynamicGlobalIlluminationMethod 1', 'r.Lumen.DiffuseIndirect.Allow 1',
            'r.Lumen.ScreenTracingSource 0']:
    command(cmd)

# A transient constant white curve is not needed: the white texture supplies
# the binary shadow endpoint; all band overrides are white as well.
for v in volumes:
    v.set_world_location(target, False, False)
    v.set_world_scale3d(unreal.Vector(1, 1, 1))
    v.set_box_extent(unreal.Vector(2000, 2000, 2000))
    v.set_edge_falloff(0)
    v.set_blend_weight(1)
    v.set_output_mode(unreal.DeferredToonVolumeOutputMode.DIRECT_LIGHT_GRADE)
    v.set_base_color_weight(0)
    v.set_lit_color_replace_weight(0)
    v.set_shadow_color_replace_weight(0)
    v.set_lighting_color_curve(None)
    v.set_shadow_texture(unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
    v.set_shadow_texture_weight(1)
    v.set_other_lighting_weight(1)
    bands = []
    for minimum, value in [(0, 0), (.1, .3), (.4, .6), (1, 1)]:
        band = unreal.DeferredToonLightBand()
        band.set_editor_property('min_response', minimum)
        band.set_editor_property('value', value)
        band.set_editor_property('override_color', True)
        band.set_editor_property('color', unreal.LinearColor(1, 1, 1, 1))
        band.set_editor_property('shadow_mapping_weight', 0)
        bands.append(band)
    v.set_light_bands(bands)
    v.set_use_light_bands(True)
    v.set_override_indirect_diffuse(False)
    v.set_indirect_diffuse_intensity(1)
    v.set_indirect_diffuse_color(unreal.LinearColor(1, 1, 1, 1))
    v.set_indirect_diffuse_color_replace_weight(0)

def debug(value):
    for v in volumes:
        v.set_debug_view(value)

def apply(method, value):
    for v in volumes:
        getattr(v, method)(value)

def output():
    debug(unreal.DeferredToonDebugView.NATIVE_COMPOSITE)

def neutral():
    apply('set_override_indirect_diffuse', True)
    apply('set_indirect_diffuse_intensity', 1)
    apply('set_indirect_diffuse_color', unreal.LinearColor(1, 1, 1, 1))
    apply('set_indirect_diffuse_color_replace_weight', 0)
    apply('set_other_lighting_weight', 1)

def sky_visibility(value):
    for actor in actors:
        for component in actor.get_components_by_class(unreal.SkyLightComponent):
            component.set_visibility(value)

def sky_only():
    command('r.Lumen.DiffuseIndirect.Allow 0')
    # Explicitly turn off GI so this covers the user's actual non-Lumen setup.
    for actor in actors:
        if isinstance(actor, unreal.PostProcessVolume):
            settings = actor.get_editor_property('settings')
            settings.set_editor_property('override_dynamic_global_illumination_method', True)
            settings.set_editor_property('dynamic_global_illumination_method', unreal.DynamicGlobalIlluminationMethod.NONE)
            actor.set_editor_property('settings', settings)

phases = [
    ('legacy', [output]),
    ('neutral', [neutral]),
    ('indirect', [lambda: debug(unreal.DeferredToonDebugView.INDIRECT_DIFFUSE)]),
    ('indirect-copy', [lambda: command('r.DiffuseIndirectForceCopyPass 1')]),
    ('indirect-preexposure', [lambda: command('r.EyeAdaptation.PreExposureOverride 4')]),
    ('coverage', [lambda: debug(unreal.DeferredToonDebugView.INDIRECT_DIFFUSE_COVERAGE)]),
    ('bands-before', [lambda: command('r.EyeAdaptation.PreExposureOverride 1'),
                      lambda: command('r.DiffuseIndirectForceCopyPass 0'),
                      lambda: debug(unreal.DeferredToonDebugView.LIGHT_BAND_VALUE)]),
    ('bands-tint', [lambda: apply('set_indirect_diffuse_color', unreal.LinearColor(.1, .3, 1, 1))]),
    ('tint', [output]),
    ('zero', [lambda: apply('set_indirect_diffuse_intensity', 0)]),
    ('replacement', [neutral, lambda: apply('set_indirect_diffuse_color', unreal.LinearColor(.04, .02, .2, 1)),
                     lambda: apply('set_indirect_diffuse_color_replace_weight', 1)]),
    ('indirect-tsr', [neutral, lambda: debug(unreal.DeferredToonDebugView.INDIRECT_DIFFUSE),
                     lambda: command('r.EyeAdaptation.PreExposureOverride 1'),
                     lambda: command('r.DiffuseIndirectForceCopyPass 0'),
                     lambda: command('r.AntiAliasingMethod 4'), lambda: command('r.ScreenPercentage 67')]),
    ('sky-indirect', [lambda: command('r.ScreenPercentage 100'), sky_only]),
    ('sky-preexposure', [lambda: command('r.EyeAdaptation.PreExposureOverride 4')]),
    ('sky-restored', [lambda: command('r.EyeAdaptation.PreExposureOverride 1')]),
    ('sky-coverage', [lambda: debug(unreal.DeferredToonDebugView.INDIRECT_DIFFUSE_COVERAGE)]),
    ('sky-legacy', [output, lambda: apply('set_override_indirect_diffuse', False)]),
    ('sky-neutral', [neutral]),
    ('sky-zero', [lambda: apply('set_indirect_diffuse_intensity', 0)]),
    ('unsupported-coverage', [lambda: sky_visibility(False),
                              lambda: debug(unreal.DeferredToonDebugView.INDIRECT_DIFFUSE_COVERAGE)]),
    ('unsupported-legacy', [output, lambda: apply('set_override_indirect_diffuse', False)]),
    ('unsupported-override', [lambda: apply('set_override_indirect_diffuse', True),
                               lambda: apply('set_indirect_diffuse_intensity', 0)]),
    ('restored', [lambda: sky_visibility(True), neutral, output]),
]
if sky_only_run:
    phases = [
        ('sky-legacy', [sky_only, output]),
        ('sky-neutral', [neutral]),
        ('sky-indirect', [lambda: debug(unreal.DeferredToonDebugView.INDIRECT_DIFFUSE)]),
        ('sky-coverage', [lambda: debug(unreal.DeferredToonDebugView.INDIRECT_DIFFUSE_COVERAGE)]),
        ('sky-zero', [output, lambda: apply('set_indirect_diffuse_intensity', 0)]),
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
    if busy or finished:
        return
    busy = True
    try:
        if time.monotonic() - started > 600:
            raise RuntimeError('capture timeout')
        if capture is not None and not capture.is_task_done():
            return
        if phase == len(phases):
            finished = True
            unreal.log('INDIRECT_DIFFUSE_GPU_PASS: all phases rendered; no assets saved')
            unreal.unregister_slate_post_tick_callback(handle)
            unreal.EditorPythonScripting.set_keep_python_script_alive(False)
            return
        name, actions = phases[phase]
        if frame == 0:
            for action in actions:
                action()
            unreal.log('INDIRECT_DIFFUSE_PHASE: ' + name)
        frame += 1
        # Initial warmup allows Lumen surface cache and sky capture to converge.
        capture_frame = 600 if phase == 0 else 160
        if frame == capture_frame:
            capture = unreal.AutomationLibrary.take_high_res_screenshot(960, 540, str(out / (name + '.png')))
            assert capture.is_valid_task()
        if frame >= capture_frame + 10:
            phase += 1
            frame = 0
    except Exception as exc:
        finished = True
        unreal.log_error('INDIRECT_DIFFUSE_GPU_FAILED: ' + str(exc))
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    finally:
        busy = False

handle = unreal.register_slate_post_tick_callback(tick)
