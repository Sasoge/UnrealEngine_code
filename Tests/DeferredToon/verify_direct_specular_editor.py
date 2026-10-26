"""Transient direct-highlight fixture; no level/material assets are saved."""
from pathlib import Path
import unreal

shared = Path(__file__).with_name('verify_indirect_diffuse_editor.py').read_text(encoding='utf-8-sig')
setup, loop = shared.split('\nphase = 0\n', 1)
exec(compile(setup, 'specular_setup', 'exec'), globals())
out = Path(unreal.Paths.project_saved_dir())/'Automation/DeferredToonDirectSpecular'
out.mkdir(parents=True, exist_ok=True)
Style = unreal.DeferredToonDirectSpecularStyle
sun_actor = next(a for a in actors if a.get_components_by_class(unreal.DirectionalLightComponent))
sun = sun_actor.get_components_by_class(unreal.DirectionalLightComponent)[0]
point_actor = next(a for a in actors if a.get_components_by_class(unreal.PointLightComponent))
point = point_actor.get_components_by_class(unreal.PointLightComponent)[0]
meshes = [c for a in actors if a.get_actor_label() != 'SM_SkySphere'
          for c in a.get_components_by_class(unreal.StaticMeshComponent)]
sky_only()
for actor in actors:
    for light in actor.get_components_by_class(unreal.LightComponent):
        light.set_visibility(light == sun or light == point)
for component in meshes + [sun, point]:
    component.set_shadow_channel_mask(True, False, False)
    component.set_editor_property('lighting_channels', unreal.LightingChannels(True, False, False))
sun_actor.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera, target), False)
sun.set_intensity(5)
sun.set_light_color(unreal.LinearColor(1,1,1,1))
point.set_intensity(2)
point.set_light_color(unreal.LinearColor(1,1,1,1))
point.set_editor_property('use_inverse_squared_falloff', False)
point.set_attenuation_radius(1500)
point_actor.set_actor_location(target+unreal.Vector(200,-150,250),False,False)
# Debug views bypass final output gamma; make production captures linear too.
for cmd in ['gamma 1', 'r.AntiAliasingMethod 0', 'r.ScreenPercentage 100', 'ShowFlag.Tonemapper 0',
            'ShowFlag.Bloom 0', 'ShowFlag.MotionBlur 0', 'r.SSR.Quality 0']:
    command(cmd)
materials = []
for roughness in [.18,.65]:
    mat = unreal.new_object(unreal.Material)
    for prop, value in [(unreal.MaterialProperty.MP_BASE_COLOR,.25),
                        (unreal.MaterialProperty.MP_ROUGHNESS,roughness),
                        (unreal.MaterialProperty.MP_SPECULAR,1.0),
                        (unreal.MaterialProperty.MP_METALLIC,0.0)]:
        expr = unreal.MaterialEditingLibrary.create_material_expression(mat,unreal.MaterialExpressionConstant)
        expr.set_editor_property('r',value)
        unreal.MaterialEditingLibrary.connect_material_property(expr,'',prop)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    materials.append(mat)

def material(index):
    for mesh in meshes:
        for slot in range(mesh.get_num_materials()):
            mesh.set_material(slot,materials[index])

def d(value):
    debug(unreal.DeferredToonDebugView.CONSOLE_GLOBAL)
    command('r.Toon.DeferredVolumes.DebugView '+str(value))

def style(value): apply('set_direct_specular_style',value)
def threshold(value): apply('set_direct_specular_threshold',value)
def intensity(value): apply('set_direct_specular_intensity',value)
def color(): apply('set_direct_specular_color',unreal.LinearColor(.3,.08,.015,1))

