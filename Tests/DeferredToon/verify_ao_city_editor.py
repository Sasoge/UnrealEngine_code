"""Actual city regression. Only transient edits, no package saves."""
from pathlib import Path
import time
import unreal
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world=editor.get_editor_world()
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
volumes=[c for a in actors for c in a.get_components_by_class(unreal.DeferredToonVolumeComponent)]
assert volumes,'Saved city must contain the user toon volume'
for c in volumes[1:]: c.set_enabled(False)
v=volumes[0]
unreal.log('CITY_AO_ORIGINAL: threshold='+str(v.get_editor_property('indirect_occlusion_threshold')))
v.set_enabled(True)
v.set_debug_view(unreal.DeferredToonDebugView.CONSOLE_GLOBAL)
v.set_indirect_occlusion_mode(unreal.DeferredToonIndirectOcclusionMode.HARD_TWO_TONE)
v.set_output_mode(unreal.DeferredToonVolumeOutputMode.DIRECT_LIGHT_GRADE)
v.set_blend_weight(1)
v.set_edge_falloff(0)
v.set_box_extent(unreal.Vector(100000,100000,100000))
v.set_world_scale3d(unreal.Vector(1,1,1))
v.set_world_location(unreal.Vector(0,0,0),False,False)
position,rotation=editor.get_level_viewport_camera_info()
unreal.log('CITY_AO_CAMERA: '+str(position)+' '+str(rotation))
out=Path(unreal.Paths.project_saved_dir())/'Automation/DeferredToonAOCity'
out.mkdir(parents=True,exist_ok=True)
def command(s): unreal.SystemLibrary.execute_console_command(world,s)
def d(n): command('r.Toon.DeferredVolumes.DebugView '+str(n))
def filter_radius(n): command('r.Toon.DeferredVolumes.AOFilterRadius '+str(n))
def cleanup(n): command('r.Toon.DeferredVolumes.AOCleanup '+str(n))
def filter_debug(n): command('r.Toon.DeferredVolumes.AOFilterDebug '+str(n))
def pose(n):
    offsets=[unreal.Vector(0,0,0),unreal.Vector(100,0,0),unreal.Vector(200,100,0)]
    editor.set_level_viewport_camera_info(position+offsets[n],rotation)
for s in ['r.AntiAliasingMethod 0','r.ScreenPercentage 100','ShowFlag.Tonemapper 0','ShowFlag.Bloom 0',
          'r.EyeAdaptation.PreExposureOverride 1','r.Toon.DeferredVolumes.AOFilterConservatism 0.5',
          'r.Toon.DeferredVolumes.AOFilterDebug 0']:
    command(s)
phases=[]
for method,r,c in [('raw',0,0),('filtered',8,0),('clean',8,1)]:
    for i in range(5):
        phases.append((f'p0-{method}-{i}',[lambda r=r:filter_radius(r),lambda c=c:cleanup(c),lambda:d(51)]))
for p in [1,2]:
    for method,r,c in [('raw',0,0),('clean',8,1)]:
        phases.append((f'p{p}-{method}',[lambda p=p:pose(p),lambda r=r:filter_radius(r),lambda c=c:cleanup(c),lambda:d(51)]))
phases += [
    ('profile-clean',[lambda:pose(0),lambda:command('r.ProfileGPU.ShowUI 0'),lambda:command('r.ProfileGPU.Pattern *DeferredToon*')]),
    ('continuous',[lambda:pose(0),lambda:filter_debug(1)]),
    ('support',[lambda:filter_debug(2)]),
    ('geometry',[lambda:filter_debug(3)]),
    ('provider',[lambda:filter_debug(0),lambda:d(54)]),
    ('raw-visibility',[lambda:d(50)]),
    ('tsr',[lambda:command('r.AntiAliasingMethod 4'),lambda:command('r.ScreenPercentage 67'),lambda:d(51)]),
    ('restored',[lambda:command('r.AntiAliasingMethod 0'),lambda:command('r.ScreenPercentage 100')]),
]
shared=Path(__file__).with_name('verify_indirect_diffuse_editor.py').read_text(encoding='utf-8-sig')
loop='\nphase = 0\n'+shared.split('\nphase = 0\n',1)[1]
loop=loop.replace('600 if phase == 0 else 160','360 if phase == 0 else 90').replace('960, 540','960, 720')
loop=loop.replace('if frame == capture_frame:\n',"if frame == capture_frame:\n            if name == 'profile-clean': command('profilegpu')\n")
exec(compile(loop,'city_ao_loop','exec'),globals())
