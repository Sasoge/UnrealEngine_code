"""Verify automatic AO selection against raw providers and legacy composition."""
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
for a,b in [('lumen-native','lumen-neutral'), ('sky-native','sky-neutral'),
            ('lumen-summary','lumen-raw'), ('sky-summary','sky-raw'),
            ('lumen-before-summary','lumen-before-raw'), ('sky-before-summary','sky-before-raw'),
            ('lumen-legacy','lumen-unified-match'), ('sky-legacy','sky-unified-match'),
            ('sky-weight-native','sky-weight-neutral'), ('missing-native','missing-unified')]:
    assert error(a,b) < 2, (a,b)
assert error('lumen-summary','lumen-summary-exposure') < 3
for prefix in ['lumen','sky']:
    assert error(prefix+'-neutral',prefix+'-removed') > .1, 'No standalone AO effect'
for name in ['lumen-source','lumen-restored-source']:
    img = read(name)
    assert ((img[:,:,1] > img[:,:,0]+40) & (img[:,:,2] > img[:,:,0]+40)).mean() > .2, 'Not Lumen source'
img = read('sky-source')
assert ((img[:,:,0] > img[:,:,2]+40) & (img[:,:,1] > img[:,:,2]+20)).mean() > .2, 'Not sky source'
for name in ['missing-source','missing-validity']:
    assert (read(name).mean(axis=2) > 100).mean() < .01, 'Stale provider data'
assert (read('lumen-validity').mean(axis=2) > 100).mean() > .2
assert read('lumen-summary-tsr').shape == read('lumen-summary').shape
print('UNIFIED_AO_IMAGE_PASS')
