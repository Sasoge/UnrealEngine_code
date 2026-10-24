"""Image assertions for native preservation, hard shape and independent composition."""
from pathlib import Path
import sys
import numpy as np
from PIL import Image

root = Path(sys.argv[1])
def read(name): return np.asarray(Image.open(root/(name+'.png')).convert('RGB'),dtype=np.float32)
def linear(image):
    # This fixture disables the tonemapper AND sets display gamma to 1 for linear UNORM.
    # Applying an sRGB decode here would incorrectly darken the measured palette.
    return image/255
def equal(a,b,limit=2):
    error=float(np.abs(read(a)-read(b)).mean())
    print(a,b,'MAE',error)
    assert error<limit,(a,b,error)

equal('baseline','native')
equal('native-restored','baseline-restored')
equal('buffers-off-native','buffers-off-hard')
box=read('box').mean(axis=2)>250
assert box.sum()>10000
fractions=[]
for name in ['mask-low','mask-mid','mask-high']:
    values=read(name).mean(axis=2)[box]
    assert ((values>5)&(values<250)).mean()<.001,'Mask is not binary'
    fractions.append(float((values>250).mean()))
print('Threshold coverage',fractions)
assert fractions[0]>fractions[1]>fractions[2]
for name in ['mask-no-light','styled-no-light','styled-zero','channel-mismatch']:
    assert read(name)[box].max()<5,(name,'Must not synthesize light')
equal('mask-mid','mask-restored')
equal('mask-restored','mask-authored')
equal('mask-restored','mask-preexposure')
assert np.abs(read('mask-restored')-read('mask-rough')).mean()>.1,'Roughness fixture has no effect'
for name in ['sun-source','point-source']:
    assert (read(name)[box]>10).sum()>100,name
both=read('both-source')[box]
single=np.maximum(read('sun-source')[box],read('point-source')[box])
assert (both+2>=single).mean()>.99,'Multi-light accumulation lost a light'
mask=read('mask-restored').mean(axis=2)>250
interior=box.copy()
interior[:2]=interior[-2:]=False;interior[:,:2]=interior[:,-2:]=False
for y in range(-2,3):
    for x in range(-2,3):
        interior &= np.roll(np.roll(box,y,axis=0),x,axis=1)
styled=read('styled')
selected=interior&mask
assert selected.sum()>100
expected=np.array([.3,.08,.015])
error=float(np.abs(linear(styled)[selected]-expected).mean())
print('Fixed scene-linear palette MAE',error)
assert error<.01
assert styled[interior&~mask].max()<5
assert np.abs(linear(read('styled-half'))[selected]-expected*.5).mean()<.01
assert np.abs(read('isolated')[interior]-styled[interior]).mean()<3,'Isolated production highlight differs from preview'
assert read('isolated-zero')[interior].max()<5,'Residual specular added twice or wrong layer retained'
before=linear(read('diffuse-no-specular'))
after=linear(read('diffuse-with-specular'))
expected_composite=before+linear(styled)
unclipped=interior&(expected_composite.max(axis=2)<.95)
assert unclipped.sum()>10000
error=float(np.abs(after[unclipped]-expected_composite[unclipped]).mean())
print('Diffuse plus styled-specular linear MAE',error)
assert error<.01,'Highlight affected diffuse or was added twice'
assert np.abs(read('channel-match')-read('sun-source')).mean()<2
ignored=read('shadow-ignored')[box];matched=read('shadow-matched')[box]
assert (ignored+2>=matched).mean()>.99,'Receiver channel policy reversed'
print('Shadow receiver policy difference',float(np.abs(ignored-matched).mean()))
assert read('mask-tsr').shape==read('mask-mid').shape
print('DIRECT_SPECULAR_IMAGE_PASS')

# Optional separate runtime captures: controlled occluder and D3D11/SM5 smoke.
if len(sys.argv)>2:
    root=Path(sys.argv[2])
    open_light=read('open')[box]
    blocked=read('occluded')[box]
    assert open_light.sum()>1000,'Shadow fixture must contain measurable specular'
    remaining=float(blocked.sum()/open_light.sum())
    print('Controlled occluder remaining specular fraction',remaining)
    assert remaining<.05,'Occluded direct specular leaked into the provider'
    equal('open','receiver-unmatched')
    equal('open','restored')
    print('DIRECT_SPECULAR_SHADOW_PASS')
if len(sys.argv)>3:
    root=Path(sys.argv[3])
    sm5mask=read('mask').mean(axis=2)
    assert ((sm5mask[box]>5)&(sm5mask[box]<250)).mean()<.001
    selected=interior&(sm5mask>250)
    assert selected.sum()>100
    assert np.abs(linear(read('styled'))[selected]-np.array([.3,.08,.015])).mean()<.01
    assert read('both-source')[box].sum()>1000
    assert read('composite').shape==read('native').shape
    print('DIRECT_SPECULAR_SM5_PASS')
