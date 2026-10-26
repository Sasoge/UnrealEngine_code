"""Offscreen UE integration: hard indirect bands. Uses transient scene edits only."""
from pathlib import Path
import unreal

shared = Path(__file__).with_name('verify_indirect_diffuse_editor.py').read_text(encoding='utf-8-sig')
setup, loop = shared.split('\nphase = 0\n', 1)
exec(compile(setup, 'indirect_test_setup', 'exec'), globals())
out = Path(unreal.Paths.project_saved_dir()) / 'Automation/DeferredToonIndirectTwoTone'
out.mkdir(parents=True, exist_ok=True)
Style = unreal.DeferredToonIndirectDiffuseStyle
Mode = unreal.DeferredToonIndirectOcclusionMode
for actor in actors:
    for c in actor.get_components_by_class(unreal.SkyLightComponent):
        c.set_real_time_capture(False)
        c.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
        c.set_cubemap(unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
for cmd in ['r.AntiAliasingMethod 0', 'r.ScreenPercentage 100', 'ShowFlag.Tonemapper 0',
            'ShowFlag.Bloom 0', 'r.Lumen.ScreenProbeGather.ShortRangeAO 1',
            'r.Lumen.ScreenProbeGather.ShortRangeAO.ApplyDuringIntegration 0']:
    command(cmd)
apply('set_indirect_occlusion_mode', Mode.NATIVE)
apply('set_indirect_diffuse_style', Style.COLOR_GRADE)

def d(value):
    debug(unreal.DeferredToonDebugView.CONSOLE_GLOBAL)
    command('r.Toon.DeferredVolumes.DebugView ' + str(value))

def threshold(value):
    apply('set_indirect_diffuse_threshold', value)

def isolate():
    # Remove the direct and residual layers to measure only the new two-tone output.
    apply('set_override_indirect_diffuse', True)
    apply('set_indirect_diffuse_style', Style.TWO_TONE)
    apply('set_indirect_diffuse_color', unreal.LinearColor(.35, .02, .01, 1))
    apply('set_indirect_diffuse_dark_color', unreal.LinearColor(.01, .02, .35, 1))
    apply('set_other_lighting_weight', 0)
    apply('set_base_color_weight', 0)
    apply('set_lit_color_replace_weight', 1)
    apply('set_shadow_color_replace_weight', 1)
    for v in volumes:
        bands = list(v.get_editor_property('light_bands'))
        for band in bands:
            band.set_editor_property('color', unreal.LinearColor(0, 0, 0, 1))
        v.set_light_bands(bands)

def sky_legacy_tint():
    apply('set_override_sky_occlusion', True)
    apply('set_sky_occlusion_strength', 1)
    apply('set_sky_occlusion_color', unreal.LinearColor(0, 1, 0, 1))
    apply('set_sky_occlusion_color_replace_weight', 1)

phases = [
    ('native', [lambda: d(0)]),
    ('grade-neutral', [neutral]),
    ('lumen-coverage', [lambda: d(41)]),
    ('box-coverage', [lambda: d(1)]),
    ('lumen-mask-low', [isolate, lambda: threshold(.02), lambda: d(42)]),
    ('lumen-mask-mid', [lambda: threshold(.1)]),
    ('lumen-mask-high', [lambda: threshold(.5)]),
    ('lumen-mask-zero', [lambda: threshold(0)]),
    ('lumen-lit-output', [lambda: d(0)]),
    ('lumen-mask-dark', [lambda: threshold(1e6), lambda: d(42)]),
    ('lumen-dark-output', [lambda: d(0)]),
    ('lumen-mixed-output', [lambda: threshold(.1)]),
    ('sky-coverage', [sky_only, lambda: d(41)]),
    ('sky-mask-low', [lambda: threshold(.02), lambda: d(42)]),
    ('sky-mask-mid', [lambda: threshold(.1)]),
    ('sky-mask-high', [lambda: threshold(.5)]),
    ('sky-mixed-output', [lambda: threshold(.1), lambda: d(0)]),
    ('sky-material-ignored', [lambda: apply('set_base_color_weight', 1), lambda: apply('set_indirect_diffuse_color_replace_weight', 1)]),
    ('sky-legacy-tint-ignored', [sky_legacy_tint]),
    ('sky-mask-exposure-base', [lambda: d(42)]),
    ('sky-mask-exposure', [lambda: command('r.EyeAdaptation.PreExposureOverride 4')]),
    ('sky-ao-removed', [lambda: command('r.EyeAdaptation.PreExposureOverride 1'), lambda: apply('set_indirect_occlusion_mode', Mode.UNIFIED), lambda: apply('set_indirect_occlusion_strength', 0)]),
    ('missing', [lambda: sky_visibility(False), lambda: d(42)]),
    ('missing-native', [lambda: d(0), lambda: apply('set_override_indirect_diffuse', False)]),
    ('missing-two-tone', [lambda: apply('set_override_indirect_diffuse', True)]),
    ('sky-mask-tsr', [lambda: sky_visibility(True), lambda: apply('set_indirect_occlusion_mode', Mode.NATIVE), lambda: command('r.AntiAliasingMethod 4'), lambda: command('r.ScreenPercentage 67'), lambda: d(42)]),
]
exec(compile('\nphase = 0\n' + loop, 'indirect_test_loop', 'exec'), globals())
