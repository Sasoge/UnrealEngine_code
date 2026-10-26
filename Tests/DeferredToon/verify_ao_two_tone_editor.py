"""AO-only quantization: verify native GI survives outside the selected AO region."""
from pathlib import Path
import unreal
shared=Path(__file__).with_name('verify_indirect_diffuse_editor.py').read_text(encoding='utf-8-sig')
setup,loop=shared.split('\nphase = 0\n',1)
exec(compile(setup,'ao_test_setup','exec'),globals())
out=Path(unreal.Paths.project_saved_dir())/'Automation/DeferredToonAOTwoTone'
out.mkdir(parents=True,exist_ok=True)
Mode=unreal.DeferredToonIndirectOcclusionMode
for actor in actors:
    for c in actor.get_components_by_class(unreal.SkyLightComponent):
        c.set_real_time_capture(False)
        c.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
        c.set_cubemap(unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
for cmd in ['r.AntiAliasingMethod 0','r.ScreenPercentage 100','ShowFlag.Tonemapper 0',
            'ShowFlag.Bloom 0','r.Lumen.ScreenProbeGather.ShortRangeAO 1',
            'r.Lumen.ScreenProbeGather.ShortRangeAO.ApplyDuringIntegration 0']:
    command(cmd)
apply('set_indirect_diffuse_style',unreal.DeferredToonIndirectDiffuseStyle.COLOR_GRADE)
def mode(value): apply('set_indirect_occlusion_mode',value)
def strength(value): apply('set_indirect_occlusion_strength',value)
def threshold(value): apply('set_indirect_occlusion_threshold',value)
def d(value):
    debug(unreal.DeferredToonDebugView.CONSOLE_GLOBAL)
    command('r.Toon.DeferredVolumes.DebugView '+str(value))
def isolate():
    # Identity indirect grading only extracts the layer for measurement. Whole-GI
    # two-tone is deliberately off. Runtime AO also works with this override off.
    neutral()
    apply('set_other_lighting_weight',0)
    apply('set_base_color_weight',0)
    apply('set_lit_color_replace_weight',1)
    apply('set_shadow_color_replace_weight',1)
    for v in volumes:
        bands=list(v.get_editor_property('light_bands'))
        for b in bands: b.set_editor_property('color',unreal.LinearColor(0,0,0,1))
        v.set_light_bands(bands)

phases=[
    ('standalone-native',[lambda: mode(Mode.NATIVE),lambda:d(0)]),
    ('standalone-unified',[lambda:mode(Mode.UNIFIED),lambda:strength(1)]),
    ('standalone-before',[lambda:strength(0)]),
    ('standalone-hard-zero',[lambda:mode(Mode.HARD_TWO_TONE)]),
    ('lumen-source',[lambda:d(54)]),
    ('lumen-raw',[lambda:d(50)]),
    ('lumen-mask-low',[lambda:threshold(.5),lambda:d(51)]),
    ('lumen-mask-mid',[lambda:threshold(.8)]),
    ('lumen-mask-high',[lambda:threshold(.95)]),
    ('box',[lambda:d(1)]),
    ('lumen-valid',[lambda:d(53)]),
    ('lumen-before',[isolate,lambda:threshold(.8),lambda:strength(0),lambda:d(0)]),
    ('lumen-hard',[lambda:strength(1)]),
    ('lumen-half',[lambda:strength(.5)]),
    ('sky-source',[sky_only,lambda:d(54)]),
    ('sky-valid',[lambda:d(53)]),
    ('sky-raw',[lambda:d(50)]),
    ('sky-mask-low',[lambda:threshold(.5),lambda:d(51)]),
    ('sky-mask-mid',[lambda:threshold(.8)]),
    ('sky-mask-high',[lambda:threshold(.95)]),
    ('sky-before',[lambda:threshold(.8),lambda:strength(0),lambda:d(0)]),
    ('sky-hard',[lambda:strength(1)]),
    ('sky-half',[lambda:strength(.5)]),
    ('sky-mask-exposure-base',[lambda:d(51)]),
    ('sky-mask-exposure',[lambda:command('r.EyeAdaptation.PreExposureOverride 4')]),
    ('missing-valid',[lambda:command('r.EyeAdaptation.PreExposureOverride 1'),lambda:sky_visibility(False),lambda:d(53)]),
    ('missing-mask',[lambda:d(51)]),
    ('missing-native',[lambda:mode(Mode.NATIVE),lambda:d(0)]),
    ('missing-hard',[lambda:mode(Mode.HARD_TWO_TONE)]),
    ('sky-mask-tsr',[lambda:sky_visibility(True),lambda:command('r.AntiAliasingMethod 4'),lambda:command('r.ScreenPercentage 67'),lambda:d(51)]),
]
exec(compile('\nphase = 0\n'+loop,'ao_test_loop','exec'),globals())
