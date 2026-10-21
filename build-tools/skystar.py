#!/usr/bin/env python3
"""Pack, unpack and verify TASOLLER Plus .skystar firmware packages.

A package is AES-128-ECB encrypted. Decrypted it is exactly 0x4B000 bytes:

    0x00000  0x19000  JSON header, NUL padded
    0x19000  0x32000  application image, NUL padded

The official updater only accepts a package of that size whose header is JSON with a
"bin_len" that fits in the first region. The device itself reports the version string it
finds at offset 0x400 of the image, and the updater compares the part before the first
underscore ("SS321") with the one of the package's "Version".

Pure Python, no third-party packages.

    skystar.py info    PACKAGE
    skystar.py unpack  PACKAGE IMAGE_OUT
    skystar.py pack    IMAGE PACKAGE_OUT --version SS321_CFW_SNV_20261021
    skystar.py verify  PACKAGE
"""

import argparse
import json
import sys

KEY = bytes.fromhex("07080C4336575936200C090000000000")

PACKAGE_SIZE = 0x4B000
HEADER_SIZE = 0x19000
IMAGE_SIZE = 0x32000
VERSION_OFFSET = 0x400
VERSION_FIELD = 0x28  # bytes between the version string and the first instruction

# ---------------------------------------------------------------------------------------
# AES-128 (FIPS 197), ECB only.
# ---------------------------------------------------------------------------------------


def _build_sbox():
    sbox = [0] * 256
    p = q = 1
    while True:
        # multiply p by 3 and divide q by 3 in GF(2^8)
        p = p ^ ((p << 1) & 0xFF) ^ (0x1B if p & 0x80 else 0)
        q ^= q << 1
        q ^= q << 2
        q ^= q << 4
        q &= 0xFF
        if q & 0x80:
            q ^= 0x09
        x = q ^ (q << 1 | q >> 7) & 0xFF ^ (q << 2 | q >> 6) & 0xFF
        x ^= (q << 3 | q >> 5) & 0xFF ^ (q << 4 | q >> 4) & 0xFF
        sbox[p] = (x ^ 0x63) & 0xFF
        if p == 1:
            break
    sbox[0] = 0x63
    return sbox


SBOX = _build_sbox()
INV_SBOX = [0] * 256
for _i, _v in enumerate(SBOX):
    INV_SBOX[_v] = _i


def _xtime(a):
    return ((a << 1) ^ 0x1B) & 0xFF if a & 0x80 else a << 1


def _mul(a, b):
    r = 0
    while b:
        if b & 1:
            r ^= a
        a = _xtime(a)
        b >>= 1
    return r


def _expand_key(key):
    w = list(key)
    rcon = 1
    for i in range(16, 176, 4):
        t = w[i - 4:i]
        if i % 16 == 0:
            t = [SBOX[t[1]] ^ rcon, SBOX[t[2]], SBOX[t[3]], SBOX[t[0]]]
            rcon = _xtime(rcon)
        w += [w[i - 16 + j] ^ t[j] for j in range(4)]
    return [w[r * 16:(r + 1) * 16] for r in range(11)]


def _encrypt_block(rk, block):
    s = [b ^ k for b, k in zip(block, rk[0])]
    for rnd in range(1, 11):
        s = [SBOX[b] for b in s]
        # ShiftRows (state is column major)
        s = [s[(i + 4 * (i % 4)) % 16] for i in range(16)]
        if rnd != 10:
            t = []
            for c in range(4):
                a = s[4 * c:4 * c + 4]
                t += [
                    _mul(a[0], 2) ^ _mul(a[1], 3) ^ a[2] ^ a[3],
                    a[0] ^ _mul(a[1], 2) ^ _mul(a[2], 3) ^ a[3],
                    a[0] ^ a[1] ^ _mul(a[2], 2) ^ _mul(a[3], 3),
                    _mul(a[0], 3) ^ a[1] ^ a[2] ^ _mul(a[3], 2),
                ]
            s = t
        s = [b ^ k for b, k in zip(s, rk[rnd])]
    return bytes(s)


def _decrypt_block(rk, block):
    s = [b ^ k for b, k in zip(block, rk[10])]
    for rnd in range(9, -1, -1):
        # InvShiftRows
        s = [s[(i - 4 * (i % 4)) % 16] for i in range(16)]
        s = [INV_SBOX[b] for b in s]
        s = [b ^ k for b, k in zip(s, rk[rnd])]
        if rnd != 0:
            t = []
            for c in range(4):
                a = s[4 * c:4 * c + 4]
                t += [
                    _mul(a[0], 14) ^ _mul(a[1], 11) ^ _mul(a[2], 13) ^ _mul(a[3], 9),
                    _mul(a[0], 9) ^ _mul(a[1], 14) ^ _mul(a[2], 11) ^ _mul(a[3], 13),
                    _mul(a[0], 13) ^ _mul(a[1], 9) ^ _mul(a[2], 14) ^ _mul(a[3], 11),
                    _mul(a[0], 11) ^ _mul(a[1], 13) ^ _mul(a[2], 9) ^ _mul(a[3], 14),
                ]
            s = t
    return bytes(s)


