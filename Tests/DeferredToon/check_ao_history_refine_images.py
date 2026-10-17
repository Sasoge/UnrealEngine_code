"""Runtime image checks for continuous-history ablation and abrupt camera turns."""
from pathlib import Path
exec(compile(Path(__file__).with_name('check_ao_temporal_images.py').read_text(encoding='utf-8'),
             'check_ao_temporal_images.py', 'exec'), globals())

_, scalar_off = measure('scalar-off')
_, scalar_on = measure('scalar-on')
_, scalar_repeat = measure('scalar-off-repeat')
extra = {'scalar_off': scalar_off, 'scalar_on': scalar_on, 'scalar_off_repeat': scalar_repeat}
assert scalar_on['mean_changed_pixels'] < min(scalar_off['mean_changed_pixels'], scalar_repeat['mean_changed_pixels']), extra
assert .75 < scalar_on['mean_dark_area'] / scalar_off['mean_dark_area'] < 1.25, extra

# Test the displayed hard output without TAA/TSR: no soft visibility may leak out.
for im in images('static-on', 24):
    assert np.all((im == 0) | (im == 255)), 'Hard mask contains intermediate values without AA'

fast_off, fast_a = measure('fast-turn-off')
fast_on, fast_b = measure('fast-turn-on')
for prefix in ('fast-turn-off', 'fast-turn-on'):
    for im in images(prefix, 24):
        assert np.all((im == 0) | (im == 255)), 'Motion blur or another display filter contaminated the turn test'
extra['fast_turn_off'] = fast_a
extra['fast_turn_on'] = fast_b
footprints = []
for x, y in zip(fast_off, fast_on):
    allowed = cv2.dilate(x.astype(np.uint8), np.ones((9,9),np.uint8)) > 0
    footprints.append(float(np.sum(y & ~allowed) / max(y.sum(),1)))
extra['fast_turn_outside_4px_max'] = max(footprints)
assert max(footprints) < .25, extra

# Fixed camera ROIs concentrate on thin door bars and boxes instead of white walls.
extra['tsr_edge_changes'] = {}
for name, roi in {'door': (slice(330,550),slice(350,560)),
                  'boxes': (slice(500,720),slice(470,720))}.items():
    changes = {}
    for prefix in ('tsr-static-off','tsr-static-on'):
        frames = np.array(images(prefix,24))[:,roi[0],roi[1]] < 100
        changes[prefix] = float(np.logical_xor(frames[1:],frames[:-1]).sum(axis=(1,2)).mean())
    extra['tsr_edge_changes'][name] = changes
    assert changes['tsr-static-on'] < changes['tsr-static-off'], changes

# Exact rejection colors only in the no-AA diagnostic; TSR blends display colors.
colors = {'accepted': (255,255,255), 'outside': (0,255,255), 'source': (255,255,0),
          'normal': (255,0,255), 'plane': (255,0,0), 'motion': (0,0,255)}
reason_images = [cv2.cvtColor(cv2.imread(str(p)),cv2.COLOR_BGR2RGB)
                 for p in sorted(root.glob('reasons-noaa-*.png'))]
assert len(reason_images) == 8
extra['reasons_noaa_fraction'] = {k: float(np.mean([np.all(im == c,axis=2).mean() for im in reason_images]))
                                for k,c in colors.items()}
assert extra['reasons_noaa_fraction']['accepted'] > .6, extra
print(json.dumps(extra, indent=2))
print('AO_HISTORY_REFINE_IMAGE_PASS')
