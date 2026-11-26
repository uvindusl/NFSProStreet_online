#!/usr/bin/env python3
"""Tests for fx_dump.py.  They build a small synthetic effect in memory; no game data is used.

Run:  python3 lifter/tools/fx_dump_test.py
"""
import os, struct, sys, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fx_dump

NONE = 0xFFFFFFFF
VS = struct.pack('<2I', 0xFFFE0200, 0x0000FFFF)         # vs_2_0, END
PS = struct.pack('<2I', 0xFFFF0300, 0x0000FFFF)         # ps_3_0, END
EXPR = struct.pack('<2I', 0x46580200, 0x0000FFFF)       # stands in for a compiled expression


def dws(*v):
    return struct.pack(f'<{len(v)}I', *v)


def pad(b):
    return b + b'\0' * (-len(b) % 4)


class Builder:
    """Lays out the data area and the tables of a compiled effect."""

    def __init__(self):
        self.data = dws(0)              # offset 0 means "no string", so nothing may live there
        self.params, self.techniques, self.small, self.res = [], [], [], []

    def put(self, b):
        off = len(self.data); self.data += pad(b); return off

    def string(self, s):
        b = s.encode() + b'\0'
        return self.put(dws(len(b)) + b)

    def typedef(self, type_, cls, name='', numeric=True):
        t = dws(type_, cls, self.string(name) if name else 0, 0, 0)
        return self.put(t + (dws(1, 1) if numeric else b''))

    def state(self, op, type_, value, index=0):
        """A state record; object-class for shaders and textures, a 1x1 class-2 constant otherwise."""
        obj = type_ in (5, 15, 16)
        return dws(op, index, self.typedef(type_, 4 if obj else 2, numeric=not obj), self.put(dws(value)))

    def param(self, type_, cls, name, value):
        self.params.append(dws(self.typedef(type_, cls, name, numeric=cls != 4), self.put(value), 0, 0))

    def sampler(self, name, states):
        self.param(10, 4, name, dws(len(states)) + b''.join(states))

    def technique(self, name, states):
        self.techniques.append(dws(self.string(name), 0, 1) + dws(self.string('p0'), 0, len(states)) + b''.join(states))

    def resource(self, technique, index, element, state, usage, blob):
        self.res.append(dws(technique, index, element, state, usage, len(blob)) + pad(blob))

    def build(self, nobjects):
        table = dws(len(self.params), len(self.techniques), 0, nobjects) + b''.join(self.params + self.techniques)
        table += dws(len(self.small), len(self.res)) + b''.join(self.small + self.res)
        return dws(fx_dump.MAGIC, len(self.data)) + self.data + table


def synthetic(ref=b'DIFFUSE_TEXTURE\0', nobjects=6, shadow_ps=None):
    """Objects: 1 texture, 2 the sampler's Texture state, 3 vertex shader, 4 pixel shader, 5 the
    second technique's vertex shader.  The second technique sets PixelShader = NULL."""
    b = Builder()
    b.param(3, 0, 'Scale', dws(0x3F800000))                                     # param 0: float
    b.param(5, 4, 'DIFFUSE_TEXTURE', dws(1))                                    # param 1: texture, object 1
    b.sampler('DIFFUSE_SAMPLER', [b.state(164, 5, 2, 256),                      # param 2
                                  b.state(165, 2, 3, 256),                      # AddressU = CLAMP
                                  b.state(169, 2, 0, 256)])                     # MagFilter = expression
    b.technique('main', [b.state(146, 16, 3), b.state(147, 15, 4),
                         b.state(0, 1, 1), b.state(8, 2, 0)])                   # ZEnable, CullMode = expression
    b.technique('shadow', [b.state(146, 16, 5), shadow_ps or b.state(147, 2, 0)])
    b.small.append(dws(1, 0))
    b.resource(1, 0, NONE, 0, 0, VS)
    b.resource(0, 0, NONE, 3, 0, EXPR)
    b.resource(0, 0, NONE, 1, 0, PS)
    b.resource(0, 0, NONE, 0, 0, VS)
    b.resource(NONE, 2, 0, 2, 0, EXPR)
    b.resource(NONE, 2, 0, 0, 1, ref)
    return b.build(nobjects)


def parse(blob):
    return fx_dump.Effect(blob, 'synthetic').parse()


