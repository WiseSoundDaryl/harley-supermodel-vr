"""Build the PSVR2 release from the extracted source archive on Windows."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--msys2", default=r"C:\msys64", type=Path)
    parser.add_argument("--jobs", type=int, choices=range(1, 17), default=4)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    toolchain = args.msys2.resolve()
    if os.name != "nt" or len(root.drive) != 2 or root.drive[1] != ":":
        sys.exit("Extract the source to a local Windows drive first.")
    manifest = json.loads((root / "SOURCE.json").read_text(encoding="utf-8"))
    for name, expected in manifest["buildInputs"].items():
        if sha256(root / name) != expected:
            sys.exit(f"Source archive verification failed for {name}")
    if any((root / name).exists() for name in ("obj64", "bin64", "BuildProfiles")):
        sys.exit("Extract into an empty folder for a fresh build.")
    binaries = toolchain / "mingw64/bin"
    shell = toolchain / "usr/bin/bash.exe"
    env = dict(os.environ, MSYSTEM="MINGW64", CHERE_INVOKING="1")
    env["SOURCE_DATE_EPOCH"] = str(manifest["linkEpoch"])
    env["PATH"] = os.pathsep.join([str(binaries), str(shell.parent), env["PATH"]])
    compiler = subprocess.check_output([binaries / "g++.exe", "-dumpfullversion"],
                                       env=env, text=True).strip()
    if compiler != "16.2.0":
        sys.exit(f"The supplied training profile requires GCC 16.2.0. Found {compiler}.")
    # GCC encodes the native absolute build path in the profile lookup name.
    profile = root.joinpath("BuildProfiles", root.drive[0].upper() + "~",
                            *root.parts[1:], "obj64#ppc.gcda")
    profile.parent.mkdir(parents=True)
    shutil.copyfile(root / "Profile/ppc.gcda", profile)
    command = ("make -f Makefiles/Makefile.Win32 -f Makefiles/HarleyVR.Profile.inc "
               f"-j{args.jobs} VERBOSE=1 bin64/supermodel.exe")
    subprocess.run([shell, "-lc", command], env=env, cwd=root, check=True)
    if sha256(root / "bin64/supermodel.exe") != manifest["referenceWindowsExecutable"]:
        sys.exit("The compiled executable differs from the release reference. Check the toolchain versions.")
    for name, expected in manifest["runtimeLibraries"].items():
        path = binaries / name
        if sha256(path) != expected:
            sys.exit(f"The installed {name} differs from the release dependency.")
        shutil.copyfile(path, root / "bin64" / name)
    print("Built", root / "bin64/supermodel.exe")


if __name__ == "__main__":
    main()
