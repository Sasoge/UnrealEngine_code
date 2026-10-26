"""PIE normal-frame capture: never HighResShot (which invalidates histories).

Open /Game/CartoonStreet/Levels/Level_Day with -ExecutePythonScript=<this file>.
All edits are transient; no map or asset is saved. Captures ordinary game frames.
"""
from pathlib import Path
import time
import os
import struct
import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
position, rotation = editor.get_level_viewport_camera_info()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
volumes = [c for a in actors for c in a.get_components_by_class(unreal.DeferredToonVolumeComponent)]
assert volumes
for c in volumes[1:]:
    c.set_enabled(False)
v = volumes[0]
v.set_enabled(True)
v.set_debug_view(unreal.DeferredToonDebugView.CONSOLE_GLOBAL)
v.set_indirect_occlusion_mode(unreal.DeferredToonIndirectOcclusionMode.HARD_TWO_TONE)
v.set_output_mode(unreal.DeferredToonVolumeOutputMode.DIRECT_LIGHT_GRADE)
v.set_blend_weight(1)
v.set_edge_falloff(0)
v.set_box_extent(unreal.Vector(100000, 100000, 100000))
v.set_world_scale3d(unreal.Vector(1, 1, 1))
v.set_world_location(unreal.Vector(0, 0, 0), False, False)
out = Path(unreal.Paths.project_saved_dir()) / ('Automation/DeferredToonAOTemporal' + os.environ.get('AO_TEMPORAL_RUN', ''))
out.mkdir(parents=True, exist_ok=True)
capture_camera = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.CameraActor, position, rotation)
capture_camera.set_actor_label('DeferredToonTemporalTestCamera')
if os.environ.get('AO_TEMPORAL_SKY_FIXTURE') == '1':
    sky = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.SkyLight, position)
    sky.set_actor_label('DeferredToonTemporalTestSky')
    light = sky.get_component_by_class(unreal.SkyLightComponent)
    light.set_mobility(unreal.ComponentMobility.MOVABLE)
    light.set_visibility(False)
capture_camera.camera_component.set_editor_property('constrain_aspect_ratio', False)
level.editor_set_viewport_realtime(False)
level.editor_request_begin_play()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
world = pc = camera = None
frame = phase = sample = 0
pending = None
started = time.monotonic()
busy = finished = False

def cmd(s):
    unreal.SystemLibrary.execute_console_command(world, s, pc)

def pose(offset):
    camera.set_actor_location_and_rotation(position + offset, rotation, False, True)

def hysteresis(x):
    cmd('r.Toon.DeferredVolumes.AOHysteresis ' + str(x))

def debug(x):
    cmd('r.Toon.DeferredVolumes.AOFilterDebug ' + str(x))

def sky_only():
    cmd('r.Lumen.DiffuseIndirect.Allow 0')
    for sky in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SkyLight):
        if sky.get_actor_label() == 'DeferredToonTemporalTestSky':
            light = sky.get_component_by_class(unreal.SkyLightComponent)
            light.set_visibility(True)
            light.recapture_sky()
    cmd('r.AntiAliasingMethod 0')
    cmd('r.ScreenPercentage 100')
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PostProcessVolume):
        settings = actor.get_editor_property('settings')
        settings.set_editor_property('override_dynamic_global_illumination_method', True)
        settings.set_editor_property('dynamic_global_illumination_method', unreal.DynamicGlobalIlluminationMethod.NONE)
        actor.set_editor_property('settings', settings)

