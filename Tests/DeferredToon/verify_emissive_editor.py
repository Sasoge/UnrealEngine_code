"""Material emissive export/composition. No scene or material assets are saved."""
from pathlib import Path
import unreal

source = Path(__file__).with_name('verify_indirect_diffuse_editor.py').read_text(encoding='utf-8-sig')
setup, loop = source.split('\nphase = 0\n', 1)
exec(compile(setup, 'emissive_setup', 'exec'), globals())
sm5 = '-emissivesm5' in unreal.SystemLibrary.get_command_line().lower()
static_lighting = '-emissivestaticlighting' in unreal.SystemLibrary.get_command_line().lower()
out = Path(unreal.Paths.project_saved_dir()) / ('Automation/EmissiveSM5Static' if static_lighting else 'Automation/EmissiveSM5' if sm5 else 'Automation/Emissive')
out.mkdir(parents=True, exist_ok=True)
apply('set_enabled', True)
apply('set_override_emissive', False)
apply('set_override_indirect_specular', False)
apply('set_indirect_occlusion_mode', unreal.DeferredToonIndirectOcclusionMode.NATIVE)
apply('set_direct_specular_style', unreal.DeferredToonDirectSpecularStyle.NATIVE)
for cmd in ['gamma 1', 'r.AntiAliasingMethod 0', 'r.ScreenPercentage 100',
            'ShowFlag.Tonemapper 0', 'ShowFlag.Bloom 0', 'r.SSR.Quality 0',
            'r.AllowStaticLighting', 'r.VelocityOutputPass']:
    command(cmd)
for actor in actors:
    for component in actor.get_components_by_class(unreal.SkyLightComponent):
        component.set_real_time_capture(False)
        component.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
        component.set_cubemap(unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
if sm5:
    sky_only()


def d(index):
    debug(unreal.DeferredToonDebugView.CONSOLE_GLOBAL)
    command('r.Toon.DeferredVolumes.DebugView ' + str(index))


materials = []
def material(color, unlit=False, mask=None):
    mat = unreal.new_object(unreal.Material)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT if unlit else unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    emission = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
    emission.set_editor_property('constant', unreal.LinearColor(*color, 1))
    unreal.MaterialEditingLibrary.connect_material_property(emission, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if mask is not None:
        mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
        expr = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant)
        expr.set_editor_property('r', mask)
        unreal.MaterialEditingLibrary.connect_material_property(expr, '', unreal.MaterialProperty.MP_OPACITY_MASK)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    materials.append(mat)
    return mat


sphere = next(c for a in actors if a.get_actor_label() == 'Sphere' for c in a.get_components_by_class(unreal.StaticMeshComponent))
cube = next(c for a in actors if a.get_actor_label() == 'Cube' for c in a.get_components_by_class(unreal.StaticMeshComponent))
lit = material((.2, .06, .02))
unlit = material((.03, .3, .06), unlit=True)
hdr = material((2, .6, .2), unlit=True)
masked_on = material((.2, .06, .02), mask=1)
masked_off = material((.2, .06, .02), mask=0)
water = material((.4, .1, 0))
water_output = unreal.MaterialEditingLibrary.create_material_expression(water, unreal.MaterialExpressionSingleLayerWaterMaterialOutput)
water_scattering = unreal.MaterialEditingLibrary.create_material_expression(water, unreal.MaterialExpressionConstant3Vector)
assert unreal.MaterialEditingLibrary.connect_material_expressions(water_scattering, '', water_output, '')
water.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
unreal.MaterialEditingLibrary.recompile_material(water)
sphere.set_material(0, lit)
cube.set_material(0, unlit)


def override(value):
    apply('set_override_emissive', value)


def intensity(value):
    apply('set_emissive_intensity', value)


def lights(value):
    for actor in actors:
        for cls in [unreal.DirectionalLightComponent, unreal.PointLightComponent, unreal.SkyLightComponent]:
            for component in actor.get_components_by_class(cls):
                component.set_visibility(value)


nanite_asset = '/Game/__CodexEmissiveValidation/Cube'
created_nanite = False
original_mesh = cube.get_editor_property('static_mesh')
def nanite():
    global created_nanite
    assert not unreal.EditorAssetLibrary.does_asset_exist(nanite_asset), 'Refuse to overwrite existing validation asset'
    mesh = unreal.EditorAssetLibrary.duplicate_asset(original_mesh.get_path_name(), nanite_asset)
    assert mesh
    created_nanite = True
    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    settings = subsystem.get_nanite_settings(mesh)
    settings.set_editor_property('enabled', True)
    subsystem.set_nanite_settings(mesh, settings, True)
    cube.set_static_mesh(mesh)
    cube.set_material(0, unlit)


def cleanup_emissive():
    if created_nanite:
        cube.set_static_mesh(original_mesh)
        assert unreal.EditorAssetLibrary.delete_asset(nanite_asset)


phases = [
    ('native', [lambda: d(0)]),
    ('neutral', [lambda: override(True), lambda: intensity(1)]),
    ('raw', [lambda: d(43)]),
    ('valid', [lambda: d(44)]),
    ('raw-no-lights', [lambda: lights(False), lambda: d(43)]),
    ('raw-restored', [lambda: lights(True)]),
    ('other-zero', [lambda: d(0), lambda: apply('set_other_lighting_weight', 0)]),
    ('emission-zero', [lambda: intensity(0)]),
    ('emission-half', [lambda: intensity(.5)]),
    ('emission-full', [lambda: intensity(1)]),
    ('masked-on', [lambda: sphere.set_material(0, masked_on), lambda: d(43)]),
    ('masked-off', [lambda: sphere.set_material(0, masked_off)]),
    ('hdr-quarter-exposure', [lambda: sphere.set_material(0, lit), lambda: cube.set_material(0, hdr), lambda: command('r.EyeAdaptation.PreExposureOverride .25')]),
    ('hdr-quarter-output', [lambda: d(0), lambda: intensity(.25)]),
    ('exposure-restored', [lambda: d(43), lambda: intensity(1), lambda: cube.set_material(0, unlit), lambda: command('r.EyeAdaptation.PreExposureOverride 1')]),
]
if not sm5:
    phases += [('nanite-raw', [nanite]), ('nanite-valid', [lambda: d(44)]),
               ('nanite-output', [lambda: d(0)]), ('nanite-zero', [lambda: intensity(0)])]
phases += [('water-valid', [lambda: cube.set_static_mesh(original_mesh), lambda: cube.set_material(0, water), lambda: d(44)]),
           ('water-raw', [lambda: d(43)])]
if '-emissivewateronly' in unreal.SystemLibrary.get_command_line().lower():
    phases = phases[-2:]
loop = loop.replace('unreal.unregister_slate_post_tick_callback(handle)',
                    'cleanup_emissive()\n            unreal.unregister_slate_post_tick_callback(handle)', 1)
loop = loop.replace('INDIRECT_DIFFUSE_GPU_', 'EMISSIVE_GPU_').replace('INDIRECT_DIFFUSE_PHASE:', 'EMISSIVE_PHASE:')
loop = loop.replace("        unreal.log_error('EMISSIVE_GPU_FAILED: ' + str(exc))", "        unreal.log_error('EMISSIVE_GPU_FAILED: ' + str(exc))\n        cleanup_emissive()")
exec(compile('\nphase = 0\n' + loop, 'emissive_loop', 'exec'), globals())
