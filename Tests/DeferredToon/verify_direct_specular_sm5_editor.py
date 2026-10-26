"""D3D11/SM5 smoke test of the same exports and volume shader permutations."""
from pathlib import Path
exec(compile(Path(__file__).with_name('verify_direct_specular_editor.py').read_text(encoding='utf-8'),
             'verify_direct_specular_editor.py','exec'),globals())
out = Path(unreal.Paths.project_saved_dir())/'Automation/DeferredToonDirectSpecularSM5'
out.mkdir(parents=True,exist_ok=True)
phases = [
    ('native',[lambda:d(0)]),
    ('both-source',[lambda:point.set_visibility(True),lambda:d(30)]),
    ('mask',[lambda:threshold(.1),lambda:d(31)]),
    ('styled',[lambda:style(Style.HARD_SINGLE_BAND),color,lambda:d(32)]),
    ('composite',[lambda:d(0)]),
]