def isolate_direct_diffuse(value):
    # A black palette removes graded diffuse, leaving an independently controlled highlight.
    apply('set_lit_color_replace_weight',1 if value else 0)
    apply('set_shadow_color_replace_weight',1 if value else 0)
    for volume in volumes:
        bands = list(volume.get_editor_property('light_bands'))
        for band in bands:
            band.set_editor_property('color',unreal.LinearColor(0,0,0,1) if value else unreal.LinearColor(1,1,1,1))
        volume.set_light_bands(bands)

material(0)
apply('set_indirect_occlusion_mode',unreal.DeferredToonIndirectOcclusionMode.NATIVE)
apply('set_override_indirect_diffuse',False)
style(Style.NATIVE)
apply('set_direct_specular_color_mode',unreal.DeferredToonDirectSpecularColorMode.FIXED_COLOR)
point.set_visibility(False)
phases = [
    ('baseline', [lambda:apply('set_enabled',False),lambda:d(0)]),
    ('native', [lambda:apply('set_enabled',True)]),
    ('box', [lambda:d(1)]),
    ('sun-source', [lambda:d(30)]),
    ('point-source', [lambda:sun.set_visibility(False),lambda:point.set_visibility(True)]),
    ('both-source', [lambda:sun.set_visibility(True)]),
    ('mask-low', [lambda:threshold(.02),lambda:d(31)]),
    ('mask-mid', [lambda:threshold(.1)]),
    ('mask-high', [lambda:threshold(1)]),
    ('mask-zero-threshold', [lambda:threshold(0)]),
    ('mask-no-light', [lambda:sun.set_visibility(False),lambda:point.set_visibility(False)]),
    ('styled-no-light', [lambda:style(Style.HARD_SINGLE_BAND),color,lambda:d(32)]),
    ('mask-rough', [lambda:sun.set_visibility(True),lambda:point.set_visibility(True),lambda:material(1),lambda:threshold(.1),lambda:d(31)]),
    ('mask-restored', [lambda:material(0)]),
    ('mask-authored', [color,lambda:intensity(2)]),
    ('styled', [lambda:intensity(1),lambda:d(32)]),
    ('styled-half', [lambda:intensity(.5)]),
    ('styled-zero', [lambda:intensity(0)]),
    ('mask-preexposure', [lambda:command('r.EyeAdaptation.PreExposureOverride 4'),lambda:d(31)]),
    ('isolated', [lambda:command('r.EyeAdaptation.PreExposureOverride 1'),lambda:intensity(1),
                  lambda:isolate_direct_diffuse(True),lambda:apply('set_other_lighting_weight',0),lambda:d(0)]),
    ('isolated-zero', [lambda:intensity(0)]),
    ('diffuse-no-specular', [lambda:isolate_direct_diffuse(False)]),
    ('diffuse-with-specular', [lambda:intensity(1)]),
    ('native-restored', [lambda:style(Style.NATIVE),lambda:apply('set_other_lighting_weight',1)]),
    ('baseline-restored', [lambda:apply('set_enabled',False)]),
    ('channel-mismatch', [lambda:apply('set_enabled',True),lambda:point.set_visibility(False),
                          lambda:sun.set_editor_property('lighting_channels',unreal.LightingChannels(False,True,False)),lambda:d(30)]),
    ('channel-match', [lambda:sun.set_editor_property('lighting_channels',unreal.LightingChannels(True,False,False))]),
    ('shadow-ignored', [lambda:sun.set_shadow_channel_mask(False,True,False)]),
    ('shadow-matched', [lambda:sun.set_shadow_channel_mask(True,False,False)]),
    ('buffers-off-native', [lambda:command('r.Toon.LightingShadowBuffers 0'),lambda:d(0)]),
    ('buffers-off-hard', [lambda:style(Style.HARD_SINGLE_BAND)]),
    ('mask-tsr', [lambda:command('r.Toon.LightingShadowBuffers 1'),lambda:command('r.AntiAliasingMethod 4'),lambda:command('r.ScreenPercentage 67'),lambda:d(31)]),
]
exec(compile('\nphase = 0\n'+loop,'specular_loop','exec'),globals())
