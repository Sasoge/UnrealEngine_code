"""Image outcomes: hard edges retained, silhouette speckles reduced, no AO erasure."""
from pathlib import Path
import sys
import cv2
import numpy as np
from PIL import Image
root=Path(sys.argv[1])
def read(name): return np.asarray(Image.open(root/(name+'.png')).convert('RGB'),dtype=np.float32)
def mask(name):
    img=read(name).mean(axis=2)
    assert ((img>5)&(img<250)).mean()<.001,'Soft transition introduced'
    return (img<127).astype(np.uint8)
def metrics(m):
    _,_,stats,_=cv2.connectedComponentsWithStats(m,8)
    small=int((stats[1:,cv2.CC_STAT_AREA]<=8).sum())
    perimeter=int((m[1:,:]!=m[:-1,:]).sum()+(m[:,1:]!=m[:,:-1]).sum())
    return small,perimeter,int(m.sum())
off=[mask(f'lumen-p0-r0-{i}') for i in range(5)]
on=[mask(f'lumen-p0-r2-{i}') for i in range(5)]
def flicker(images): return float(np.mean([np.count_nonzero(a!=b) for a,b in zip(images,images[1:])]))
raw_flicker=flicker(off); filtered_flicker=flicker(on)
print('static changed pixels:',raw_flicker,filtered_flicker)
assert filtered_flicker<raw_flicker*.8,'No material reduction of static AO edge noise'
for pose in range(3):
    raw=mask(f'lumen-p{pose}-r0-0'); filtered=mask(f'lumen-p{pose}-r2-0')
    a=metrics(raw); b=metrics(filtered)
    print('pose',pose,'small components/perimeter/area',a,b)
    assert b[1]<a[1]*.9,'No reduction of jagged edge perimeter'
    assert b[0]<=a[0],'Added isolated speckles'
    assert .7<b[2]/a[2]<1.3,'AO was erased or grew excessively'
for r in [0,2]:
    for i in range(2): mask(f'sky-r{r}-{i}')
for provider in ['lumen','sky']:
    valid=read(provider+'-valid').mean(axis=2)>100
    m=on[0] if provider=='lumen' else mask('sky-r2-0')
    assert (m[~valid]>0).sum()<10,'Filtered AO leaked into unavailable pixels'
    error=float(np.abs(read(provider+'-raw-off')-read(provider+'-raw-on')).mean())
    print(provider,'raw AO diagnostic MAE',error)
    assert error<2,'Raw AO diagnostics were modified'
assert read('missing-mask').min()>250
assert read('missing-valid').max()<5
assert read('lumen-tsr').shape==read('lumen-return').shape
print('AO_FILTER_IMAGE_PASS')
