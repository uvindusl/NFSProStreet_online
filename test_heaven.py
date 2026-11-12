import pefile, struct
pe = pefile.PE('/home/uvindu/Documents/Games/Need for Speed ProStreet/nfs.exe')
for s in pe.sections:
    name = s.Name.decode('latin1').rstrip('\x00')
    if name == 'HEAVEN':
        heaven = s
        break

data = heaven.get_data()
print(f'.HEAVEN size: {len(data)} bytes, {len(data)//4} dwords')

ib = pe.OPTIONAL_HEADER.ImageBase
code_lo = ib + 0x13ec000
code_hi = ib + 0x13ec000 + 0x2be400

found = 0
unique = set()
for off in range(0, len(data) - 3, 4):
    v = struct.unpack_from('<I', data, off)[0]
    if code_lo <= v < code_hi:
        found += 1
        unique.add(v)
        if found <= 15:
            print(f'  off={off} (0x{off:x}): 0x{v:x} aligned={v % 16 == 0}')

print(f'Total in-range dwords: {found}')
print(f'Unique values: {len(unique)}')
for u in sorted(unique)[:10]:
    print(f'  0x{u:x}')