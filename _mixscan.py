import struct, zlib, os, sys

MISSING = ["NONE.SHP", "BIGBLUE3.SHP"] + [f"BOXES{i:02d}.TEM" for i in range(1, 10)] + [f"ICE{i:02d}.TEM" for i in range(1, 6)]
CONTROL = ["C_SHADOW.SHP", "TREET01.TEM", "CONQUER.MIX"]
SCAN = [
    r"h:/OpenTS/OpenTS/Run/MIX/Temperat.mix",
    r"h:/OpenTS/OpenTS/Run/MIX/IsoTemp.mix",
    r"h:/OpenTS/OpenTS/Run/MIX/Snow.mix",
    r"h:/OpenTS/OpenTS/Run/MIX/IsoSnow.mix",
    r"h:/OpenTS/OpenTS/Run/MIX/Cache.mix",
    r"h:/OpenTS/OpenTS/Run/MIX/Conquer.mix",
    r"h:/OpenTS/OpenTS/Run/MIX/Local.mix",
    r"h:/OpenTS/OpenTS/Run/MIX/TibSun.MIX",
    r"h:/OpenTS/OpenTS/Run/TIBSUN.MIX",
    r"h:/OpenTS/OpenTS/Run/patch.mix",
    r"h:/OpenTS/OpenTS/Run/expand01.mix",
    r"h:/OpenTS/OpenTS/Run/expand02.mix",
    r"h:/OpenTS/OpenTS/Run/expand03.mix",
    r"h:/OpenTS/OpenTS/Run/multi.mix",
]

def read_index(path):
    try:
        with open(path, "rb") as f:
            data = f.read(6 + 2 + 4)
            if len(data) < 12:
                return None
            first, flags = struct.unpack_from("<HH", data, 0)
            count, size = struct.unpack_from("<HI", data, 4)
            if first != 0:
                # plain format: first short is actually the low half of count
                count = struct.unpack_from("<H", data, 0)[0]
                offset = 6
            else:
                if flags & 0x2:
                    return "encrypted"
                offset = 10
            entries = {}
            f.seek(offset)
            for _ in range(count):
                crc, off, sz = struct.unpack("<III", f.read(12))
                entries[crc] = (off, sz)
            return entries
    except OSError as e:
        return None

def main():
    index = {}
    for path in SCAN:
        name = os.path.basename(path)
        if not os.path.isfile(path):
            print(f"[skip] {name}: not present")
            continue
        result = read_index(path)
        if result is None:
            print(f"[skip] {name}: unreadable")
            continue
        if result == "encrypted":
            print(f"[skip] {name}: encrypted header")
            continue
        index[name] = result
        print(f"[ok]   {name}: {len(result)} entries")

    print()
    for name in MISSING + CONTROL:
        crc = zlib.crc32(name.upper().encode())
        where = [mix for mix, entries in index.items() if crc in entries]
        status = "FOUND: " + ", ".join(where) if where else "not in any scanned mix"
        print(f"{name:16} crc={crc & 0xFFFFFFFF:08X}  {status}")

if __name__ == "__main__":
    main()
