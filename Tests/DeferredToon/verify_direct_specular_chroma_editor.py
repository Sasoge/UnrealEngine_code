"""Transient material/light chroma integration fixture; no asset saves."""
from pathlib import Path
exec(compile(Path(__file__).with_name('verify_direct_specular_editor.py').read_text(encoding='utf-8'),
             'verify_direct_specular_editor.py','exec'),globals())
out = Path(unreal.Paths.project_saved_dir())/('Automation/DeferredToonDirectSpecularChromaSM5' if '-specularchromasm5' in unreal.SystemLibrary.get_command_line().lower() else 'Automation/DeferredToonDirectSpecularChroma')
out.mkdir(parents=True,exist_ok=True)
ColorMode=unreal.DeferredToonDirectSpecularColorMode
apply('set_direct_specular_color_mode',ColorMode.NATIVE_CHROMA)
apply('set_direct_specular_color',unreal.LinearColor(1,1,1,1))
style(Style.HARD_SINGLE_BAND)
threshold(.02)
intensity(.12)
# New transient material graphs: a colored dielectric must NOT tint its specular
# by BaseColor, while a conductor's colored F0 must survive the mapping.
chroma_materials=[]
for base,metallic in [((.8,.03,.01),0),((.01,.03,.8),0),((.8,.4,.08),1),((.08,.2,.8),1),((.5,.5,.5),1)]:
    mat=unreal.new_object(unreal.Material)
    base_expr=unreal.MaterialEditingLibrary.create_material_expression(mat,unreal.MaterialExpressionConstant3Vector)
    base_expr.set_editor_property('constant',unreal.LinearColor(*base,1))
    unreal.MaterialEditingLibrary.connect_material_property(base_expr,'',unreal.MaterialProperty.MP_BASE_COLOR)
    for prop,value in [(unreal.MaterialProperty.MP_ROUGHNESS,.3),(unreal.MaterialProperty.MP_SPECULAR,1),
                       (unreal.MaterialProperty.MP_METALLIC,metallic)]:
        expr=unreal.MaterialEditingLibrary.create_material_expression(mat,unreal.MaterialExpressionConstant)
        expr.set_editor_property('r',value)
        unreal.MaterialEditingLibrary.connect_material_property(expr,'',prop)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    chroma_materials.append(mat)
def chroma_material(index):
    for mesh in meshes:
        for slot in range(mesh.get_num_materials()):
            mesh.set_material(slot,chroma_materials[index])
def tint(value): apply('set_direct_specular_color',unreal.LinearColor(*value,1))
def case(index,sun_color,point_color=None):
    chroma_material(index)
    sun.set_light_color(unreal.LinearColor(*sun_color,1))
    point.set_visibility(point_color is not None)
    if point_color is not None: point.set_light_color(unreal.LinearColor(*point_color,1))
white=(1,1,1)
red=(1,.15,.03)
blue=(.03,.15,1)
phases=[
    ('box',[lambda:d(1)]),
    ('dielectric-red',[lambda:case(0,white),lambda:d(18)]),
    ('dielectric-blue',[lambda:case(1,white)]),
]
for name,index,sun_color,point_color in [('colored-light',1,red,None),('gold',2,white,None),
                                       ('blue-metal',3,white,None),('mixed',4,red,blue)]:
    phases += [
        (name+'-source',[lambda i=index,s=sun_color,p=point_color:case(i,s,p),lambda:d(18)]),
        (name+'-mask',[lambda:d(39)]),
        (name+'-styled',[lambda:d(40)]),
    ]
phases += [
    ('mixed-tint',[lambda:tint((.5,1,.8))]),
    ('mixed-mask-tint',[lambda:d(39)]),
    ('mixed-half',[lambda:tint(white),lambda:intensity(.06),lambda:d(40)]),
    ('mixed-preexposure',[lambda:intensity(.12),lambda:command('r.EyeAdaptation.PreExposureOverride 4')]),
    ('mixed-isolated',[lambda:command('r.EyeAdaptation.PreExposureOverride 1'),
                       lambda:isolate_direct_diffuse(True),lambda:apply('set_other_lighting_weight',0),lambda:d(0)]),
    ('mixed-zero',[lambda:intensity(0)]),
    ('no-light',[lambda:intensity(.12),lambda:sun.set_visibility(False),lambda:point.set_visibility(False),lambda:threshold(0),lambda:d(40)]),
    ('fixed',[lambda:sun.set_visibility(True),lambda:point.set_visibility(True),lambda:threshold(.02),
              lambda:apply('set_direct_specular_color_mode',ColorMode.FIXED_COLOR),lambda:tint((.3,.08,.015)),lambda:intensity(1)]),
    ('fixed-mask',[lambda:d(39)]),
]
