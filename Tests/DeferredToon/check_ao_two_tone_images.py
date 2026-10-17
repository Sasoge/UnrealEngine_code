"""Image-level AO replacement tests, including GI preservation and no double AO."""
from pathlib import Path
import sys
import numpy as np
from PIL import Image
root=Path(sys.argv[1])
def read(name): return np.asarray(Image.open(root/(name+'.png')).convert('RGB'),dtype=np.float32)
def equal(a,b,limit=2):
    error=float(np.abs(read(a)-read(b)).mean())
    print(a,b,'MAE',error)
    assert error<limit,(a,b,error)
def interior(mask):
    result=mask.copy()
    result[:2,:]=result[-2:,:]=False
    result[:,:2]=result[:,-2:]=False
    for y in range(-2,3):
        for x in range(-2,3): result &= np.roll(np.roll(mask,y,axis=0),x,axis=1)
    return result
equal('standalone-native','standalone-unified')
equal('standalone-before','standalone-hard-zero')
equal('sky-mask-exposure-base','sky-mask-exposure')
equal('missing-native','missing-hard')
assert read('missing-valid').max()<5
assert read('missing-mask').min()>250,'Unavailable AO must remain neutral white'
box=read('box').mean(axis=2)>250
for provider in ['lumen','sky']:
    valid=(read(provider+'-valid').mean(axis=2)>100)&box
    assert valid.mean()>.2
    fractions=[]
    for key in ['low','mid','high']:
        mask=read(provider+'-mask-'+key).mean(axis=2)
        mid=((mask>5)&(mask<250)&valid).sum()/valid.sum()
        fraction=(mask[valid]<127).mean()
        fractions.append(fraction)
        print(provider,key,'intermediate fraction',mid,'occluded fraction',fraction)
        assert mid<.001,'AO mask contains smooth transitions'
    assert fractions[0]<=fractions[1]<=fractions[2]
    assert fractions[2]-fractions[0]>.001,'AO threshold does not affect coverage'
    mask=read(provider+'-mask-mid').mean(axis=2)
    lit=interior(valid&(mask>250))
    dark=interior(valid&(mask<5))
    assert lit.sum()>1000 and dark.sum()>20,'Missing measurable lit/dark interiors'
    before=read(provider+'-before'); hard=read(provider+'-hard'); half=read(provider+'-half')
    unchanged=float(np.abs(before[lit]-hard[lit]).mean())
    print(provider,'unoccluded GI MAE',unchanged,'dark mean',hard[dark].mean())
    assert unchanged<3,'Unoccluded GI was recolored or double-occluded'
    assert hard[dark].mean()<5,'Hard AO region did not darken'
    assert before[dark].mean()>half[dark].mean()>hard[dark].mean(), 'AO strength ordering broken'
    # GI must retain its spatial variation in unoccluded regions; it is not a
    # two-color replacement. Evaluate a robust dynamic range, not noise count.
    light=hard[lit].mean(axis=1)
    span=float(np.percentile(light,90)-np.percentile(light,10))
    print(provider,'preserved GI range',span)
    assert span>10,'GI was flattened along with AO'
assert read('sky-mask-tsr').shape==read('sky-mask-mid').shape
print('AO_TWO_TONE_IMAGE_PASS')