phases = [
    ('static-off', 24, lambda: hysteresis(0), False),
    ('static-on', 24, lambda: hysteresis(.04), False),
    ('static-off-repeat', 24, lambda: hysteresis(0), False),
    ('accepted', 2, lambda: (hysteresis(.04), debug(4)), False),
    ('motion-off', 24, lambda: (debug(0), hysteresis(0)), True),
    ('motion-on', 24, lambda: hysteresis(.04), True),
    ('motion-accepted', 4, lambda: debug(4), True),
    ('cut-accepted', 1, lambda: (debug(4), pc.player_camera_manager.set_game_camera_cut_this_frame()), False),
    ('recovered', 2, lambda: debug(4), False),
    ('source-disabled', 2, lambda: (debug(4), cmd('r.Lumen.ScreenProbeGather.ShortRangeAO 0')), False),
    ('source-restored', 2, lambda: cmd('r.Lumen.ScreenProbeGather.ShortRangeAO 1'), False),
    ('tsr', 12, lambda: (debug(0), cmd('r.AntiAliasingMethod 4'), cmd('r.ScreenPercentage 67')), True),
    ('tsr-accepted', 2, lambda: debug(4), False),
    ('tsr-static-off', 24, lambda: (debug(0), hysteresis(0)), False),
    ('tsr-static-on', 24, lambda: hysteresis(.04), False),
    ('sky-provider', 2, lambda: (sky_only(), debug(0), cmd('r.Toon.DeferredVolumes.DebugView 54')), False),
    ('sky-accepted', 2, lambda: (cmd('r.Toon.DeferredVolumes.DebugView 51'), debug(4)), False),
    ('sky-hard', 4, lambda: debug(0), False),
]

def finish(ok, reason):
    global finished
    finished = True
    unreal.log(('AO_TEMPORAL_CAPTURE_PASS: ' if ok else 'AO_TEMPORAL_CAPTURE_FAILED: ') + reason)
    if pc is not None:
        cmd('Toon.Test.ViewportSize 0 0')
    level.editor_request_end_play()
    level.editor_set_viewport_realtime(True)
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(delta):
    global world, pc, camera, frame, phase, sample, pending, busy
    if busy or finished:
        return
    busy = True
    try:
        if time.monotonic() - started > 600:
            raise RuntimeError('PIE/capture timeout')
        if world is None:
            worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
            if not worlds:
                return
            world = worlds[0]
            pc = unreal.GameplayStatics.get_player_controller(world, 0)
            if pc is None:
                world = None
                return
            camera = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor)
                          if a.get_actor_label() == 'DeferredToonTemporalTestCamera')
            pc.set_view_target_with_blend(camera, 0)
            for s in ['r.AntiAliasingMethod 0', 'r.ScreenPercentage 100', 'Toon.Test.ViewportSize 960 720',
                      'ShowFlag.Tonemapper 0', 'ShowFlag.Bloom 0', 'ShowFlag.MotionBlur 0', 'r.EyeAdaptation.PreExposureOverride 1',
                      'r.Toon.DeferredVolumes.DebugView 51', 'r.Toon.DeferredVolumes.AOFilterDebug 0',
                      't.IdleWhenNotForeground 0', 'r.Toon.DeferredVolumes.AOFilterRadius 8']:
                cmd(s)
            unreal.log('AO_TEMPORAL_PIE: ' + str(position) + ' ' + str(rotation))
        if pending is not None:
            if not pending.exists():
                return
            # Global screenshot requests can be consumed by an editor viewport
            # during a layout redraw. Never treat those as game-history samples.
            dimensions = struct.unpack('>II', pending.read_bytes()[16:24])
            if dimensions != (960, 720):
                pending.rename(pending.with_name('discard-' + str(time.time_ns()) + '.png'))
                pending = None
            else:
                pending = None
                sample += 1
        if phase == len(phases):
            finish(True, 'normal game frames captured; assets not saved')
            return
        name, count, action, moving = phases[phase]
        if frame == 0:
            pose(unreal.Vector(0, 0, 0))
            action()
            unreal.log('AO_TEMPORAL_PHASE: ' + name)
        frame += 1
        warmup = 240 if phase == 0 else (1 if name.startswith('cut-') else 30)
        if frame < warmup:
            return
        if sample >= count:
            phase += 1
            sample = frame = 0
            return
        if moving:
            pose(unreal.Vector(sample * 2, sample, 0))
        pending = out / (name + '-' + str(sample).zfill(3) + '.png')
        if pending.exists():
            raise RuntimeError('Use a fresh output folder: ' + str(pending))
        cmd('Shot filename="' + str(pending) + '" -nosuffix')
    except Exception as exc:
        finish(False, str(exc))
    finally:
        busy = False

handle = unreal.register_slate_post_tick_callback(tick)
