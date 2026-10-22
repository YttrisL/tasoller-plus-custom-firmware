#!/usr/bin/env python3
"""Tests for the build and the package tooling.

    python -I build-tools/run_tests.py

Tests that need the vendor's package (TSLP_Main_APP_050_20250102.skystar) look for it in the
TASOLLER_OFFICIAL_PKG environment variable, then in a few usual places, and are skipped when it is
missing. Tests that build need the Arm GNU Toolchain (see build-tools/build.py) and are skipped without it.
"""

import contextlib
import io
import os
import struct
import subprocess
import sys
import tempfile
import unittest

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)

import build  # noqa: E402
import skystar  # noqa: E402

OFFICIAL_NAME = "TSLP_Main_APP_050_20250102.skystar"
IMAGE_BASE = 0x4000
NUL = bytes(1)


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


def find_official():
    candidates = [os.environ.get("TASOLLER_OFFICIAL_PKG", "")]
    for up in (ROOT, os.path.dirname(ROOT)):
        candidates.append(os.path.join(up, OFFICIAL_NAME))
        candidates.append(os.path.join(up, "official-firmware", OFFICIAL_NAME))
        candidates.append(os.path.join(up, "tasoller", "TASOLLER-plus-option-V2.8", OFFICIAL_NAME))
    for c in candidates:
        if c and os.path.isfile(c):
            return c
    return None


def have_toolchain():
    try:
        for tool in ("gcc", "objcopy", "nm"):
            build.find_tool(tool, None)
        return True
    except build.BuildError:
        return False


OFFICIAL = find_official()
TOOLCHAIN = have_toolchain()


def run_build(out, *extra):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = build.main(["--skip-tests", "--out", out] + list(extra))
    return rc, buf.getvalue()


def symbols(elf):
    out = subprocess.run([build.find_tool("nm", None), elf], capture_output=True, text=True, check=True).stdout
    table = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            table[parts[2]] = int(parts[0], 16)
    return table


class AesTest(unittest.TestCase):
    def test_fips197_vector(self):
        key = bytes(range(16))
        plain = bytes.fromhex("00112233445566778899aabbccddeeff")
        cipher = bytes.fromhex("69c4e0d86a7b0430d8cdb78070b4c55a")
        self.assertEqual(skystar.aes_ecb(plain, True, key), cipher)
        self.assertEqual(skystar.aes_ecb(cipher, False, key), plain)


class PackageTest(unittest.TestCase):
    def test_round_trip(self):
        image = bytes(range(256)) * 10
        image = image[:0x400] + b"SS321_TEST_1".ljust(0x28, NUL) + image[0x428:]
        package = skystar.pack(image, "SS321_TEST_1")
        self.assertEqual(len(package), skystar.PACKAGE_SIZE)
        header, back = skystar.unpack(package)
        self.assertEqual(back, image)
        self.assertEqual(header["Version"], "SS321_TEST_1")
        self.assertEqual(header["bin_len"], str(len(image)))
        self.assertEqual(skystar.check(package)[2], [])

    def test_rejects_wrong_size(self):
        with self.assertRaises(skystar.PackageError):
            skystar.unpack(bytes(1024))

    def test_rejects_oversized_image(self):
        with self.assertRaises(skystar.PackageError):
            skystar.pack(b"\x01" * (skystar.HEADER_SIZE + 1), "SS321_X")

    def test_check_flags_version_mismatch(self):
        image = bytes(0x400) + b"SS321_A".ljust(0x28, NUL)
        package = skystar.pack(image, "SS321_B")
        self.assertTrue(skystar.check(package)[2])

    @unittest.skipUnless(OFFICIAL, "official package not found")
    def test_official_package_round_trips_byte_exact(self):
        package = read_bytes(OFFICIAL)
        header, image = skystar.unpack(package)
        self.assertEqual(header["Version"], "SS321_APP_050_20250102")
        self.assertEqual(skystar.pack(image, header["Version"]), package)


class VersionStringTest(unittest.TestCase):
    def test_default_shape(self):
        self.assertEqual(build.version_string("20261021", None), "SS321_CFW_SNV_20261021")

    def test_tag_replaces_date(self):
        self.assertEqual(build.version_string("20261021", "TEST1"), "SS321_CFW_SNV_TEST1")

    def test_rejects_bad_text(self):
        for bad in ("a b", "x" * 40, "caf" + chr(233)):
            with self.assertRaises(build.BuildError):
                build.version_string("20261021", bad)


