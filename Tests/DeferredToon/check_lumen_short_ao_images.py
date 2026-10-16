"""Check actual GPU extraction/composition from -ToonLumenShortAO.

Small frame-to-frame Lumen noise is allowed; fallback must remain neutral.
No engine assets are changed by the capture script.
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

for a, b in [('legacy', 'neutral'), ('bands-removed', 'bands-native'),
             ('disabled-legacy', 'disabled-override'), ('sky-legacy', 'sky-override'),
             ('integration-legacy', 'integration-override')]:
    assert error(a, b) < 2, f'Neutral/fallback/isolation failure: {a}, {b}'
for a, b in [('before-ao', 'before-ao-exposure'), ('before-ao', 'before-ao-restored'),
             ('before-ao', 'before-ao-copy'), ('visibility', 'visibility-exposure')]:
    assert error(a, b) < 3, f'Exposure/copy-path failure: {a}, {b}'

assert error('native-final', 'before-ao') > .1, 'No measurable AO to split'
assert error('neutral', 'removed') > .1, 'Strength control had no effect'
# Compare only pixels with a clear effect, excluding temporal-noise-dominated pixels.
native, half, removed = [read(n).mean(axis=2) for n in ['neutral', 'half', 'removed']]
effect = removed - native > 4
assert effect.mean() > .0005, 'No visibly restored contact regions'
assert (half[effect].mean() > native[effect].mean() and half[effect].mean() < removed[effect].mean()), 'Half strength must lie between endpoints'

for name in ['validity', 'scalar-validity', 'restored-validity']:
    assert (read(name).mean(axis=2) > 100).mean() > .2, f'Missing provider: {name}'
for name in ['disabled-validity', 'sky-validity', 'integration-validity']:
    assert (read(name).mean(axis=2) > 100).mean() < .01, f'Stale/unsupported data: {name}'
coverage = read('validity').mean(axis=2) > 100
for name in ['visibility', 'scalar-visibility']:
    values = read(name).mean(axis=2)[coverage]
    assert (values.max() - values.min()) > 15, f'AO has no spatial detail: {name}'
assert read('visibility-tsr').shape == read('visibility').shape
print('LUMEN_SHORT_AO_IMAGE_PASS')
