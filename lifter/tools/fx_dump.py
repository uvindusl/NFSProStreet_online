#!/usr/bin/env python3
"""fx_dump.py - list the contents of the compiled D3DX effects (fx_2_0, magic 0xFEFF0901)
that a game image carries in its resources.

ProStreet creates its effects with D3DXCreateEffectFromResourceA; the effect files are
RT_RCDATA resources named IDI_*_FX.  The HLE for ID3DXEffect has to read this format, so this
tool decodes the part of it that is understood and checks it, and reports what it cannot yet
read.  Nothing from the image is written to disk unless you ask for it (--extract).

Usage:
    fx_dump.py <pe-image | effect.fxo> [--extract DIR] [--check] [-v]

The format is described in docs/fx-effect-format.md, which also says which parts are confirmed
and which are inferred.  In short (all DWORDs little-endian; offsets are relative to byte 8 of
the file, i.e. just after the magic and the table offset):

    +0   0xFEFF0901
    +4   table_offset
    +8   data area: typedefs, strings and values, addressed by offset
    +8+table_offset:
         nparams, ntechniques, unknown, nobjects
         nparams     x { typedef_off, value_off, flags, nannotations, annotations... }
         ntechniques x { name_off, nannotations, npasses, annotations...,
                         npasses x { name_off, nannotations, nstates, annotations..., states... } }
         nsmall, nresources
         nsmall      x { object_id, size, data (padded to 4) }
         nresources  x { technique_index, index, element_index, state_index, usage,
                         size, data (padded to 4) }
    state             { operation, index, typedef_off, value_off }
    annotation        { typedef_off, value_off }
    typedef           { type, class, name_off, semantic_off, element_count,
                        [cols, rows] for class scalar/vector/matrix }
    string            { length, bytes (NUL-terminated, padded to 4) }
    sampler value     { nstates, states... }
    object value      { object_id }

A resource record carries the value of one state: a pass state (technique_index, index = pass)
or, when technique_index is 0xFFFFFFFF, a state of a sampler parameter (index = parameter).
usage 0 is a blob (shader bytecode for the shader states, otherwise a compiled expression that
this tool does not decode), usage 1 is the name of the parameter the state takes its value from.

The preshader / expression VM and the parameter default values are NOT decoded here.
"""
import argparse, collections, os, struct, sys

TYPE_NAMES = {0: 'void', 1: 'bool', 2: 'int', 3: 'float', 4: 'string', 5: 'texture', 6: 'texture1d',
              7: 'texture2d', 8: 'texture3d', 9: 'texturecube', 10: 'sampler', 11: 'sampler1d',
              12: 'sampler2d', 13: 'sampler3d', 14: 'samplercube', 15: 'pixelshader',
              16: 'vertexshader'}
MAGIC = 0xFEFF0901
CLASS_OBJECT = 4
SAMPLER_TYPES = (10, 11, 12, 13, 14)
OBJECT_PARAM_TYPES = (4, 5, 6, 7, 8, 9, 15, 16)     # parameters whose value is an object id
OP_VERTEXSHADER, OP_PIXELSHADER, OP_TEXTURE = 146, 147, 164
NO_INDEX = 0xFFFFFFFF
EXPRESSION_TOKEN = 0x46580200                       # first DWORD of a compiled expression blob
END_TOKEN = 0x0000FFFF                              # last DWORD of shader bytecode

# State operation -> (name, kind, D3DRENDERSTATETYPE / D3DSAMPLERSTATETYPE value or None).
# Only the operations the 34 ProStreet effects use are listed; see docs/fx-effect-format.md for
# the evidence behind each one.
STATE_NAMES = {
    0: ('D3DRS_ZENABLE', 'render', 7),
    3: ('D3DRS_ZWRITEENABLE', 'render', 14),
    6: ('D3DRS_SRCBLEND', 'render', 19),
    7: ('D3DRS_DESTBLEND', 'render', 20),
    8: ('D3DRS_CULLMODE', 'render', 22),
    9: ('D3DRS_ZFUNC', 'render', 23),
    13: ('D3DRS_ALPHABLENDENABLE', 'render', 27),
    14: ('D3DRS_FOGENABLE', 'render', 28),
    73: ('D3DRS_COLORWRITEENABLE', 'render', 168),
    99: ('D3DRS_SEPARATEALPHABLENDENABLE', 'render', 206),
    100: ('D3DRS_SRCBLENDALPHA', 'render', 207),
    101: ('D3DRS_DESTBLENDALPHA', 'render', 208),
    146: ('VertexShader', 'shader', None),
    147: ('PixelShader', 'shader', None),
    164: ('Texture', 'texture', None),
    165: ('D3DSAMP_ADDRESSU', 'sampler', 1),
    166: ('D3DSAMP_ADDRESSV', 'sampler', 2),
    167: ('D3DSAMP_ADDRESSW', 'sampler', 3),
    168: ('D3DSAMP_BORDERCOLOR', 'sampler', 4),
    169: ('D3DSAMP_MAGFILTER', 'sampler', 5),
    170: ('D3DSAMP_MINFILTER', 'sampler', 6),
    171: ('D3DSAMP_MIPFILTER', 'sampler', 7),
    174: ('D3DSAMP_MAXANISOTROPY', 'sampler', 10),
}


