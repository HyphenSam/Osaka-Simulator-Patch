# List Windows compatibility layers and the shims they contain, from apppatch\sysmain.sdb.
# Usage: python sdb_layers.py C:\Windows\apppatch\<newest>sysmain.sdb [LAYER ...]
import struct, sys
d = open(sys.argv[1], 'rb').read()
want = [w.upper() for w in sys.argv[2:]]
def u16(o): return struct.unpack_from('<H', d, o)[0]
def u32(o): return struct.unpack_from('<I', d, o)[0]
# locate string table
strtab = None
def walk(o, end, depth, cb):
    while o < end:
        tag = u16(o); typ = tag & 0xF000; o2 = o + 2
        if typ == 0x7000:
            size = u32(o2); cb(tag, o2 + 4, size, depth)
            walk(o2 + 4, o2 + 4 + size, depth + 1, cb); o = o2 + 4 + size
        elif typ in (0x8000, 0x9000):
            size = u32(o2); cb(tag, o2 + 4, size, depth); o = o2 + 4 + size
        else:
            sz = {0x1000: 0, 0x3000: 2, 0x4000: 4, 0x5000: 8, 0x6000: 4}.get(typ, 0)
            cb(tag, o2, sz, depth); o = o2 + sz
        if o & 1: o += 1
def find_strtab(tag, o, size, depth):
    global strtab
    if tag == 0x7801: strtab = o
walk(12, len(d), 0, find_strtab)
def sref(off):
    o = strtab - 6 + off  # offset is from the TAG_STRINGTABLE header
    size = u32(o + 2)
    return d[o + 6:o + 6 + size].decode('utf-16le').rstrip('\0')
# collect layers: TAG_LAYER 0x700B, TAG_NAME 0x6001, TAG_SHIM_REF 0x7009, TAG_FLAG_REF 0x700A
layers = {}
cur = {'layer': None, 'ref': None}
stack = []
def cb(tag, o, size, depth):
    if tag == 0x700B:
        cur['layer'] = [None, [], depth]
        layers.setdefault(id(cur['layer']), cur['layer'])
    lay = cur['layer']
    if lay and depth <= lay[2] and tag != 0x700B:
        cur['layer'] = None; return
    if lay is None: return
    if tag == 0x6001:
        name = sref(u32(o))
        if depth == lay[2] + 1: lay[0] = name
        else: lay[1].append(name)
walk(12, len(d), 0, cb)
for lay in layers.values():
    if lay[0] and (not want or lay[0].upper() in want):
        print(lay[0], '->', ', '.join(lay[1]))
