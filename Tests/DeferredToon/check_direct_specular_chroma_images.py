"""Checks real rendered colors against the captured effective-specular input."""
from pathlib import Path
import sys
import numpy as np
from PIL import Image
root=Path(sys.argv[1])
def read(name): return np.asarray(Image.open(root/(name+'.png')).convert('RGB'),dtype=np.float64)/255
weights=np.array([.2126,.7152,.0722])
# Match the existing fixture's white-region cutoff (debug target is quantized).
box=read('box').mean(axis=2)>250/255
interior=box.copy()
for y in range(-2,3):
    for x in range(-2,3): interior &= np.roll(np.roll(box,y,axis=0),x,axis=1)
interior[:2]=interior[-2:]=False;interior[:,:2]=interior[:,-2:]=False
assert interior.sum()>10000
error=np.abs(read('dielectric-red')-read('dielectric-blue'))[interior].mean()
print('Dielectric BaseColor specular difference',error)
assert error<.003,'Dielectric BaseColor was incorrectly multiplied into specular'
for name in ['colored-light','gold','blue-metal','mixed']:
    source=read(name+'-source')
    mask_image=read(name+'-mask').mean(axis=2)
    mask=mask_image>.99
    assert ((mask_image[interior]>.02)&(mask_image[interior]<.98)).mean()<.001
    actual=read(name+'-styled')
    y=source@weights
    # Exclude source quantization near zero and clipped HDR source pixels. Test
    # native RGB ratios, not a guessed metal color (Fresnel can change that ratio).
    selected=interior&mask&(y>.05)&(source.max(axis=2)<.95)
    assert selected.sum()>20,(name,'Insufficient unsaturated source samples',selected.sum())
    expected=source/np.maximum(y[...,None],1.e-6)*.12
    error=np.abs(actual[selected]-expected[selected]).mean()
    print(name,'pixels',int(selected.sum()),'RGB MAE',float(error))
    assert error<.012,(name,'Native material/light chroma was lost')
    lit=interior&mask
    assert np.abs((actual@weights)[lit]-.12).max()<.004,'Highlight luminance is not a single band'
    assert actual[interior&~mask].max()<.005,'Highlight escaped hard mask'
# Require actual colored signal, so a neutral fixture cannot falsely pass.
red=read('colored-light-styled')[interior&(read('colored-light-mask').mean(axis=2)>.99)].mean(axis=0)
assert red[0]>red[1]*2 and red[1]>red[2]*2,red
for name,channel in [('gold',0),('blue-metal',2)]:
    pixels=read(name+'-styled')[interior&(read(name+'-mask').mean(axis=2)>.99)]
    assert pixels[:,channel].mean()>pixels[:,2-channel].mean()*1.3,(name,'Material F0 color missing')
mixed=read('mixed-styled')
mask=read('mixed-mask').mean(axis=2)>.99
assert np.abs(read('mixed-mask')-read('mixed-mask-tint')).max()==0
assert np.abs(read('mixed-mask')-read('fixed-mask')).max()==0
assert np.abs(read('mixed-half')[interior]-mixed[interior]*.5).mean()<.003
assert np.abs(read('mixed-tint')[interior]-mixed[interior]*np.array([.5,1,.8])).mean()<.003
assert np.abs(read('mixed-preexposure')[interior]-mixed[interior]).mean()<.003
assert np.abs(read('mixed-isolated')[interior]-mixed[interior]).mean()<.003
assert read('mixed-zero')[interior].max()<.005
assert read('no-light')[interior].max()<.005
assert np.abs(read('fixed')[interior&mask]-np.array([.3,.08,.015])).max()<.005
print('DIRECT_SPECULAR_CHROMA_PASS')
