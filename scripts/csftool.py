"""Read a Westwood CSF string table (generals.csf, extracted from EnglishZH.big with bigtool.py). Standard library only.
usage:
  python -I scripts/csftool.py <file.csf> [--filter substr]   # prints  label<TAB>repr(text)  for every (matching) label
Format: 'FSC ' header (version, label count, string count, ...), then per label: ' LBL', pair count, name length, name;
per pair: ' RTS' or 'WRTS', text length (UTF-16 code units, each byte bitwise NOT), text, and for 'WRTS' an extra
ASCII string (the sound/extra value)."""
import struct, sys

def read_csf(path):
    data = open(path, 'rb').read()
    if data[:4] != b' FSC':
        raise SystemExit('not a CSF file: ' + path)
    version, nlabels, nstrings, _unused, _lang = struct.unpack_from('<IIIII', data, 4)
    pos = 24
    out = {}
    for _ in range(nlabels):
        tag = data[pos:pos + 4]
        if tag != b' LBL':
            raise SystemExit('bad label tag at %d: %r' % (pos, tag))
        npairs, namelen = struct.unpack_from('<II', data, pos + 4)
        pos += 12
        name = data[pos:pos + namelen].decode('latin-1')
        pos += namelen
        text = ''
        for _ in range(npairs):
            stag = data[pos:pos + 4]
            (tlen,) = struct.unpack_from('<I', data, pos + 4)
            pos += 8
            raw = bytes(b ^ 0xFF for b in data[pos:pos + tlen * 2])
            pos += tlen * 2
            text = raw.decode('utf-16-le', errors='replace')
            if stag == b'WRTS':
                (xlen,) = struct.unpack_from('<I', data, pos)
                pos += 4 + xlen
        out[name] = text
    return out

if __name__ == '__main__':
    args = sys.argv[1:]
    flt = None
    if '--filter' in args:
        i = args.index('--filter'); flt = args[i + 1].lower(); del args[i:i + 2]
    table = read_csf(args[0])
    for k in sorted(table):
        if flt is None or flt in k.lower():
            print(k + '\t' + repr(table[k]))
