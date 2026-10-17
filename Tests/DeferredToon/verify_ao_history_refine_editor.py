"""Reuse ordinary PIE captures; add scalar-history ablation and rejection reasons.

Run on the city map with a fresh AO_TEMPORAL_RUN suffix. No assets are saved.
"""
from pathlib import Path
exec(compile(Path(__file__).with_name('verify_ao_temporal_editor.py').read_text(encoding='utf-8'),
             'verify_ao_temporal_editor.py', 'exec'), globals())

# Match the previous revision's recorded city camera for a comparable footprint.
position = unreal.Vector(-443.674251, -4919.226602, 236.955125)
rotation = unreal.Rotator(pitch=-7.399901, yaw=22538.950320, roll=0)

def scalar(weight):
    cmd('r.Toon.DeferredVolumes.AOTemporalWeight ' + str(weight))

extra = [
    ('reasons-tsr', 8, lambda: debug(5), False),
    ('scalar-off', 24, lambda: (debug(0), scalar(0)), False),
    ('scalar-on', 24, lambda: scalar(.85), False),
    ('scalar-off-repeat', 24, lambda: scalar(0), False),
    ('reasons-noaa', 8, lambda: (scalar(.85), debug(5), cmd('r.AntiAliasingMethod 0'), cmd('r.ScreenPercentage 100')), False),
    ('fast-turn-off', 24, lambda: (debug(0), hysteresis(0)), True),
    ('fast-turn-on', 24, lambda: hysteresis(.04), True),
]
phases[-3:-3] = extra

original_pose = pose
def pose(offset):
    if phase < len(phases) and phases[phase][0].startswith('fast-turn'):
        # Abrupt turn and return, deliberately without a camera-cut reset.
        yaw_offset = 35 if 6 <= sample < 12 else 0
        camera.set_actor_location_and_rotation(position, unreal.Rotator(
            pitch=rotation.pitch, yaw=rotation.yaw + yaw_offset, roll=0), False, True)
    else:
        original_pose(offset)
