"""Check ordinary PIE-frame captures, including that history was actually used.
Run after verify_ao_temporal_editor.py. Requires numpy and OpenCV.
"""
from pathlib import Path
import json
import sys
import cv2
import numpy as np

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2] / 'Project/test/Saved/Automation/DeferredToonAOTemporal'

def images(prefix, count):
    paths = sorted(root.glob(prefix + '-[0-9]*.png'))
    assert len(paths) == count, (prefix, len(paths), count)
    result = [cv2.imread(str(p), cv2.IMREAD_GRAYSCALE) for p in paths]
    assert all(x is not None for x in result), prefix
    assert all(x.shape == result[0].shape for x in result), prefix
    return result

def measure(prefix):
    frames = np.array(images(prefix, 24)) < 100
    areas = frames.sum(axis=(1, 2))
    changes = np.logical_xor(frames[1:], frames[:-1]).sum(axis=(1, 2))
    assert areas.min() > 100 and areas.max() < frames[0].size * .8, (prefix, areas)
    return frames, {'mean_dark_area': float(areas.mean()), 'mean_changed_pixels': float(changes.mean()),
                    'change_per_dark_area': float(changes.mean() / areas.mean())}

off, a = measure('static-off')
on, b = measure('static-on')
_, repeat = measure('static-off-repeat')
report = {'static_off': a, 'static_on': b, 'static_off_repeat': repeat}
assert b['mean_changed_pixels'] < repeat['mean_changed_pixels'] * .75, report
assert b['mean_changed_pixels'] < a['mean_changed_pixels'] * .75, report
assert b['change_per_dark_area'] < a['change_per_dark_area'] * .8, report
assert .6 < b['mean_dark_area'] / a['mean_dark_area'] < 1.5, report

accepted = images('accepted', 2)
report['accepted_fraction'] = float(np.mean([np.mean(x > 180) for x in accepted]))
assert report['accepted_fraction'] > .6, 'History never used or reprojection invalid'
cut = images('cut-accepted', 1)[0]
report['cut_accepted_fraction'] = float(np.mean(cut > 180))
assert report['cut_accepted_fraction'] < .001, 'Camera cut retained history'
assert np.mean(images('recovered', 2)[-1] > 180) > .6

off_m, a_m = measure('motion-off')
on_m, b_m = measure('motion-on')
report['motion_off'] = a_m
report['motion_on'] = b_m
# Current off/on samples share camera poses but have different AO sample sequences.
# This is a broad trailing-footprint guard, not an exact motion-compensated error.
outside = []
for x, y in zip(off_m, on_m):
    allowed = cv2.dilate(x.astype(np.uint8), np.ones((9, 9), np.uint8)) > 0
    outside.append(float(np.sum(y & ~allowed) / max(y.sum(), 1)))
report['motion_dark_outside_spatial_4px_mean'] = float(np.mean(outside))
assert report['motion_dark_outside_spatial_4px_mean'] < .25, report
assert .6 < b_m['mean_dark_area'] / a_m['mean_dark_area'] < 1.5, report
assert np.mean(images('motion-accepted', 4)[-1] > 180) > .5
assert np.mean(images('source-disabled', 2)[-1] > 180) < .001
assert np.mean(images('source-restored', 2)[-1] > 180) > .6
images('tsr', 12)
report['tsr_accepted_fraction'] = float(np.mean(images('tsr-accepted', 2)[-1] > 180))
assert report['tsr_accepted_fraction'] > .5, report
_, tsr_off = measure('tsr-static-off')
_, tsr_on = measure('tsr-static-on')
report['tsr_static_off'] = tsr_off
report['tsr_static_on'] = tsr_on
assert tsr_on['mean_changed_pixels'] < tsr_off['mean_changed_pixels'], report
assert .6 < tsr_on['mean_dark_area'] / tsr_off['mean_dark_area'] < 1.5, report
sky_source = cv2.imread(str(root / 'sky-provider-001.png'))
assert sky_source is not None
sky_valid = (sky_source[:,:,2] > 180) & (sky_source[:,:,0] < 30)
report['sky_source_fraction'] = float(sky_valid.mean())
sky_accept = images('sky-accepted', 2)[-1] > 180
if sky_valid.mean() > .5:
    assert sky_accept.mean() > .5
else:
    assert sky_accept.mean() < .001
    assert (images('sky-hard', 4)[-1] > 180).mean() > .999
print(json.dumps(report, indent=2))
print('AO_TEMPORAL_IMAGE_PASS')
