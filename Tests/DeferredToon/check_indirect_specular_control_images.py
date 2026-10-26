"""Validate independent environment reflection composition in linear captures."""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image


def check(root):
    def roi(name):
        image = np.asarray(Image.open(root / (name + '.png')).convert('RGB'), dtype=float)
        return image[310:440, 420:625]

    def near(a, b, limit=2):
        error = np.abs(a - b).mean()
        assert error < limit, (error, limit)
        return error

    neutral_error = near(roi('neutral'), roi('baseline'))
    near(roi('reflection-zero'), roi('direct-only'))
    full = roi('reflection-full')
    zero = roi('reflection-zero')
    added = full - zero
    assert added.mean() > 1, 'Other=0 must allow independent reflections'
    # Compare unsaturated channels only, since the screenshot is an 8-bit output.
    usable = (full < 240) & (zero < 240)
    half_error = near(roi('reflection-half')[usable], ((full + zero) * .5)[usable], 3)
    near(roi('reflection-diffuse-zero'), full, 3)
    matte_without_diffuse = roi('matte-diffuse-zero-reflection') - roi('matte-diffuse-zero')
    matte_with_diffuse = roi('matte-diffuse-one-reflection') - roi('matte-diffuse-one')
    independence_error = near(matte_without_diffuse, matte_with_diffuse, 2)
    assert (roi('matte-diffuse-one') - roi('matte-diffuse-zero')).mean() > 1
    near(roi('matte-both-neutral'), roi('matte-native'))
    near(roi('missing-override'), roi('missing-native'))
    print(f'{root.name}: PASS; neutral MAE={neutral_error:.4f}/255, '
          f'half MAE={half_error:.4f}/255, diffuse independence MAE={independence_error:.4f}/255, '
          f'added reflection mean={added.mean():.4f}/255')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    check(parser.parse_args().root)