def state_name(op):
    return STATE_NAMES[op][0] if op in STATE_NAMES else f'op{op}'


def shader_version(blob):
    """'vs_2_0' / 'ps_3_0' for shader bytecode, or None if the blob does not start with a version token."""
    if len(blob) < 4:
        return None
    w = struct.unpack_from('<I', blob, 0)[0]
    kind = {0xFFFE: 'vs', 0xFFFF: 'ps'}.get(w >> 16)
    return f'{kind}_{(w >> 8) & 0xFF}_{w & 0xFF}' if kind else None


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

    def state(self, o):
        """The state record { operation, index, typedef_off, value_off } at absolute offset o."""
        op, idx, toff, voff = (self.dw(o + 4 * i) for i in range(4))
        td = self.typedef(toff)
        n = 1 if td['cls'] == CLASS_OBJECT else max(1, td.get('cols', 1) * td.get('rows', 1)) * max(1, td['elements'])
        value = [self.dw(8 + voff + 4 * i) for i in range(n)]
        # 'object' is the object id for object-class states (shaders, textures) and the first
        # value DWORD otherwise.  'resource' is filled in by the object table, if it has a record.
        return dict(op=op, index=idx, typedef=td, value_off=voff, value=value, object=value[0], resource=None)

    def states(self, n):
        out = [self.state(self.cur + 16 * i) for i in range(n)]
        self.cur += 16 * n
        return out

    def sampler(self, o):
        """Sampler value { nstates, states... } at absolute offset o; returns (states, end offset)."""
        n = self.dw(o)
        return [self.state(o + 4 + 16 * i) for i in range(n)], o + 4 + 16 * n

    def parse(self):
        self.nparams, self.ntechs, self.unknown, self.nobjects = (self.rd() for _ in range(4))
        self.params = []
        for _ in range(self.nparams):
            toff, voff, flags, na = (self.rd() for _ in range(4))
            p = self.typedef(toff); p.update(value_off=voff, flags=flags)
            p['annotations'] = self.annotations(na)
            if p['cls'] == CLASS_OBJECT and p['type'] in SAMPLER_TYPES:
                # One block per element.  Every sampler in the game has element_count 0, so the
                # layout of sampler arrays (consecutive blocks) is assumed, not observed.
                p['samplers'] = []; o = 8 + voff
                for _ in range(max(1, p['elements'])):
                    sts, o = self.sampler(o); p['samplers'].append(sts)
            elif p['cls'] == CLASS_OBJECT and p['type'] in OBJECT_PARAM_TYPES and not p['elements']:
                p['object'] = self.dw(8 + voff)
            self.params.append(p)
        self.techniques = []
        for _ in range(self.ntechs):
            name, na, npass = self.rd(), self.rd(), self.rd()
            tech = dict(name=self.string(name), annotations=self.annotations(na), passes=[])
            for _ in range(npass):
                pname, pna, nst = self.rd(), self.rd(), self.rd()
                ps = dict(name=self.string(pname), annotations=self.annotations(pna))
                ps['states'] = self.states(nst)
                tech['passes'].append(ps)
            self.techniques.append(tech)
        self.tail_start = self.cur          # the object table starts here
        self.parse_objects()
        return self

    def blob(self):
        n = self.rd()
        if self.cur + n > len(self.d):
            raise ValueError(f'object data at {self.cur:#x} ({n} bytes) runs past the end')
        b = self.d[self.cur:self.cur + n]; self.cur += (n + 3) & ~3
        return b

    def resource_state(self, ti, idx, el, si):
        """The state a resource record addresses, and a printable description of where it is."""
        if ti == NO_INDEX:
            if idx >= len(self.params) or 'samplers' not in self.params[idx]:
                raise ValueError(f'resource addresses parameter {idx}, which is not a sampler')
            p = self.params[idx]; e = 0 if el == NO_INDEX or not p['elements'] else el
            if e >= len(p['samplers']) or si >= len(p['samplers'][e]):
                raise ValueError(f"resource addresses state {si} of element {el:#x} of {p['name']}, out of range")
            return p['samplers'][e][si], f"{p['name']}.state{si}"
        if ti >= len(self.techniques) or idx >= len(self.techniques[ti]['passes']):
            raise ValueError(f'resource addresses technique {ti} pass {idx}, out of range')
        t = self.techniques[ti]; sts = t['passes'][idx]['states']
        if si >= len(sts):
            raise ValueError(f"resource addresses state {si} of {t['name']} pass {idx}, out of range")
        return sts[si], f"{t['name']}/pass{idx}.state{si}"

    def parse_objects(self):
        """The object table: the data of the objects parameters own, then one record per state
        whose value is not a constant in the data area."""
        self.nsmall, self.nresources = self.rd(), self.rd()
        self.objects = collections.defaultdict(list)    # object id -> records carrying its data
        owners = {p['object']: p for p in self.params if 'object' in p}
        self.small = []
        for _ in range(self.nsmall):
            oid = self.rd(); data = self.blob(); p = owners.get(oid)
            rec = dict(id=oid, data=data, param=p['name'] if p else None,
                       kind=TYPE_NAMES.get(p['type'], 'unknown') if p else 'unknown')
            self.small.append(rec); self.objects[oid].append(rec)
        self.resources = []
        for _ in range(self.nresources):
            ti, idx, el, si, usage = (self.rd() for _ in range(5))
            data = self.blob()
            st, where = self.resource_state(ti, idx, el, si)
            rec = dict(technique=ti, index=idx, element=el, state_index=si, usage=usage, data=data,
                       where=where, state=st, kind='unknown', shader=None, ref=None)
            first = struct.unpack_from('<I', data, 0)[0] if len(data) >= 4 else None
            if usage == 0 and shader_version(data):
                rec['kind'], rec['shader'] = 'shader', shader_version(data)
            elif usage == 0 and first == EXPRESSION_TOKEN:
                rec['kind'] = 'expression'
            elif usage == 1:
                rec['kind'], rec['ref'] = 'reference', data.split(b'\0')[0].decode('latin1')
            st['resource'] = rec; self.resources.append(rec)
            if st['typedef']['cls'] == CLASS_OBJECT:
                rec['id'] = st['object']; self.objects[st['object']].append(rec)
        self.trailing = len(self.d) - self.cur

    def shader_of(self, st):
        """What a VertexShader / PixelShader state assigns: 'vs_2_0'-style version, 'NULL', or None
        if it is neither a shader object nor a constant NULL."""
        res = st['resource']
        if res is not None:
            return res['shader'] if st['typedef']['type'] in (15, 16) else None
        if st['typedef']['type'] == 2 and st['value'] == [0]:
            return 'NULL'                   # `PixelShader = NULL`: an int constant 0, no resource
        return None


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


