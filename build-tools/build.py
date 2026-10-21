#!/usr/bin/env python3
"""Build the TASOLLER Plus custom firmware.

    python build-tools/build.py                       run the tests, then build the custom firmware (C), build/fw/out/SS321_CFW.skystar
    python build-tools/build.py --release release     also publish release/SS321_CFW_SNV_<date>.skystar
    python build-tools/build.py --date 20261021       fix the date part of the version string

Needs the Arm GNU Toolchain (arm-none-eabi-gcc, -objcopy, -nm). It is looked for in
--toolchain, then in the ARM_GNU_TOOLCHAIN_BIN environment variable, then on PATH. Python 3.8 or
newer, no third-party packages.

The firmware is src/**/*.c, linked at 0x4000 with build-tools/fw.ld. --check
compares the result with the vendor's package.

The tests (build-tools/run_tests.py) run first; when they fail nothing is built or published. --release empties
its folder only once the build has succeeded.
"""

import argparse
import datetime
import glob
import hashlib
import os
import re
import shutil
import subprocess
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import skystar  # noqa: E402

VERSION_PREFIX = "SS321_CFW_SNV_"


class BuildError(Exception):
    pass


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


def write_bytes(path, data):
    with open(path, "wb") as f:
        f.write(data)


def find_tool(name, toolchain):
    exe = "arm-none-eabi-" + name
    if toolchain:
        path = shutil.which(exe, path=toolchain)
        if path:
            return path
        raise BuildError("%s not found in %s" % (exe, toolchain))
    env = os.environ.get("ARM_GNU_TOOLCHAIN_BIN")
    if env:
        path = shutil.which(exe, path=env)
        if path:
            return path
    path = shutil.which(exe)
    if not path:
        raise BuildError("%s not found: install the Arm GNU Toolchain and set ARM_GNU_TOOLCHAIN_BIN" % exe)
    return path


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise BuildError("%s failed:\n%s%s" % (os.path.basename(cmd[0]), r.stdout, r.stderr))
    return r.stdout


def version_string(date, tag):
    suffix = tag if tag else date
    version = VERSION_PREFIX + suffix
    # The device reports this string from a 40 byte field and the updater protocol carries it
    # in a short frame; keep it printable and well inside both.
    if not re.fullmatch(r"SS321_[A-Za-z0-9_]{1,30}", version) or len(version) > 39:
        raise BuildError("version %r must be SS321_ plus up to 30 letters, digits or underscores" % version)
    return version


CFLAGS = ["-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard", "-Os", "-ffreestanding",
          "-fno-builtin", "-fno-tree-loop-distribute-patterns", "-fno-strict-aliasing", "-fsigned-char",
          "-fwrapv", "-ffp-contract=off", "-w", "-fpermissive", "-ffunction-sections", "-fdata-sections"]


def build_c(args, version):
    gcc = find_tool("gcc", args.toolchain)
    objcopy = find_tool("objcopy", args.toolchain)
    out = os.path.abspath(args.out)
    objdir = os.path.join(out, "obj")
    outdir = os.path.join(out, "out")
    shutil.rmtree(out, ignore_errors=True)
    for d in (objdir, outdir):
        os.makedirs(d)
    sources = sorted(glob.glob(os.path.join(ROOT, "src", "**", "*.c"), recursive=True))
    include_dirs = [d for d, _, _ in os.walk(os.path.join(ROOT, "src"))]   # headers are included by name
    objs = []
    for src in sources:
        obj = os.path.join(objdir, os.path.splitext(os.path.basename(src))[0] + ".o")
        common = ["-include", "ss321.h"] if os.path.basename(src) != "data_init.c" else []   # its names clash with state.h
        run([gcc] + CFLAGS + [x for d in include_dirs for x in ("-I", d)]
            + common + ['-DFW_VERSION="%s"' % version, "-c", src, "-o", obj])
        objs.append(obj)
    elf = os.path.join(outdir, "app.elf")
    run([gcc] + CFLAGS[:4] + ["-nostdlib", "-nostartfiles", "-Wl,--gc-sections", "-Wl,-T," + os.path.join(TOOLS, "fw.ld"),
                              "-Wl,-Map," + os.path.join(outdir, "app.map"), "-o", elf] + objs + ["-lgcc"])
    image_path = os.path.join(outdir, "app.bin")
    run([objcopy, "-O", "binary", elf, image_path])
    return elf, image_path


def run_tests():
    """Run build-tools/run_tests.py; raise BuildError with its output when a test fails."""
    r = subprocess.run([sys.executable, "-I", os.path.join(TOOLS, "run_tests.py")], capture_output=True, text=True)
    if r.returncode:
        raise BuildError("tests failed:\n%s%s" % (r.stdout, r.stderr))
    print("tests   passed")


def build(args):
    if not args.skip_tests:
        run_tests()
    version = version_string(args.date or datetime.date.today().strftime("%Y%m%d"), args.tag)
    elf, image_path = build_c(args, version)
    return finish(args, os.path.abspath(args.out), elf, image_path, version)


def finish(args, out, elf, image_path, version):
    image = read_bytes(image_path)

    name = "SS321_CFW"
    package_path = os.path.join(out, "out", name + ".skystar")
    pkg_version = skystar.image_version(image)
    if version and pkg_version != version:
        raise BuildError("image reports %r, expected %r" % (pkg_version, version))
    package = skystar.pack(image, pkg_version)
    write_bytes(package_path, package)
    header, unpacked, problems = skystar.check(package)
    if problems or unpacked != image:
        raise BuildError("package check failed: %s" % problems)

    print("image   %6d bytes  sha256 %s" % (len(image), hashlib.sha256(image).hexdigest()))
    print("version %s" % pkg_version)
    print("package %s" % package_path)
    print("symbols %s" % elf)
    if args.release:
        shutil.rmtree(args.release, ignore_errors=True)
        os.makedirs(args.release)
        release_path = os.path.join(args.release, pkg_version + ".skystar")    # SS321_CFW_SNV_<date>.skystar
        shutil.copyfile(package_path, release_path)
        print("release %s" % release_path)

    if args.check:
        _, official = skystar.unpack(read_bytes(args.check))
        diffs = [i for i in range(min(len(image), len(official))) if image[i] != official[i]]
        print("differs from the official image in %d bytes (sizes %d and %d)" % (len(diffs), len(image), len(official)))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--date", help="date part of the version string, default today (YYYYMMDD)")
    ap.add_argument("--tag", help="replace the date part of the version string by this text")
    ap.add_argument("--out", help="output directory, default build/fw")
    ap.add_argument("--toolchain", help="directory holding arm-none-eabi-gcc and friends")
    ap.add_argument("--release", metavar="DIR", help="empty DIR and put the package there")
    ap.add_argument("--skip-tests", action="store_true", help="do not run the tests first")
    ap.add_argument("--check", metavar="OFFICIAL.skystar",
                    help="compare the result with the vendor's package")
    args = ap.parse_args(argv)
    if not args.out:
        args.out = os.path.join(ROOT, "build", "fw")
    try:
        return build(args)
    except (BuildError, skystar.PackageError) as exc:
        print("build failed: %s" % exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
