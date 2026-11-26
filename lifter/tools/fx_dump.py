#!/usr/bin/env python3
"""fx_dump.py - list the contents of the compiled D3DX effects (fx_2_0, magic 0xFEFF0901)
that a game image carries in its resources.

ProStreet creates its effects with D3DXCreateEffectFromResourceA; the effect files are
RT_RCDATA resources named IDI_*_FX.  The HLE for ID3DXEffect has to read this format, so this
tool decodes the part of it that is understood and checks it, and reports what it cannot yet
read.  Nothing from the image is written to disk unless you ask for it (--extract).

Usage:
    fx_dump.py <pe-image | effect.fxo> [--extract DIR] [--check] [-v]

Layout understood so far (all DWORDs little-endian; offsets are relative to byte 8 of the
file, i.e. just after the magic and the 'tail offset'):

    +0   0xFEFF0901
    +4   tail_offset
    +8+tail_offset:   nparams, ntechniques, unknown (always 4 here), nobjects
                      nparams  x { typedef_off, value_off, flags, nannotations, annotations... }
                      ntechniques x { name_off, nannotations, npasses, annotations...,
                                      npasses x { name_off, nannotations, nstates, annotations...,
                                                  nstates x { operation, index, typedef_off, value_off } } }
    annotation        { typedef_off, value_off }
    typedef           { type, class, name_off, semantic_off, element_count,
                        [cols, rows] for class scalar/vector/matrix }
    string            { length, bytes (NUL-terminated, padded to 4) }

D3DXPARAMETER_TYPE: 1 BOOL 2 INT 3 FLOAT 4 STRING 5 TEXTURE 10 SAMPLER 15 PIXELSHADER 16 VERTEXSHADER.
State operations 146 and 147 carry typedef types 16 and 15 in the effects checked, so they are
the vertex-shader and pixel-shader assignments. The value at value_off starts with an object
index. The one exception is operation 147 in the shadow-map techniques of IDI_WORLDBONE_FX
(RenderLiSPSM*), which has an int typedef and the value 0; that looks like `PixelShader = NULL`
(a depth-only pass), but it is an inference, not something read from the file. What follows the technique table (the object table with the shader bytecode, strings and
textures) is NOT decoded here.
"""
import argparse, collections, os, struct, sys

TYPE_NAMES = {0: 'void', 1: 'bool', 2: 'int', 3: 'float', 4: 'string', 5: 'texture', 6: 'texture1d',
              7: 'texture2d', 8: 'texture3d', 9: 'texturecube', 10: 'sampler', 11: 'sampler1d',
              12: 'sampler2d', 13: 'sampler3d', 14: 'samplercube', 15: 'pixelshader',
              16: 'vertexshader'}
MAGIC = 0xFEFF0901


class Effect:
    def __init__(self, data, name=''):
        self.d, self.name = data, name
        if len(data) < 16 or self.dw(0) != MAGIC:
            raise ValueError('not a compiled effect (magic 0xFEFF0901)')
        self.cur = 8 + self.dw(4)

    def dw(self, o):
        if o < 0 or o + 4 > len(self.d):
            raise ValueError(f'read past end of effect at {o:#x}')
        return struct.unpack_from('<I', self.d, o)[0]

    def rd(self):
        v = self.dw(self.cur); self.cur += 4; return v

    def string(self, off):
        if not off:
            return ''
        o = 8 + off; n = self.dw(o)
        if o + 4 + n > len(self.d):
            raise ValueError(f'string at {off} runs past the end')
        return self.d[o + 4:o + 4 + n].split(b'\0')[0].decode('latin1')

    def typedef(self, off):
        o = 8 + off; w = lambda i: self.dw(o + 4 * i)
        t = dict(type=w(0), cls=w(1), name=self.string(w(2)), semantic=self.string(w(3)), elements=w(4))
        if t['cls'] in (0, 1, 2, 3):
            t['cols'], t['rows'] = w(5), w(6)
        return t

    def annotations(self, n):
        out = []
        for _ in range(n):
            toff, voff = self.rd(), self.rd()
            out.append(dict(typedef=self.typedef(toff), value_off=voff))
        return out

    def parse(self):
        self.nparams, self.ntechs, self.unknown, self.nobjects = (self.rd() for _ in range(4))
        self.params = []
        for _ in range(self.nparams):
            toff, voff, flags, na = (self.rd() for _ in range(4))
            p = self.typedef(toff); p.update(value_off=voff, flags=flags)
            p['annotations'] = self.annotations(na); self.params.append(p)
        self.techniques = []
        for _ in range(self.ntechs):
            name, na, npass = self.rd(), self.rd(), self.rd()
            tech = dict(name=self.string(name), annotations=self.annotations(na), passes=[])
            for _ in range(npass):
                pname, pna, nst = self.rd(), self.rd(), self.rd()
                ps = dict(name=self.string(pname), annotations=self.annotations(pna), states=[])
                for _ in range(nst):
                    op, idx, toff, voff = (self.rd() for _ in range(4))
                    st = dict(op=op, index=idx, typedef=self.typedef(toff), value_off=voff)
                    st['object'] = self.dw(8 + voff)    # object index, for shader states
                    ps['states'].append(st)
                tech['passes'].append(ps)
            self.techniques.append(tech)
        self.tail_start = self.cur          # the object table starts here (not decoded)
        return self