def all_states(fx):
    """(where, state) for every pass state and every sampler state of the effect."""
    for t in fx.techniques:
        for i, p in enumerate(t['passes']):
            for s in p['states']:
                yield f"{t['name']}/pass{i}", s
    for p in fx.params:
        for sts in p.get('samplers', []):
            for s in sts:
                yield p['name'], s


def check(fx):
    """Consistency checks that the layout above is right; returns a list of problems."""
    bad = []
    if fx.trailing:
        bad.append(f'{fx.trailing} unexplained trailing byte(s) after the object table')
    for oid in range(1, fx.nobjects):
        n = len(fx.objects.get(oid, []))
        if n != 1:
            bad.append(f'object {oid} has {n} data records, want 1')
    for oid in sorted(fx.objects):
        if oid == 0 or oid >= fx.nobjects:
            bad.append(f'data record for object {oid}, outside 1..{fx.nobjects - 1}')
    for rec in fx.small:
        if rec['param'] is None:
            bad.append(f"object {rec['id']} in the parameter-object table belongs to no parameter")
    names = {p['name'] for p in fx.params}
    for rec in fx.resources:
        if rec['kind'] == 'unknown':
            bad.append(f"{rec['where']}: resource with usage {rec['usage']} is not decoded")
        elif rec['kind'] == 'reference' and rec['ref'] not in names:
            bad.append(f"{rec['where']}: refers to unknown parameter {rec['ref']!r}")
        elif rec['kind'] == 'shader' and (len(rec['data']) % 4 or rec['data'][-4:] != struct.pack('<I', END_TOKEN)):
            bad.append(f"{rec['where']}: shader bytecode does not end with the END token")
    for where, s in all_states(fx):
        if s['op'] not in STATE_NAMES:
            bad.append(f"{where}: state operation {s['op']} has no name")
        if s['op'] in (OP_VERTEXSHADER, OP_PIXELSHADER):
            sh = fx.shader_of(s); want = 'vs' if s['op'] == OP_VERTEXSHADER else 'ps'
            if sh is None:
                bad.append(f"{where}: {state_name(s['op'])} resolves to neither a shader nor NULL")
            elif sh != 'NULL' and not sh.startswith(want):
                bad.append(f"{where}: {state_name(s['op'])} is assigned a {sh} shader")
    return bad


