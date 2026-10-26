"""Independent native diffuse/specular exports; transient edits, no asset saves."""
from pathlib import Path
import unreal
source=Path(__file__).with_name('verify_indirect_diffuse_editor.py').read_text(encoding='utf-8-sig')
setup,loop=source.split('\nphase = 0\n',1)
exec(compile(setup,'split_setup','exec'),globals())
sm5='-indirectsplitsm5' in unreal.SystemLibrary.get_command_line().lower()
out=Path(unreal.Paths.project_saved_dir())/('Automation/IndirectSplitSM5' if sm5 else 'Automation/IndirectSplit')
out.mkdir(parents=True,exist_ok=True)
for cmd in ['gamma 1','r.SSR.Quality 0','r.Lumen.Reflections.Allow 1','r.AntiAliasingMethod 0','r.ScreenPercentage 100','ShowFlag.Tonemapper 0','ShowFlag.Bloom 0']:
 command(cmd)
apply('set_direct_specular_style',unreal.DeferredToonDirectSpecularStyle.NATIVE)
apply('set_indirect_occlusion_mode',unreal.DeferredToonIndirectOcclusionMode.NATIVE)
apply('set_other_lighting_weight',1)
def d(value):
 debug(unreal.DeferredToonDebugView.CONSOLE_GLOBAL)
 command('r.Toon.DeferredVolumes.DebugView '+str(value))
def reflection_method(value):
 for a in actors:
  if isinstance(a,unreal.PostProcessVolume):
   settings=a.get_editor_property('settings')
   settings.set_editor_property('override_reflection_method',True)
   settings.set_editor_property('reflection_method',value)
   a.set_editor_property('settings',settings)
reflection_method(unreal.ReflectionMethod.LUMEN)
materials=[]
for metallic,roughness,model in [(1,.02,unreal.MaterialShadingModel.MSM_DEFAULT_LIT),(0,.7,unreal.MaterialShadingModel.MSM_DEFAULT_LIT),(1,.02,unreal.MaterialShadingModel.MSM_TOON)]:
 mat=unreal.new_object(unreal.Material)
 mat.set_editor_property('shading_model',model)
 for prop,value in [(unreal.MaterialProperty.MP_BASE_COLOR,.8),(unreal.MaterialProperty.MP_METALLIC,metallic),(unreal.MaterialProperty.MP_ROUGHNESS,roughness)]:
  expr=unreal.MaterialEditingLibrary.create_material_expression(mat,unreal.MaterialExpressionConstant)
  expr.set_editor_property('r',value)
  unreal.MaterialEditingLibrary.connect_material_property(expr,'',prop)
 unreal.MaterialEditingLibrary.recompile_material(mat)
 materials.append(mat)
floors=[c for a in actors if a.get_actor_label() in ['Floor','Plane','Plane2'] for c in a.get_components_by_class(unreal.StaticMeshComponent)]
assert floors
emissive_materials=[]
for a in actors:
 if a.get_actor_label() in ['Sphere','Cube']:
  mat=unreal.new_object(unreal.Material)
  expr=unreal.MaterialEditingLibrary.create_material_expression(mat,unreal.MaterialExpressionConstant3Vector)
  expr.set_editor_property('constant',unreal.LinearColor(2,.1,.1,1) if a.get_actor_label()=='Sphere' else unreal.LinearColor(.1,2,.1,1))
  unreal.MaterialEditingLibrary.connect_material_property(expr,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
  unreal.MaterialEditingLibrary.recompile_material(mat)
  emissive_materials.append(mat)
  for c in a.get_components_by_class(unreal.StaticMeshComponent): c.set_material(0,mat)
def floor_material(index):
 for c in floors:
  for slot in range(c.get_num_materials()): c.set_material(slot,materials[index])
def ssr():
 sky_only()
 reflection_method(unreal.ReflectionMethod.SCREEN_SPACE)
 command('r.SSR.Quality 3')
phases=[
 ('baseline',[lambda:floor_material(0),lambda:apply('set_enabled',False),lambda:d(0)]),
 ('neutral',[lambda:apply('set_enabled',True)]),
 ('metal-diffuse',[lambda:d(40)]),
 ('metal-specular',[lambda:d(80)]),
 ('metal-valid',[lambda:d(81)]),
 ('metal-specular-off',[lambda:command('r.Lumen.Reflections.Allow 0'),lambda:d(80)]),
 ('metal-diffuse-off',[lambda:d(40)]),
 ('metal-specular-restored',[lambda:command('r.Lumen.Reflections.Allow 1'),lambda:d(80)]),
 ('metal-specular-copy',[lambda:command('r.DiffuseIndirectForceCopyPass 1')]),
 ('matte-diffuse',[lambda:command('r.DiffuseIndirectForceCopyPass 0'),lambda:floor_material(1),lambda:d(40)]),
 ('matte-specular',[lambda:d(80)]),
 ('toon-specular',[lambda:floor_material(2)]),
 ('toon-valid',[lambda:d(81)]),
 ('sky-diffuse',[ssr,lambda:floor_material(1),lambda:d(40)]),
 ('sky-specular',[lambda:floor_material(0),lambda:d(80)]),
 ('sky-valid',[lambda:d(81)]),
 ('no-sky-specular',[lambda:sky_visibility(False),lambda:d(80)]),
 ('no-sky-diffuse',[lambda:d(40)]),
 ('no-sky-diffuse-valid',[lambda:d(41)]),
 ('no-sky-specular-valid',[lambda:d(81)]),
 ('no-reflections-valid',[lambda:reflection_method(unreal.ReflectionMethod.NONE),lambda:d(81)]),
 ('no-reflections',[lambda:d(80)]),
]
if sm5:
 phases=phases[13:]
exec(compile('\nphase = 0\n'+loop,'split_loop','exec'),globals())
