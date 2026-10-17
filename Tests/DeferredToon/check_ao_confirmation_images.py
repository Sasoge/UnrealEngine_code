"""Check actual rendered ablation, cold-entry behavior and motion footprints."""
from pathlib import Path
exec(compile(Path(__file__).with_name('check_ao_temporal_images.py').read_text(encoding='utf-8'),
             'check_ao_temporal_images.py', 'exec'), globals())
extra = {}
for prefix in ('confirm-baseline','confirm-only','confirm-both','confirm-repeat',
               'confirm-motion-base','confirm-motion-on','fast-turn-base','fast-turn-confirm'):
    _, extra[prefix] = measure(prefix)
base, both, repeat = (extra[k] for k in ('confirm-baseline','confirm-both','confirm-repeat'))
assert both['mean_changed_pixels'] < min(base['mean_changed_pixels'],repeat['mean_changed_pixels']), extra
assert both['change_per_dark_area'] < min(base['change_per_dark_area'],repeat['change_per_dark_area']), extra
assert .5 < both['mean_dark_area']/base['mean_dark_area'] < 1.05, extra

extra['edge_changes'] = {}
for name, roi in {'door': (slice(330,550),slice(350,560)),
                  'boxes': (slice(500,720),slice(470,720))}.items():
    stats = {}
    for prefix in ('confirm-baseline','confirm-both','confirm-repeat'):
        frames = np.array(images(prefix,24))[:,roi[0],roi[1]] < 100
        stats[prefix] = float(np.logical_xor(frames[1:],frames[:-1]).sum(axis=(1,2)).mean())
    extra['edge_changes'][name] = stats
    assert stats['confirm-both'] < min(stats['confirm-baseline'],stats['confirm-repeat']), stats

assert np.all(images('cut-hard',1)[0] == 255), 'Newly invalidated history darkened without confirmation'
recovered = images('confirm-recovered',6)
assert (recovered[-1] == 0).sum() > 1000, 'Confirmed AO failed to recover after camera cut'
for prefix in ('static-on','fast-turn-base','fast-turn-confirm'):
    for im in images(prefix,24):
        assert np.all((im==0)|(im==255)), 'Hard output is no longer binary'

for baseline, current in [('confirm-motion-base','confirm-motion-on'),('fast-turn-base','fast-turn-confirm')]:
    outside = []
    for x,y in zip(images(baseline,24),images(current,24)):
        allowed = cv2.dilate((x<100).astype(np.uint8),np.ones((9,9),np.uint8)) > 0
        mask = y < 100
        outside.append(float(np.sum(mask & ~allowed)/max(mask.sum(),1)))
    extra[current+'_outside_4px_max'] = max(outside)
    assert max(outside) < .25, extra
print(json.dumps(extra,indent=2))
print('AO_CONFIRMATION_IMAGE_PASS')
