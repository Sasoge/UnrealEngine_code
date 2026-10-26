"""Check independent HDR material emission and visible composition captures."""
import argparse
from pathlib import Path
import numpy as np
from PIL import Image


def check(root, sm5=False):
    def load(name):
        return np.asarray(Image.open(root / (name + '.png')).convert('RGB'), dtype=float)

    raw = load('raw')
    red = (raw[:, :, 0] > 45) & (raw[:, :, 0] < 56) & (raw[:, :, 1] < 20) & (raw[:, :, 2] < 8)
    green = (raw[:, :, 1] > 70) & (raw[:, :, 1] < 83) & (raw[:, :, 0] < 12) & (raw[:, :, 2] < 20)
    assert red.sum() > 100 and green.sum() > 100, ('missing Lit/Unlit emission', red.sum(), green.sum())
    selected = red | green

    def near(a, b, limit=2):
        err = np.abs(a - b).mean()
        assert err < limit, (err, limit)
        return err

    near(raw[red], np.array([.2, .06, .02]) * 255)
    near(raw[green], np.array([.03, .3, .06]) * 255)
    assert load('valid')[selected].min() >= 254
    assert load('valid')[:20, :100].max() <= 1, 'background marked as available'
    near(load('raw-no-lights')[selected], raw[selected], .5)
    near(load('raw-restored')[selected], raw[selected], .5)
    neutral_error = near(load('neutral')[selected], load('native')[selected])
    full, zero, half = (load(n) for n in ('emission-full', 'emission-zero', 'emission-half'))
    near(full[green], raw[green])  # Unlit works despite having no GBuffer normal.
    assert zero[green].max() <= 1
    usable = np.broadcast_to(selected[:, :, None], full.shape) & (full < 240) & (zero < 240)
    near((full - zero)[usable], raw[usable], 2)
    half_error = near(half[usable], ((full + zero) * .5)[usable], 2)
    near(load('masked-on')[red], raw[red], .5)
    assert load('masked-off')[red].max() <= 1, 'discarded masked fragments wrote emission'
    # Gamma-only tonemapping still removes pre-exposure before the PNG is written.
    near(load('hdr-quarter-exposure')[green], np.minimum([2, .6, .2], 1) * 255)
    near(load('hdr-quarter-exposure')[red], raw[red], .5)
    near(load('hdr-quarter-output')[green], np.array([2, .6, .2]) * .25 * 255)
    near(load('exposure-restored')[selected], raw[selected], .5)
    if not sm5:
        near(load('nanite-raw')[green], raw[green], .5)
        assert load('nanite-valid')[green].min() >= 254
        near(load('nanite-output')[green], raw[green])
        assert load('nanite-zero')[green].max() <= 1
    assert load('water-valid')[green].max() <= 1, 'water must not reuse underlying opaque emission'
    assert load('water-raw')[green].max() <= 1
    print(f'{root.name}: PASS, neutral MAE={neutral_error:.4f}/255, half MAE={half_error:.4f}/255; '
          f'Lit pixels={red.sum()}, Unlit pixels={green.sum()}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--sm5', action='store_true')
    args = parser.parse_args()
    check(args.root, args.sm5)