@unittest.skipUnless(TOOLCHAIN, "Arm GNU Toolchain not found")
class CBuildTest(unittest.TestCase):
    """The C firmware (src/)."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.out = os.path.join(cls.tmp.name, "fw")
        rc, text = run_build(cls.out, "--date", "20261021")
        assert rc == 0, text
        cls.pkg = read_bytes(os.path.join(cls.out, "out", "SS321_CFW.skystar"))
        cls.header, cls.image = skystar.unpack(cls.pkg)
        cls.syms = symbols(os.path.join(cls.out, "out", "app.elf"))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_package_is_acceptable_to_the_updater(self):
        header, image, problems = skystar.check(self.pkg)
        self.assertEqual(problems, [])
        self.assertEqual(header["Version"], "SS321_CFW_SNV_20261021")
        self.assertEqual(header["Type"], "APP")
        self.assertLessEqual(len(image), skystar.HEADER_SIZE)

    def test_image_reports_the_new_version(self):
        self.assertEqual(skystar.image_version(self.image), "SS321_CFW_SNV_20261021")
        self.assertEqual(self.syms["gVersionString"] - IMAGE_BASE, skystar.VERSION_OFFSET)
        field = self.image[skystar.VERSION_OFFSET:skystar.VERSION_OFFSET + 0x28]
        self.assertEqual(field, b"SS321_CFW_SNV_20261021".ljust(0x28, NUL))

    def test_name_descriptor_says_supernova(self):
        text = "Supernova"
        descriptor = bytes([2 + 2 * len(text), 3]) + text.encode("utf-16-le")
        self.assertEqual(self.image.count(descriptor), 1)
        self.assertNotIn("Sky Star".encode("utf-16-le"), self.image)

    def test_webusb_landing_page(self):
        url = b"github.com/YttrisL/tasoller-plus-custom-firmware"
        descriptor = bytes([3 + len(url), 3, 1]) + url
        self.assertEqual(self.image.count(descriptor), 1)
        self.assertNotIn(b"www.gamo2.com", self.image)

    def test_io4_descriptors_are_consistent(self):
        """The USB descriptors of the IO4-Emulation personality: lengths add up, the endpoints are the ones the firmware sets up."""
        def blob(name, size):
            at = self.syms[name] - IMAGE_BASE
            return self.image[at:at + size]
        device = blob("io4_device_descriptor", 18)
        self.assertEqual(struct.unpack_from("<HH", device, 8), (0x0CA3, 0x0021))      # the ID of the real I/O board
        config = blob("io4_config_descriptor", 9)
        config = blob("io4_config_descriptor", struct.unpack_from("<H", config, 2)[0])
        report = blob("io4_report_descriptor", 0x71)
        i, endpoints, interfaces, hid_lengths = 0, [], [], []
        while i < len(config):
            length, kind = config[i], config[i + 1]
            self.assertGreater(length, 1)
            if kind == 4:
                interfaces.append(config[i + 2])
            elif kind == 5:
                endpoints.append(config[i + 2])
            elif kind == 0x21:
                hid_lengths.append(struct.unpack_from("<H", config, i + 7)[0])
            i += length
        self.assertEqual(i, len(config))
        self.assertEqual(config[4], len(interfaces))
        self.assertEqual(interfaces, [0, 1, 2, 3, 4, 5, 6, 7])
        # the command channel of the other modes (interface 1: IN 5, OUT 6), the I/O board, the slider's serial port and its
        # notifications, the two LED boards; the endpoint numbers are all different (the controller finds an endpoint by its number)
        self.assertEqual(sorted(endpoints), sorted([0x84, 0x85, 0x06, 0x81, 0x03, 0x87, 0x88, 0x02, 0x8A, 0x09]))
        self.assertEqual(len({e & 15 for e in endpoints}), len(endpoints))
        self.assertEqual(hid_lengths, [len(report), 0x1F])
        j = 0
        while j < len(report):                  # the report descriptor is made of whole items
            j += 1 + {0: 0, 1: 1, 2: 2, 3: 4}[report[j] & 3]
        self.assertEqual(j, len(report))

    def test_io4_static_light_descriptors(self):
        """The configuration with the static light of the air towers (no LED boards, one vendor interface) and the Microsoft OS 2.0 sets."""
        def blob(name, size):
            at = self.syms[name] - IMAGE_BASE
            return self.image[at:at + size]
        static_device = blob("io4_device_static_descriptor", 18)
        device = blob("io4_device_descriptor", 18)
        self.assertEqual(static_device[:12], device[:12])                      # the same USB ID ...
        self.assertEqual(static_device[14:], device[14:])
        self.assertNotEqual(static_device[12:14], device[12:14])               # ... under another device version
        config = blob("io4_config_static_descriptor", 9)
        config = blob("io4_config_static_descriptor", struct.unpack_from("<H", config, 2)[0])
        i, endpoints, interfaces = 0, [], []
        while i < len(config):
            length, kind = config[i], config[i + 1]
            self.assertGreater(length, 1)
            if kind == 4:
                interfaces.append(config[i + 2])
            elif kind == 5:
                endpoints.append(config[i + 2])
            i += length
        self.assertEqual(i, len(config))
        self.assertEqual(config[4], len(interfaces))
        self.assertEqual(interfaces, [0, 1, 2, 3, 4])
        self.assertEqual(sorted(endpoints), sorted([0x84, 0x85, 0x06, 0x81, 0x03, 0x87, 0x02]))
        self.assertEqual(len({e & 15 for e in endpoints}), len(endpoints))
        for name, size in (("io4_ms_os_descriptor", 156), ("io4_ms_os_static_descriptor", 224)):
            ms = blob(name, size)
            self.assertEqual(struct.unpack_from("<H", ms, 8)[0], size)        # the total length of the set
            self.assertEqual(struct.unpack_from("<H", ms, 16)[0], size - 10)   # that of the configuration subset
            j = 18
            while j < size:                                                   # function subsets that add up
                self.assertEqual(struct.unpack_from("<H", ms, j)[0], 8)
                self.assertEqual(struct.unpack_from("<H", ms, j + 2)[0], 2)
                j += struct.unpack_from("<H", ms, j + 6)[0]
            self.assertEqual(j, size)
        for name, size in (("io4_bos_descriptor", 156), ("io4_bos_static_descriptor", 224)):
            bos = blob(name, 33)
            self.assertEqual(struct.unpack_from("<H", bos, 2)[0], 33)
            self.assertEqual(struct.unpack_from("<H", bos, 29)[0], size)      # the length of the set it announces

    def test_vector_table(self):
        words = struct.unpack_from("<110I", self.image, 0)
        self.assertEqual(words[0], 0x2001ED48)
        self.assertEqual(words[1], self.syms["Reset_Handler"] | 1)
        self.assertEqual(words[3], self.syms["HardFault_Handler"] | 1)

    @unittest.skipUnless(OFFICIAL, "official package not found")
    def test_initialised_data_is_the_official_one_apart_from_the_name_and_code_addresses(self):
        _, official = skystar.unpack(read_bytes(OFFICIAL))
        stock = official[0xEF78 - IMAGE_BASE:0xEF78 - IMAGE_BASE + 0x1024]
        base = self.syms["gDataInit"] - IMAGE_BASE
        ours = self.image[base:base + 0x1024]
        name = 0xF100 - 0xEF78
        url = 0x174                 # the WebUSB landing page moved to flash
        for i in range(0, 0x1024, 4):
            a, b = struct.unpack_from("<I", stock, i)[0], struct.unpack_from("<I", ours, i)[0]
            if a == b or name <= i < name + 0x80 or url <= i < url + 16:
                continue
            self.assertTrue(a & 1 and 0x4000 <= a < 0xF000 and (b == 0 or (0x4000 <= b < 0x10000 and b & 1)),
                            "data word 0x%x: official 0x%x, ours 0x%x" % (i, a, b))

    @unittest.skipUnless(OFFICIAL, "official package not found")
    def test_tables_are_the_official_ones(self):
        _, official = skystar.unpack(read_bytes(OFFICIAL))
        for symbol, address, size in (("aes_sbox_rom", 0xECE4, 0x100), ("aes_inv_sbox_rom", 0xEDE4, 0x100)):
            off = self.syms[symbol] - IMAGE_BASE
            self.assertEqual(self.image[off:off + size], official[address - IMAGE_BASE:address - IMAGE_BASE + size])

    def test_no_ram_variables_are_defined(self):
        for name in ("_edata", "_end", "__bss_start__"):
            self.assertNotIn(name, self.syms)


class HygieneTest(unittest.TestCase):
    def test_text_files_are_plain_ascii_with_lf(self):
        skipped = {".git", "build", "__pycache__"}
        bad = []
        for base, dirs, files in os.walk(ROOT):
            dirs[:] = [d for d in dirs if d not in skipped]
            for name in files:
                if name.endswith((".skystar", ".bin", ".pyc", ".elf", ".o")):
                    continue
                data = read_bytes(os.path.join(base, name))
                if b"\r" in data or any(b > 0x7E or (b < 0x20 and b not in (9, 10)) for b in data):
                    bad.append(os.path.relpath(os.path.join(base, name), ROOT))
        self.assertEqual(bad, [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
