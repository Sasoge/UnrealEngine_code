"""AO must survive authored indirect colors; transient D3D12 rendering only."""
from pathlib import Path
import unreal

exec(compile(Path(__file__).with_name('verify_ao_two_tone_editor.py').read_text(encoding='utf-8'),
             'verify_ao_two_tone_editor.py', 'exec'), globals())
out = Path(unreal.Paths.project_saved_dir()) / 'Automation/DeferredToonAOCompositeOrder'
out.mkdir(parents=True, exist_ok=True)
# HighResShot starts a fresh view. This suite tests composition, not history:
# keep spatial filtering but bypass temporal confirmation for every comparison.
command('r.Toon.DeferredVolumes.AOHysteresis 0')
command('ShowFlag.MotionBlur 0')
Style = unreal.DeferredToonIndirectDiffuseStyle

def lumen_again():
    sky_visibility(True)
    command('r.Lumen.DiffuseIndirect.Allow 1')
    for actor in actors:
        if isinstance(actor, unreal.PostProcessVolume):
            settings = actor.get_editor_property('settings')
            settings.set_editor_property('dynamic_global_illumination_method', unreal.DynamicGlobalIlluminationMethod.LUMEN)
            actor.set_editor_property('settings', settings)

def configure():
    command('r.AntiAliasingMethod 0')
    command('r.ScreenPercentage 100')
    command('r.EyeAdaptation.PreExposureOverride 1')
    isolate()
    mode(Mode.HARD_TWO_TONE)
    threshold(.8)
    apply('set_indirect_diffuse_style', Style.COLOR_GRADE)
    apply('set_indirect_diffuse_color', unreal.LinearColor(.3, .1, .05, 1))
    apply('set_indirect_diffuse_dark_color', unreal.LinearColor(.04, .08, .25, 1))
    apply('set_indirect_diffuse_color_replace_weight', 1)

def direct_color():
    for v in volumes:
        bands = list(v.get_editor_property('light_bands'))
        for band in bands:
            band.set_editor_property('color', unreal.LinearColor(.08, .03, .01, 1))
        v.set_light_bands(bands)

for provider, select in [('lumen', lumen_again), ('sky', sky_only)]:
    prefix = provider + '-order-'
    phases.extend([
        (prefix+'valid', [select, configure, lambda:d(53)]),
        (prefix+'mask', [lambda:d(51)]),
        (prefix+'replace-before', [lambda:strength(0), lambda:d(0)]),
        (prefix+'replace-hard', [lambda:strength(1)]),
        (prefix+'replace-half', [lambda:strength(.5)]),
        (prefix+'dark-before', [lambda:apply('set_indirect_diffuse_style', Style.TWO_TONE),
                               lambda:apply('set_indirect_diffuse_threshold',1e6), lambda:strength(0)]),
        (prefix+'dark-hard', [lambda:strength(1)]),
        (prefix+'dark-half', [lambda:strength(.5)]),
        (prefix+'band-before', [lambda:apply('set_indirect_diffuse_threshold',.1), lambda:strength(0), lambda:d(42)]),
        (prefix+'band-hard', [lambda:strength(1)]),
        (prefix+'direct-only', [direct_color, lambda:apply('set_indirect_diffuse_intensity',0), lambda:d(0)]),
        (prefix+'composite-before', [lambda:apply('set_indirect_diffuse_intensity',1),
                                    lambda:apply('set_indirect_diffuse_style',Style.COLOR_GRADE), lambda:strength(0)]),
        (prefix+'composite-hard', [lambda:strength(1)]),
    ])
phases.extend([
    ('order-missing-valid', [lambda:sky_visibility(False), lambda:d(53)]),
    ('order-missing-hard', [lambda:d(0)]),
    ('order-missing-native', [lambda:mode(Mode.NATIVE)]),
])