def resources(path):
    """(name, bytes) of every effect resource of a PE image, or the file itself if it is an effect."""
    data = open(path, 'rb').read()
    if len(data) >= 4 and struct.unpack_from('<I', data, 0)[0] == MAGIC:
        return [(os.path.basename(path), data)]
    import pefile
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_RESOURCE']])
    out = []
    for t in getattr(pe, 'DIRECTORY_ENTRY_RESOURCE', None).entries if hasattr(pe, 'DIRECTORY_ENTRY_RESOURCE') else []:
        if t.id != 10:                      # RT_RCDATA
            continue
        for e in t.directory.entries:
            for l in e.directory.entries:
                r = l.data.struct; blob = pe.get_data(r.OffsetToData, r.Size)
                if len(blob) >= 4 and struct.unpack_from('<I', blob, 0)[0] == MAGIC:
                    out.append((e.name.decode('latin1') if e.name else str(e.id), blob))
    return out


def check(fx):
    """Consistency checks that the layout above is right; returns a list of problems."""
    bad = []
    for t in fx.techniques:
        for p in t['passes']:
            for s in p['states']:
                if s['op'] in (146, 147):
                    want = 16 if s['op'] == 146 else 15
                    if s['op'] == 147 and s['typedef']['type'] == 2 and s['object'] == 0:
                        continue            # PixelShader = NULL, see the module docstring
                    if s['typedef']['type'] != want:
                        bad.append(f"{t['name']}: state {s['op']} has typedef type {s['typedef']['type']}, want {want}")
                    if s['object'] >= fx.nobjects:
                        bad.append(f"{t['name']}: state {s['op']} object index {s['object']} >= {fx.nobjects}")
    if fx.tail_start > len(fx.d):
        bad.append('technique table runs past the end of the effect')
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('image'); ap.add_argument('--extract', metavar='DIR'); ap.add_argument('--check', action='store_true')
    ap.add_argument('-v', '--verbose', action='store_true')
    a = ap.parse_args()
    effects = resources(a.image)
    if not effects:
        sys.exit('no compiled effects found')
    ops = collections.Counter(); problems = 0
    for name, blob in effects:
        if a.extract:
            os.makedirs(a.extract, exist_ok=True)
            open(os.path.join(a.extract, name + '.fxo'), 'wb').write(blob)
        try:
            fx = Effect(blob, name).parse()
        except ValueError as e:
            print(f'{name}: PARSE ERROR: {e}'); problems += 1; continue
        npass = sum(len(t['passes']) for t in fx.techniques)
        print(f'{name:34s} {len(blob):7d} bytes  {fx.nparams:3d} params  {len(fx.techniques):2d} techniques  '
              f'{npass:2d} passes  {fx.nobjects:3d} objects')
        for t in fx.techniques:
            for p in t['passes']:
                for s in p['states']:
                    ops[s['op']] += 1
        if a.verbose:
            for t in fx.techniques:
                for p in t['passes']:
                    vs = [s['object'] for s in p['states'] if s['op'] == 146]
                    ps_ = [s['object'] for s in p['states'] if s['op'] == 147]
                    other = [(s['op'], TYPE_NAMES.get(s['typedef']['type'], s['typedef']['type'])) for s in p['states'] if s['op'] not in (146, 147)]
                    print(f"    technique {t['name']:28s} pass {p['name'] or '-':8s} vs=obj{vs} ps=obj{ps_} other states={other}")
        if a.check:
            for msg in check(fx):
                print('   CHECK FAILED:', msg); problems += 1
    print(f'{len(effects)} effects; state operations used: {dict(sorted(ops.items()))}')
    if a.check:
        print('CHECK:', 'ok' if not problems else f'{problems} problem(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
