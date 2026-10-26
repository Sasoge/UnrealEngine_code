"""Controlled point-light occluder: verifies effective specular and receiver channels."""
from pathlib import Path
exec(compile(Path(__file__).with_name('verify_direct_specular_editor.py').read_text(encoding='utf-8'),
             'verify_direct_specular_editor.py','exec'),globals())
out = Path(unreal.Paths.project_saved_dir())/'Automation/DeferredToonDirectSpecularShadow'
out.mkdir(parents=True,exist_ok=True)
sun.set_visibility(False)
point.set_visibility(True)
point.set_cast_shadows(True)
blocker = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(
    unreal.StaticMeshActor,point_actor.get_actor_location())
blocker.set_actor_label('TransientSpecularShadowFixture')
blocker_mesh = blocker.get_component_by_class(unreal.StaticMeshComponent)
blocker_mesh.set_mobility(unreal.ComponentMobility.MOVABLE)
blocker_mesh.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
blocker_mesh.set_render_in_main_pass(False)
blocker_mesh.set_render_in_depth_pass(False)
blocker_mesh.set_editor_property('cast_shadow_as_two_sided',True)
blocker_mesh.set_shadow_channel_mask(True,False,False)
phases = [
    ('open',[lambda:blocker_mesh.set_cast_shadow(False),lambda:d(30)]),
    ('occluded',[lambda:blocker_mesh.set_cast_shadow(True)]),
    ('receiver-unmatched',[lambda:point.set_shadow_channel_mask(False,True,False)]),
    ('restored',[lambda:point.set_shadow_channel_mask(True,False,False),lambda:blocker_mesh.set_cast_shadow(False)]),
]
