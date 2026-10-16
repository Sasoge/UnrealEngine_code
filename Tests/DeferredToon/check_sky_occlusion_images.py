"""Check GPU outcomes from verify_indirect_diffuse_editor.py -ToonSkyOcclusionSplit.

This checks actual extraction, fallback and grading effects on the saved test map.
It does not infer AO by dividing final radiance or compare Lumen across frames.
"""
from pathlib import Path
import sys
import numpy as np
from PIL import Image

root = Path(sys.argv[1])

def read(name):
    return np.asarray(Image.open(root / (name + '.png')).convert('RGB'), dtype=np.float32)

def error(a, b):
    value = float(np.abs(read(a) - read(b)).mean())
    print(f'{a} vs {b}: MAE={value:.4f}')
    return value

assert error('legacy', 'neutral') < 2, 'Neutral split changed native composite'
assert error('native-final', 'native-final-exposure') < 3, 'Native sky fixture changed with exposure'
assert error('native-final', 'native-final-restored') < 3, 'Native sky fixture did not settle'
assert error('before-ao', 'before-ao-exposure') < 3, 'Pre-exposure changed before-AO lighting'
assert error('before-ao', 'before-ao-restored') < 3, 'Before-AO lighting did not recover'
assert error('visibility', 'visibility-exposure') < 3, 'Pre-exposure changed AO mask'
assert error('ao-removed', 'ao-color-disabled') < 2, 'Zero strength still applied AO color'
assert error('bands-colored', 'bands-native') < 2, 'AO grading affected direct bands'
assert error('missing-legacy', 'missing-override') < 2, 'Missing sky did not fall back'
assert error('native-final', 'before-ao') > .1, 'Fixture has no measurable sky occlusion'
assert error('neutral', 'ao-removed') > .1, 'Removing AO had no visible effect'
assert error('neutral', 'ao-colored') > .1, 'AO replacement had no visible effect'

coverage = read('validity').mean(axis=2) > 100
assert coverage.mean() > .2, 'Sky split provider did not cover test surfaces'
visibility = read('visibility').mean(axis=2)
assert np.percentile(visibility[coverage], 5) < np.percentile(visibility[coverage], 95) - 5, 'AO mask has no spatial variation'
for name in ['missing-validity', 'lumen-validity']:
    assert (read(name).mean(axis=2) > 100).mean() < .01, f'Stale sky split pixels: {name}'
assert (read('lumen-indirect-validity').mean(axis=2) > 100).mean() > .2, 'Lumen diffuse regressed'
assert (read('sky-restored').mean(axis=2) > 100).mean() > .2, 'Sky split failed after GI switch'
assert read('visibility-tsr').shape == read('visibility').shape
print('SKY_OCCLUSION_IMAGE_PASS')
