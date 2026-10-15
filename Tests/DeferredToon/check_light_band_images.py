"""Run after verify_light_bands_editor.py; requires Pillow and NumPy.

Optional first argument: the capture directory for a different test project.
"""
from pathlib import Path
import sys
import numpy as np
from PIL import Image

root = Path(__file__).resolve().parents[2]
captures = Path(sys.argv[1]) if len(sys.argv) > 1 else root/'Project/test/Saved/Automation/DeferredToonBands'


def image(label):
    return np.asarray(Image.open(captures/f'bands-{label}.png').convert('RGB'), dtype=np.float32)


for a,b in [('four-white','four-black'), ('four-black','four-exposure'),
            ('four-white','four-reordered'), ('two-before','two-restored'),
            ('four-color-grade','four-color-recompose')]:
    error = float(np.abs(image(a)-image(b)).mean())
    print(a, b, 'mean error:', error)
    assert error < 1.0, (a,b,error)

# Opaque test geometry region; reference frames account for display transforms.
roi = np.s_[150:450,200:750]
actual = image('four-white')[roi].mean(axis=2)
references = np.stack([image('reference-'+n)[roi].mean(axis=2) for n in ('0','03','06','1')])
errors = np.abs(references-actual)
labels = errors.argmin(axis=0)
counts = np.bincount(labels.ravel(), minlength=4)
print('display-calibrated band pixel counts:', counts.tolist())
assert min(counts) > 100, 'Test scene must exercise all four levels'
# Bloom leaks light into the nominally black band, so inspect the clean middle
# plateaus separately rather than asserting that the final display is binary.
for level in (1,2):
    assert ((labels==level) & (errors.min(axis=0)<5)).sum() > 100
assert float(np.median(errors.min(axis=0))) < 1.0
assert float(image('four-no-light')[roi].max()) == 0.0
assert float(np.abs(image('eight')-image('four-white')).mean()) > 1.0
print('LIGHT_BANDS_IMAGE_CHECK_PASS')
