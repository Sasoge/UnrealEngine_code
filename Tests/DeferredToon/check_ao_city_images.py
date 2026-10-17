"""City hard-AO quality checks; uses separated-in-time screenshots, not motion vectors."""
from pathlib import Path
import sys
import cv2
import numpy as np
from PIL import Image
root=Path(sys.argv[1])
def image(name): return np.asarray(Image.open(root/(name+'.png')).convert('RGB'),dtype=np.float32)
def mask(name):
    a=image(name).mean(2)
    assert ((a>5)&(a<250)).mean()<.001,'Soft transitions introduced'
    return (a<127).astype(np.uint8)
def metrics(m):
    _,_,stats,_=cv2.connectedComponentsWithStats(m,8)
    perimeter=int((m[1:]!=m[:-1]).sum()+(m[:,1:]!=m[:,:-1]).sum())
    return int((stats[1:,cv2.CC_STAT_AREA]<=8).sum()),perimeter,int(m.sum())
results={}
for method in ['raw','filtered','clean']:
    ms=[mask(f'p0-{method}-{i}') for i in range(5)]
    area=float(np.mean([m.sum() for m in ms]))
    change=float(np.mean([np.count_nonzero(a!=b) for a,b in zip(ms,ms[1:])]))
    results[method]=(metrics(ms[0]),area,change,change/area)
    print(method,results[method])
a,_,change,relative=results['raw']; b,_,new_change,new_relative=results['clean']
assert new_change<change*.6,'Insufficient reduction in sampled mask changes'
assert new_relative<relative*.8,'Improvement only from removing dark area'
assert b[0]<a[0]*.4,'Too many isolated tiny features'
assert b[1]/np.sqrt(b[2]) < .75*a[1]/np.sqrt(a[2]),'Area-normalized boundary remains rough'
assert .45 < b[2]/a[2] < 1,'Major dark regions erased or expanded'
for pose in [1,2]:
    a=metrics(mask(f'p{pose}-raw')); b=metrics(mask(f'p{pose}-clean'))
    print('pose',pose,a,b)
    assert b[0]<a[0]*.5
    assert .45 < b[2]/a[2] < 1
    assert b[1]/np.sqrt(b[2]) < .8*a[1]/np.sqrt(a[2])
# Saved street camera: this interior of the large wall is planar despite material detail.
support=image('support')[50:150,100:350].mean()/255
print('planar wall support',float(support))
assert support>.9,'Planar wall lost its filtering neighborhood'
assert image('tsr').shape==image('restored').shape
mask('restored')
print('AO_CITY_IMAGE_PASS')
