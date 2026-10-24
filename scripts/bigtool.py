"""Minimal reader/writer for the Westwood BIG archive format used by Generals/Zero Hour. Standard library only.
usage:
  python -I scripts/bigtool.py list    <archive.big> [--filter substr]
  python -I scripts/bigtool.py extract <archive.big> <outdir> [--filter substr]
  python -I scripts/bigtool.py pack    <archive.big> <srcdir>            # packs every file under srcdir, paths relative to srcdir
Format: 'BIGF' + total size (LE u32) + entry count (BE u32) + header size (BE u32), then per entry: offset (BE), size (BE),
NUL-terminated path (backslashes), then the data blobs.
"""
import os, struct, sys

def read_entries(path):
    with open(path, "rb") as f:
        magic = f.read(4)
        if magic not in (b"BIGF", b"BIG4"):
            raise SystemExit(f"{path}: not a BIG archive")
        f.read(4)
        count = struct.unpack(">I", f.read(4))[0]
        f.read(4)
        entries = []
        for _ in range(count):
            off, size = struct.unpack(">II", f.read(8))
            name = b""
            while True:
                c = f.read(1)
                if c == b"\0" or not c:
                    break
                name += c
            entries.append((name.decode("latin-1"), off, size))
        return entries

def cmd_list(archive, flt):
    for name, off, size in read_entries(archive):
        if not flt or flt.lower() in name.lower():
            print(f"{size:>10}  {name}")

def cmd_extract(archive, outdir, flt):
    entries = read_entries(archive)
    n = 0
    with open(archive, "rb") as f:
        for name, off, size in entries:
            if flt and flt.lower() not in name.lower():
                continue
            dst = os.path.join(outdir, name.replace("\\", os.sep))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            f.seek(off)
            with open(dst, "wb") as o:
                o.write(f.read(size))
            n += 1
    print(f"extracted {n} files to {outdir}")

def cmd_pack(archive, srcdir):
    files = []
    for root, _, names in os.walk(srcdir):
        for nm in names:
            full = os.path.join(root, nm)
            rel = os.path.relpath(full, srcdir).replace(os.sep, "\\")
            files.append((rel, full, os.path.getsize(full)))
    files.sort(key=lambda t: t[0].lower())
    header = 16 + sum(8 + len(rel.encode("latin-1")) + 1 for rel, _, _ in files)
    offset = header
    table = b""
    for rel, _, size in files:
        table += struct.pack(">II", offset, size) + rel.encode("latin-1") + b"\0"
        offset += size
    total = offset
    with open(archive, "wb") as o:
        o.write(b"BIGF" + struct.pack("<I", total) + struct.pack(">I", len(files)) + struct.pack(">I", header))
        o.write(table)
        for _, full, _ in files:
            with open(full, "rb") as i:
                o.write(i.read())
    print(f"packed {len(files)} files ({total/1e6:.1f} MB) into {archive}")

if __name__ == "__main__":
    a = sys.argv[1:]
    if not a:
        raise SystemExit(__doc__)
    flt = a[a.index("--filter") + 1] if "--filter" in a else None
    if a[0] == "list":
        cmd_list(a[1], flt)
    elif a[0] == "extract":
        cmd_extract(a[1], a[2], flt)
    elif a[0] == "pack":
        cmd_pack(a[1], a[2])
    else:
        raise SystemExit(__doc__)
