"""Check captures from verify_indirect_split_editor.py (requires Pillow/numpy)."""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image


def check(root, sm5=False):
    def image(name):
        return np.asarray(Image.open(root / (name + '.png')).convert('RGB'), dtype=float)

    def roi(name):
        # Mirror surface below the objects, including their reflected silhouettes.
        return image(name)[310:440, 420:625]

    def black(name):
        assert roi(name).max() <= 1, name

    def valid(name):
        assert roi(name).min() >= 254, name

    if not sm5:
        error = np.abs(image('baseline') - image('neutral')).mean()
        assert error < 2, ('native composition changed', error)
        black('metal-diffuse')
        black('metal-diffuse-off')
        for name in ('metal-specular', 'metal-specular-restored', 'metal-specular-copy'):
            color = roi(name)
            red = (color[:, :, 0] > 150) & (color[:, :, 1] < 50)
            green = (color[:, :, 1] > 150) & (color[:, :, 0] < 50)
            assert red.sum() > 500 and green.sum() > 500, name
        off = roi('metal-specular-off')
        # Sky fallback stays visible; its brightness is not a traced-reflection test.
        assert ((off[:, :, 0] > 150) & (off[:, :, 1] < 50)).sum() < 20
        assert ((off[:, :, 1] > 150) & (off[:, :, 0] < 50)).sum() < 20
        assert roi('matte-diffuse').mean() > 5
        black('toon-specular')
        valid('toon-valid')  # Supported but intentionally suppressed, not unavailable.
        valid('metal-valid')
        print(f'Lumen: composition MAE={error:.4f}/255; diffuse/specular separated')

    assert roi('sky-diffuse').mean() > 1
    assert roi('sky-specular').mean() > 1
    assert roi('no-sky-specular').mean() > 5, 'SSR export incorrectly requires SkyLight'
    valid('sky-valid')
    valid('no-sky-specular-valid')
    black('no-sky-diffuse')
    black('no-sky-diffuse-valid')
    black('no-reflections')
    black('no-reflections-valid')
    assert image('sky-valid')[0:20, 0:100].max() <= 1, 'background marked valid'
    print(f'{root.name}: non-Lumen diffuse, SSR without sky, and missing data PASS')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--sm5', action='store_true')
    args = parser.parse_args()
    check(args.root, args.sm5)
