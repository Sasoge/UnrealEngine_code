"""Ordinary PIE frame ablation for dark-entry confirmation and thin-fringe rejection."""
from pathlib import Path
exec(compile(Path(__file__).with_name('verify_ao_temporal_editor.py').read_text(encoding='utf-8'),
             'verify_ao_temporal_editor.py', 'exec'), globals())
position = unreal.Vector(-443.674251, -4919.226602, 236.955125)
rotation = unreal.Rotator(pitch=-7.399901, yaw=22538.950320, roll=0)

def confirmation(frames, support):
    cmd('r.Toon.DeferredVolumes.AODarkConfirmationFrames ' + str(frames))
    cmd('r.Toon.DeferredVolumes.AOMinDarkSupport ' + str(support))

phases[-3:-3] = [
    ('confirm-baseline', 24, lambda: confirmation(1,0), False),
    ('confirm-only', 24, lambda: confirmation(3,0), False),
    ('confirm-both', 24, lambda: confirmation(3,5), False),
    ('confirm-repeat', 24, lambda: confirmation(1,0), False),
    ('confirm-motion-base', 24, lambda: confirmation(1,0), True),
    ('confirm-motion-on', 24, lambda: confirmation(3,5), True),
    ('fast-turn-base', 24, lambda: (confirmation(1,0), cmd('r.AntiAliasingMethod 0'), cmd('r.ScreenPercentage 100')), True),
    ('fast-turn-confirm', 24, lambda: confirmation(3,5), True),
    ('cut-hard', 1, lambda: (confirmation(3,5), pc.player_camera_manager.set_game_camera_cut_this_frame()), False),
    ('confirm-recovered', 6, lambda: None, False),
]
original_pose = pose
def pose(offset):
    if phase < len(phases) and phases[phase][0].startswith('fast-turn'):
        camera.set_actor_location_and_rotation(position, unreal.Rotator(
            pitch=rotation.pitch, yaw=rotation.yaw + (35 if 6 <= sample < 12 else 0), roll=0), False, True)
    else:
        original_pose(offset)
