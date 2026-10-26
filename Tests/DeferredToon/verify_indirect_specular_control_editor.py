"""Render independent reflection composition; transient edits, no asset saves."""
from pathlib import Path
import unreal

source = Path(__file__).with_name('verify_indirect_split_editor.py').read_text(encoding='utf-8-sig')
setup = source.split('\nphases=[', 1)[0]
exec(compile(setup, 'reflection_control_setup', 'exec'), globals())
out = Path(unreal.Paths.project_saved_dir()) / ('Automation/ReflectionControlSM5' if sm5 else 'Automation/ReflectionControl')
out.mkdir(parents=True, exist_ok=True)
apply('set_enabled', True)
apply('set_indirect_diffuse_style', unreal.DeferredToonIndirectDiffuseStyle.COLOR_GRADE)
for actor in actors:
    for component in actor.get_components_by_class(unreal.SkyLightComponent):
        # Real-time sky recapture changes the input between screenshots.
        component.set_real_time_capture(False)
        component.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
        component.set_cubemap(unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
if sm5:
    ssr()
    # Stable sky/capture input for numerical composition tests, no SSR feedback.
    command('r.SSR.Quality 0')
    reflection_method(unreal.ReflectionMethod.NONE)


def override(value):
    apply('set_override_indirect_specular', value)


def intensity(value):
    apply('set_indirect_specular_intensity', value)


def other(value):
    apply('set_other_lighting_weight', value)


def diffuse(value):
    apply('set_override_indirect_diffuse', True)
    apply('set_indirect_diffuse_intensity', value)


phases = [
    ('baseline', [lambda: floor_material(0), lambda: d(0), lambda: override(False), lambda: other(1)]),
    ('neutral', [lambda: override(True), lambda: intensity(1)]),
    ('direct-only', [lambda: other(0), lambda: override(False)]),
    ('reflection-zero', [lambda: override(True), lambda: intensity(0)]),
    ('reflection-full', [lambda: intensity(1)]),
    ('reflection-half', [lambda: intensity(.5)]),
    ('reflection-diffuse-zero', [lambda: intensity(1), lambda: diffuse(0)]),
    ('matte-diffuse-zero', [lambda: floor_material(1), lambda: override(False)]),
    ('matte-diffuse-zero-reflection', [lambda: override(True)]),
    ('matte-diffuse-one', [lambda: diffuse(1), lambda: override(False)]),
    ('matte-diffuse-one-reflection', [lambda: override(True)]),
    ('matte-native', [lambda: other(1), lambda: override(False), lambda: apply('set_override_indirect_diffuse', False)]),
    ('matte-both-neutral', [lambda: diffuse(1), lambda: override(True)]),
    ('missing-native', [sky_only, lambda: sky_visibility(False), lambda: command('r.SSR.Quality 0'), lambda: reflection_method(unreal.ReflectionMethod.NONE), lambda: override(False)]),
    ('missing-override', [lambda: override(True), lambda: intensity(0)]),
]
exec(compile('\nphase = 0\n' + loop, 'reflection_control_loop', 'exec'), globals())
