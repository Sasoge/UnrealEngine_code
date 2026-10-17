"""Check actual hard-band screenshots; full-scene final compositing is not two colors."""
import sys
from pathlib import Path
import numpy as np
from PIL import Image

root = Path(sys.argv[1])
def read(name):
    return np.asarray(Image.open(root / (name + '.png')).convert('RGB'), dtype=np.float32)
def equal(a,b,limit=2):
    error = float(np.abs(read(a)-read(b)).mean())
    print(a,b,'MAE',error)
    assert error < limit,(a,b,error)
equal('native','grade-neutral')
equal('sky-mixed-output','sky-material-ignored')
equal('sky-material-ignored','sky-legacy-tint-ignored')
equal('sky-mask-exposure-base','sky-mask-exposure')
equal('missing-native','missing-two-tone')
for provider in ['lumen','sky']:
    valid = read(provider+'-coverage').mean(axis=2) > 100
    assert valid.mean() > .2, 'Missing provider'
    fractions=[]
    for band in ['low','mid','high']:
        img=read(provider+'-mask-'+band).mean(axis=2)
        # AA is disabled: the authored mask must not have a soft transition.
        intermediates=((img > 5)&(img < 250)&valid).sum()/valid.sum()
        fraction=(img[valid] > 127).mean()
        print(provider,band,'intermediate',intermediates,'lit fraction',fraction)
        assert intermediates < .001, 'Mask has gradients'
        fractions.append(fraction)
    assert fractions[0] >= fractions[1] >= fractions[2], 'Threshold ordering broken'
    assert fractions[0] - fractions[2] > .05, 'Threshold has no useful effect'
    assert any(.02 < value < .98 for value in fractions), 'No split of lit/dark regions'
    # Provider validity covers the entire viewport, including the native scene
    # outside the authored box. Only full box coverage promises fixed endpoints.
    inside = valid & (read('box-coverage').mean(axis=2) > 250)
    assert inside.mean() > .2, 'No fully covered test region'
    output=read(provider+'-mixed-output')[inside].astype(np.uint8)
    _, counts=np.unique(output,axis=0,return_counts=True)
    top_two=np.sort(counts)[-2:].sum()/len(output)
    print(provider,'two-color coverage',top_two)
    assert top_two > .98, 'Band interiors retained lighting/material gradients'
assert read('lumen-mask-dark').max() < 5
assert read('missing').max() < 5
assert float(np.abs(read('lumen-lit-output')-read('lumen-dark-output')).mean()) > 10
assert read('sky-mask-tsr').shape == read('sky-mask-mid').shape
print('INDIRECT_TWO_TONE_IMAGE_PASS')