def describe(s):
    """`NAME = value` for one state."""
    res = s['resource']; name = state_name(s['op'])
    if s['op'] in (OP_VERTEXSHADER, OP_PIXELSHADER) and res is None:
        return f"{name} = NULL" if s['value'] == [0] else f"{name} = ?{s['value']}"
    if res is None:
        v = [struct.unpack('<f', struct.pack('<I', x))[0] for x in s['value']] if s['typedef']['type'] == 3 else s['value']
        return f"{name} = {v[0] if len(v) == 1 else v}"
    if res['kind'] == 'shader':
        return f"{name} = obj{s['object']} {res['shader']} {len(res['data'])}b"
    if res['kind'] == 'reference':
        return f"{name} = <{res['ref']}>"
    return f"{name} = <{res['kind']} {len(res['data'])}b>"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('image'); ap.add_argument('--extract', metavar='DIR'); ap.add_argument('--check', action='store_true')
    ap.add_argument('-v', '--verbose', action='store_true')
    a = ap.parse_args()
    effects = resources(a.image)
    if not effects:
        sys.exit('no compiled effects found')
    ops = collections.Counter(); kinds = collections.Counter(); problems = 0
    for name, blob in effects:
        if a.extract:
            os.makedirs(a.extract, exist_ok=True)
            open(os.path.join(a.extract, name + '.fxo'), 'wb').write(blob)
        try:
            fx = Effect(blob, name).parse()
        except ValueError as e:
            print(f'{name}: PARSE ERROR: {e}'); problems += 1; continue
        npass = sum(len(t['passes']) for t in fx.techniques)
        nshader = sum(r['kind'] == 'shader' for r in fx.resources)
        print(f'{name:34s} {len(blob):7d} bytes  {fx.nparams:3d} params  {len(fx.techniques):2d} techniques  '
              f'{npass:2d} passes  {fx.nobjects:3d} objects  {nshader:2d} shaders  {fx.trailing} trailing')
        for t in fx.techniques:
            for p in t['passes']:
                for s in p['states']:
                    ops[s['op']] += 1
        for r in fx.small:
            kinds[r['kind'] + ' object'] += 1
        for r in fx.resources:
            kinds[r['shader'] or r['kind']] += 1
        if a.verbose:
            for t in fx.techniques:
                for p in t['passes']:
                    print(f"    technique {t['name']:28s} pass {p['name'] or '-':8s} " + '; '.join(describe(s) for s in p['states']))
            for p in fx.params:
                for i, sts in enumerate(p.get('samplers', [])):
                    label = p['name'] + (f'[{i}]' if p['elements'] else '')
                    print(f"    sampler {label:30s} " + '; '.join(describe(s) for s in sts))
            for r in fx.small:
                print(f"    object {r['id']:3d} {r['kind']} of parameter {r['param']} ({len(r['data'])} bytes)")
        if a.check:
            for msg in check(fx):
                print('   CHECK FAILED:', msg); problems += 1
    print(f'{len(effects)} effects; state operations used: {dict(sorted(ops.items()))}')
    print(f'object table records: {dict(sorted(kinds.items()))}')
    if a.check:
        print('CHECK:', 'ok' if not problems else f'{problems} problem(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
