import struct, zlib, os

SCAN = []
for root, dirs, files in os.walk(r"h:/OpenTS/OpenTS/Run"):
    if "Saved Games" in root or "Screenshots" in root:
        continue
    for fn in files:
        if fn.lower().endswith(".mix"):
            SCAN.append(os.path.join(root, fn).replace("\\", "/"))

# TS mix local checksum: standard CRC-32 with bit-reversed polynomial 0x04C11DB7
def ts_name_crc(name):
    name = name.upper().encode()
    crc = 0xFFFFFFFF
    for b in name:
        crc ^= b << 24
        for _ in range(8):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
    return crc & 0xFFFFFFFF

def read_index(path):
    with open(path, "rb") as f:
        head = f.read(10)
        first, flags = struct.unpack_from("<HH", head, 0)
        if first != 0:
            count = first
            offset = 6
        else:
            count = struct.unpack_from("<H", head, 4)[0]
            offset = 10
        entries = {}
        f.seek(offset)
        for _ in range(count):
            crc, off, sz = struct.unpack("<III", f.read(12))
            entries[crc] = (off, sz)
        return f, entries

def main():
    target = ts_name_crc("rules.ini")
    print(f"looking for RULES.INI crc={target:08X}")
    for path in SCAN:
        if not os.path.isfile(path):
            continue
        try:
            f, entries = read_index(path)
        except Exception as e:
            print(f"[skip] {path}: {e}")
            continue
        if target in entries:
            off, sz = entries[target]
            f.seek(off)
            data = f.read(sz)
            out = path.replace("\\", "/").split("/")[-1] + "_rules.ini"
            with open(r"h:/OpenTS/OpenTS/" + out, "wb") as o:
                o.write(data)
            print(f"[found] {path}: offset={off} size={sz} -> {out}")
        else:
            print(f"[--] {os.path.basename(path)}: no rules.ini ({len(entries)} entries)")

if __name__ == "__main__":
    main()
