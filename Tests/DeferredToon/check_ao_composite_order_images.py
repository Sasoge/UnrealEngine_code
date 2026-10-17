"""Check AO after indirect replacement/banding, with direct light preserved."""
from pathlib import Path
import sys
import numpy as np
from PIL import Image

root = Path(sys.argv[1])

def read(name):
    return np.asarray(Image.open(root/(name+'.png')).convert('RGB'), dtype=np.float32)

def interior(mask):
    result = mask.copy()
    result[:3] = result[-3:] = False
    result[:,:3] = result[:,-3:] = False
    for y in range(-3,4):
        for x in range(-3,4):
            result &= np.roll(np.roll(mask,y,axis=0),x,axis=1)
    return result

box = read('box').mean(axis=2)>250
for provider in ['lumen','sky']:
    p = provider+'-order-'
    valid = (read(p+'valid').mean(axis=2)>250)&box
    mask = read(p+'mask').mean(axis=2)
    lit = interior(valid&(mask>250))
    dark = interior(valid&(mask<5))
    assert lit.sum()>1000 and dark.sum()>20,(provider,lit.sum(),dark.sum())
    for style in ['replace','dark']:
        before = read(p+style+'-before')
        hard = read(p+style+'-hard')
        half = read(p+style+'-half')
        unchanged = float(np.abs(before[lit]-hard[lit]).mean())
        print(provider,style,'dark pixels',dark.sum(),'hard mean',hard[dark].mean(),'lit MAE',unchanged)
        assert hard[dark].mean()<5,'Authored color overrode final AO attenuation'
        assert before[dark].mean()>half[dark].mean()>hard[dark].mean()
        assert unchanged<3,'Unoccluded authored color changed'
    band_error = float(np.abs(read(p+'band-before')[valid]-read(p+'band-hard')[valid]).mean())
    print(provider,'GI band AO-strength MAE',band_error)
    assert band_error<3,'Independent AO still changes the GI palette classification'
    direct = read(p+'direct-only')
    before = read(p+'composite-before')
    hard = read(p+'composite-hard')
    error = float(np.abs(hard[dark]-direct[dark]).mean())
    print(provider,'occluded composite vs direct-only MAE',error)
    assert direct[dark].mean()>10,'Direct contribution missing from test'
    assert error<3,'AO attenuated direct light or failed to remove indirect'
    assert np.abs(before[lit]-hard[lit]).mean()<3
assert read('order-missing-valid').max()<5
assert np.abs(read('order-missing-hard')-read('order-missing-native')).mean()<2
print('AO_COMPOSITE_ORDER_IMAGE_PASS')
