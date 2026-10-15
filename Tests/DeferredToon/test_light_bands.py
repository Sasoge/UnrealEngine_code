"""Run: python -m unittest discover -s Tests/DeferredToon

Execute the scalar interval selector translated from its actual HLSL source.
GPU captures additionally cover C++ palette packing and output colors.
"""
from pathlib import Path
from types import SimpleNamespace
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Engine/Shaders/Private/PostProcessDeferredToonVolumes.usf'


def selector():
    source = SOURCE.read_text(encoding='utf-8')
    start = source.index('uint SelectDeferredToonLightBand(')
    block = source[start:source.index('\nfloat SelectDeferredToonChannel', start)]
    lines = ['def select(Response, ArtisticShadow, ToonShadowThreshold, LightBandParams, LightBandCount):']
    indent = 0
    for raw in block.splitlines()[1:]:
        line = raw.strip()
        if not line or line.startswith('//'): continue
        if line == '{':
            indent += 1
            continue
        if line == '}':
            indent -= 1
            continue
        line = re.sub(r'(?<=\d)[uf]\b', '', line).replace('&&', 'and').rstrip(';')
        if line.startswith('uint '): line=line[5:]
        if line.startswith('if ('): line='if ' + line[4:-1] + ':'
        if line.startswith('for ('):
            assert line == 'for (uint Index = 1; Index < min(LightBandCount, 8); ++Index)'
            line='for Index in range(1, min(LightBandCount, 8)):'
        lines.append('    '*indent + line)
    namespace = {}
    exec('\n'.join(lines), namespace)
    return namespace['select']


class LightBands(unittest.TestCase):
    def setUp(self):
        self.select = selector()

    def pick(self, energy, thresholds=(0, .1, .4, 1), artistic=0):
        return self.select(energy, artistic, .5,
                           [SimpleNamespace(x=t) for t in thresholds], len(thresholds))

    def test_four_band_boundaries_are_inclusive(self):
        for e, expected in [(0,0),(.0999,0),(.1,1),(.3999,1),(.4,2),(.9999,2),(1,3),(100,3)]:
            self.assertEqual(self.pick(e), expected)

    def test_zero_energy_does_not_jump_across_zero_thresholds(self):
        self.assertEqual(self.pick(0, (0,0,0,0)), 0)
        self.assertEqual(self.pick(.001, (0,0,0,0)), 3)

    def test_artistic_override_has_a_hard_cut(self):
        self.assertEqual(self.pick(100, artistic=.4999), 3)
        self.assertEqual(self.pick(100, artistic=.5), 0)

    def test_positive_energy_never_lowers_interval(self):
        for e in (0,.05,.1,.3,.4,.9,1,5):
            for added in (0,.01,.3,1,10):
                self.assertGreaterEqual(self.pick(e+added), self.pick(e))

    def test_duplicate_threshold_selects_last(self):
        self.assertEqual(self.pick(.4, (0,.4,.4,1)), 2)

    def test_two_and_eight_band_limits(self):
        self.assertEqual(self.pick(.1, (0,.1)), 1)
        self.assertEqual(self.pick(10, tuple(range(8))), 7)
        self.assertEqual(self.pick(10, tuple(range(12))), 7)

    def test_disabled_palette_selector_is_safe(self):
        self.assertEqual(self.pick(10, ()), 0)


if __name__ == '__main__': unittest.main()
