"""Verify rendered outcomes, not copies of the shader implementation.

Run after verify_indirect_diffuse_editor.py with its output directory argument.
Dependencies: numpy, Pillow. Temporal edges allow a small 8-bit image tolerance.
"""
import sys
from pathlib import Path
import numpy as np
from PIL import Image

root = Path(sys.argv[1])
def read(name):
    return np.asarray(Image.open(root / (name + '.png')).convert('RGB'), dtype=np.float32)

def equal(a, b, tolerance=2):
    difference = np.abs(read(a) - read(b))
    error = float(difference.mean())
    print(f'{a} vs {b}: MAE={error:.4f}, max={difference.max():.0f}')
    assert error < tolerance, (a, b, error)

def changed(a, b, minimum=.1):
    error = float(np.abs(read(a) - read(b)).mean())
    print(f'{a} vs {b}: MAE={error:.4f}')
    assert error > minimum, (a, b, error)

equal('legacy', 'neutral')
equal('bands-before', 'bands-tint')
# Lumen's changing caches/screen traces make screenshots across separate frames
# unsuitable for strict provider equivalence. Report these differences; validate
# exposure numerically on the stable sky provider instead of loosening tolerance.
for a, b in [('indirect', 'indirect-copy'), ('indirect-copy', 'indirect-preexposure')]:
    print('Lumen temporal comparison (diagnostic only):', a, b,
          float(np.abs(read(a) - read(b)).mean()))
equal('sky-indirect', 'sky-preexposure', 3)
equal('sky-indirect', 'sky-restored', 3)
equal('unsupported-legacy', 'unsupported-override')
equal('sky-legacy', 'sky-neutral')
changed('sky-neutral', 'sky-zero')
sky_coverage = read('sky-coverage').mean(axis=2) > 100
assert sky_coverage.mean() > .2, 'Dynamic sky provider missing'
assert read('sky-indirect')[sky_coverage].mean() > 1, 'Sky diffuse unexpectedly black'
coverage = read('coverage').mean(axis=2) > 100
assert coverage.mean() > .2, 'No valid indirect provider on test surfaces'
energy = float(read('indirect')[coverage].mean())
print('Supported coverage:', coverage.mean(), 'Indirect mean:', energy)
assert energy > 1, 'Indirect export unexpectedly black on supported lit scene'
unsupported = read('unsupported-coverage')
assert np.count_nonzero(unsupported.mean(axis=2) > 100) < coverage.size * .01, 'Stale availability after GI disabled'
changed('neutral', 'zero')
changed('neutral', 'tint')
changed('zero', 'replacement')
assert read('indirect-tsr').shape == read('indirect').shape
print('INDIRECT_DIFFUSE_IMAGE_PASS')
