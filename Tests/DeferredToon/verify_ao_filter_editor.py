"""Compare unfiltered/filtered hard AO, static noise and translated camera poses."""
from pathlib import Path
import unreal
if '-toonaofilterdefaultsonly' in unreal.SystemLibrary.get_command_line().lower():
    assert unreal.SystemLibrary.get_console_variable_float_value('r.Toon.DeferredVolumes.AOFilterConservatism') == .5
shared=Path(__file__).with_name('verify_indirect_diffuse_editor.py').read_text(encoding='utf-8-sig')
setup,loop=shared.split('\nphase = 0\n',1)
exec(compile(setup,'ao_filter_setup','exec'),globals())
out=Path(unreal.Paths.project_saved_dir())/'Automation/DeferredToonAOFlatWide'
out.mkdir(parents=True,exist_ok=True)
for actor in actors:
    for c in actor.get_components_by_class(unreal.SkyLightComponent):
        c.set_real_time_capture(False)
        c.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
        c.set_cubemap(unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
for cmd in ['r.AntiAliasingMethod 0','r.ScreenPercentage 100','ShowFlag.Tonemapper 0','ShowFlag.Bloom 0',
            'r.Toon.DeferredVolumes.AOFilterConservatism 0.5','r.Lumen.ScreenProbeGather.ShortRangeAO 1','r.Lumen.ScreenProbeGather.ShortRangeAO.ApplyDuringIntegration 0']:
    command(cmd)
apply('set_indirect_occlusion_mode',unreal.DeferredToonIndirectOcclusionMode.HARD_TWO_TONE)
apply('set_indirect_occlusion_threshold',.8)
def d(value):
    debug(unreal.DeferredToonDebugView.CONSOLE_GLOBAL)
    command('r.Toon.DeferredVolumes.DebugView '+str(value))
def radius(value): command('r.Toon.DeferredVolumes.AOFilterRadius '+str(value))
def pose(index):
    offsets=[unreal.Vector(-400,-550,400),unreal.Vector(-370,-570,400),unreal.Vector(-310,-450,340)]
    location=target+offsets[index]
    editor.set_level_viewport_camera_info(location,unreal.MathLibrary.find_look_at_rotation(location,target))
phases=[]
for p in range(3):
    for r in [0,8]:
        for frame_index in range(5 if p==0 else 1):
            phases.append((f'lumen-p{p}-r{r}-{frame_index}',[lambda p=p:pose(p),lambda r=r:radius(r),lambda:d(38)]))
phases += [
    ('lumen-raw-off',[lambda:pose(0),lambda:radius(0),lambda:d(33)]),
    ('lumen-raw-on',[lambda:radius(8)]),
    ('lumen-valid',[lambda:d(35)]),
    ('lumen-no-shrink',[lambda:command('r.Toon.DeferredVolumes.AOFilterConservatism 0'),lambda:d(38)]),
    ('lumen-shrink',[lambda:command('r.Toon.DeferredVolumes.AOFilterConservatism 0.5')]),
    ('lumen-tsr',[lambda:command('r.AntiAliasingMethod 4'),lambda:command('r.ScreenPercentage 67'),lambda:d(38)]),
    ('lumen-return',[lambda:command('r.AntiAliasingMethod 0'),lambda:command('r.ScreenPercentage 100')]),
]
for r in [0,8]:
    for f in range(2): phases.append((f'sky-r{r}-{f}',[sky_only,lambda r=r:radius(r),lambda:d(38)]))
phases += [
    ('sky-raw-off',[lambda:radius(0),lambda:d(33)]),
    ('sky-raw-on',[lambda:radius(8)]),
    ('sky-valid',[lambda:d(35)]),
    ('missing-mask',[lambda:sky_visibility(False),lambda:d(38)]),
    ('missing-valid',[lambda:d(35)]),
]
if '-toonaofilterdefaultsonly' in unreal.SystemLibrary.get_command_line().lower():
    assert unreal.SystemLibrary.get_console_variable_float_value('r.Toon.DeferredVolumes.AOFilterRadius') == 8
    assert unreal.SystemLibrary.get_console_variable_float_value('r.Toon.DeferredVolumes.AOFilterConservatism') == .5
    phases = [('default-mask', [lambda:pose(0),lambda:d(38)]), ('default-output', [lambda:d(0)])]
    unreal.log('AO_FILTER_DEFAULTS_PASS: radius=8, conservatism=0.5')
exec(compile('\nphase = 0\n'+loop,'ao_filter_loop','exec'),globals())