def aes_ecb(data, encrypt, key=KEY):
    if len(data) % 16:
        raise ValueError("length must be a multiple of 16")
    rk = _expand_key(key)
    fn = _encrypt_block if encrypt else _decrypt_block
    # ECB: equal blocks give equal results, and the padding makes most blocks equal.
    cache = {}
    out = []
    for i in range(0, len(data), 16):
        block = data[i:i + 16]
        res = cache.get(block)
        if res is None:
            res = cache[block] = fn(rk, block)
        out.append(res)
    return b"".join(out)


# ---------------------------------------------------------------------------------------
# Package handling
# ---------------------------------------------------------------------------------------


class PackageError(Exception):
    pass


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


def write_bytes(path, data):
    with open(path, "wb") as f:
        f.write(data)


def make_header(version, image_len):
    # The key order is the one the vendor uses.
    fields = {
        "CLog": "",
        "ELog": "",
        "JLog": "",
        "Type": "APP",
        "Version": version,
        "bin_len": str(image_len),
    }
    raw = json.dumps(fields, separators=(",", ":")).encode("ascii")
    if len(raw) > HEADER_SIZE:
        raise PackageError("header too large")
    return raw


def pack(image, version):
    if not image or len(image) > HEADER_SIZE:
        raise PackageError("image must be 1..0x19000 bytes, got %d" % len(image))
    plain = make_header(version, len(image)).ljust(HEADER_SIZE, b"\0")
    plain += image.ljust(IMAGE_SIZE, b"\0")
    assert len(plain) == PACKAGE_SIZE
    return aes_ecb(plain, True)


def unpack(package):
    """Return (header dict, image bytes of bin_len)."""
    if len(package) != PACKAGE_SIZE:
        raise PackageError("package must be 0x%X bytes, got 0x%X" % (PACKAGE_SIZE, len(package)))
    plain = aes_ecb(package, False)
    head = plain[:HEADER_SIZE].rstrip(b"\0")
    try:
        header = json.loads(head.decode("ascii"))
        bin_len = int(header["bin_len"])
    except (ValueError, KeyError) as exc:
        raise PackageError("bad header (wrong key or not a package): %s" % exc)
    if not 0 < bin_len <= HEADER_SIZE:
        raise PackageError("bin_len out of range: %d" % bin_len)
    if plain[HEADER_SIZE + bin_len:].strip(b"\0"):
        raise PackageError("data after the image")
    return header, plain[HEADER_SIZE:HEADER_SIZE + bin_len]


def image_version(image):
    """The version string the running firmware reports, taken from offset 0x400."""
    field = image[VERSION_OFFSET:VERSION_OFFSET + VERSION_FIELD]
    return field.split(b"\0", 1)[0].decode("ascii")


def check(package):
    """Apply the rules the official updater applies. Returns (header, image, problems)."""
    header, image = unpack(package)
    problems = []
    version = header.get("Version", "")
    if version.split("_")[0] != "SS321":
        problems.append("header Version %r does not start with SS321_" % version)
    if header.get("CLog"):
        problems.append("CLog must be empty")
    if image_version(image) != version:
        problems.append("header Version %r differs from the image's own %r" % (version, image_version(image)))
    return header, image, problems


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("info")
    p.add_argument("package")
    p = sub.add_parser("unpack")
    p.add_argument("package")
    p.add_argument("image")
    p = sub.add_parser("pack")
    p.add_argument("image")
    p.add_argument("package")
    p.add_argument("--version")
    p = sub.add_parser("verify")
    p.add_argument("package")
    args = ap.parse_args(argv)

    try:
        if args.cmd in ("info", "verify"):
            header, image, problems = check(read_bytes(args.package))
            print(json.dumps(header))
            print("image: %d bytes, reports %r" % (len(image), image_version(image)))
            for msg in problems:
                print("PROBLEM:", msg)
            return 1 if problems and args.cmd == "verify" else 0
        if args.cmd == "unpack":
            header, image = unpack(read_bytes(args.package))
            write_bytes(args.image, image)
            print(json.dumps(header))
            return 0
        if args.cmd == "pack":
            image = read_bytes(args.image)
            version = args.version or image_version(image)
            write_bytes(args.package, pack(image, version))
            print("wrote %s (%s)" % (args.package, version))
            return 0
    except PackageError as exc:
        print("error:", exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