class FxDumpTest(unittest.TestCase):
    def test_object_table_is_consumed_exactly(self):
        fx = parse(synthetic())
        self.assertEqual(fx.trailing, 0)
        self.assertEqual((fx.nsmall, fx.nresources), (1, 6))
        self.assertEqual(fx_dump.check(fx), [])

    def test_parameter_object(self):
        fx = parse(synthetic())
        self.assertEqual([(r['id'], r['kind'], r['param'], r['data']) for r in fx.small],
                         [(1, 'texture', 'DIFFUSE_TEXTURE', b'')])

    def test_shader_states_resolve(self):
        fx = parse(synthetic())
        main, shadow = (t['passes'][0]['states'] for t in fx.techniques)
        self.assertEqual([fx.shader_of(s) for s in main[:2]], ['vs_2_0', 'ps_3_0'])
        self.assertEqual(main[0]['resource']['data'], VS)
        self.assertEqual(main[1]['resource']['data'], PS)
        self.assertEqual([fx.shader_of(s) for s in shadow], ['vs_2_0', 'NULL'])
        self.assertIsNone(shadow[1]['resource'])

    def test_every_object_has_one_record(self):
        fx = parse(synthetic())
        self.assertEqual(sorted(fx.objects), [1, 2, 3, 4, 5])
        self.assertTrue(all(len(v) == 1 for v in fx.objects.values()))

    def test_expression_and_constant_states(self):
        fx = parse(synthetic())
        main = fx.techniques[0]['passes'][0]['states']
        self.assertEqual(main[2]['value'], [1])
        self.assertIsNone(main[2]['resource'])
        self.assertEqual(main[3]['resource']['kind'], 'expression')
        self.assertEqual(fx_dump.describe(main[2]), 'D3DRS_ZENABLE = 1')
        self.assertEqual(fx_dump.describe(main[3]), 'D3DRS_CULLMODE = <expression 8b>')

    def test_sampler_states(self):
        fx = parse(synthetic())
        tex, addr, mag = fx.params[2]['samplers'][0]
        self.assertEqual((tex['resource']['kind'], tex['resource']['ref']), ('reference', 'DIFFUSE_TEXTURE'))
        self.assertEqual(fx_dump.describe(addr), 'D3DSAMP_ADDRESSU = 3')
        self.assertEqual(mag['resource']['kind'], 'expression')

    def test_state_names(self):
        self.assertEqual(fx_dump.STATE_NAMES[73], ('D3DRS_COLORWRITEENABLE', 'render', 168))
        self.assertEqual(fx_dump.STATE_NAMES[174], ('D3DSAMP_MAXANISOTROPY', 'sampler', 10))
        self.assertEqual(fx_dump.state_name(1), 'op1')
        self.assertEqual(fx_dump.shader_version(dws(0xFFFE0101)), 'vs_1_1')
        self.assertIsNone(fx_dump.shader_version(EXPR))

    def test_trailing_bytes_are_reported(self):
        problems = fx_dump.check(parse(synthetic() + dws(0)))
        self.assertEqual(problems, ['4 unexplained trailing byte(s) after the object table'])

    def test_object_without_data_is_reported(self):
        problems = fx_dump.check(parse(synthetic(nobjects=7)))
        self.assertEqual(problems, ['object 6 has 0 data records, want 1'])

    def test_unknown_reference_is_reported(self):
        problems = fx_dump.check(parse(synthetic(ref=b'NO_SUCH\0')))
        self.assertEqual(len(problems), 1)
        self.assertIn("unknown parameter 'NO_SUCH'", problems[0])

    def test_unresolved_pixel_shader_is_reported(self):
        # The pixel-shader object's record holds something that is not shader bytecode.
        problems = fx_dump.check(parse(synthetic().replace(PS, EXPR)))
        self.assertTrue(any('PixelShader resolves to neither a shader nor NULL' in p for p in problems), problems)

    def test_vertex_shader_slot_given_pixel_shader(self):
        problems = fx_dump.check(parse(synthetic().replace(VS, PS)))
        self.assertTrue(any('VertexShader is assigned a ps_3_0 shader' in p for p in problems), problems)

    def test_truncated_effect_raises(self):
        blob = synthetic()
        with self.assertRaises(ValueError):
            parse(blob[:-6])
        with self.assertRaises(ValueError):
            fx_dump.Effect(b'\0' * 16)


if __name__ == '__main__':
    unittest.main()
