"""Assemble unicode_shim.s and update the SHIM bytes embedded in patch_exe.py.

Needs GNU binutils that can target i386 (`as --32`, `objcopy`), e.g. MinGW-w64.
Usage: python build_shim.py
"""
import os
import re
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    with tempfile.TemporaryDirectory() as tmp:
        obj = os.path.join(tmp, "unicode_shim.o")
        binary = os.path.join(HERE, "tests", "unicode_shim.bin")
        subprocess.run(["as", "--32", os.path.join(HERE, "unicode_shim.s"), "-o", obj], check=True)
        relocs = subprocess.run(["objdump", "-r", obj], check=True, capture_output=True, text=True).stdout
        if "RELOCATION RECORDS" in relocs:
            raise SystemExit("unicode_shim.s must not need relocations:\n" + relocs)
        subprocess.run(["objcopy", "-O", "binary", "-j", ".text", obj, binary], check=True)
        data = open(binary, "rb").read()

    if len(data) > 0x4E3000 - 0x4E25A0:
        raise SystemExit(f"shim is {len(data)} bytes, too big for the .text padding")
    hexstr = data.hex()
    lines = "\n".join(f'    "{hexstr[i:i + 96]}"' for i in range(0, len(hexstr), 96))
    path = os.path.join(HERE, "patch_exe.py")
    src = open(path).read()
    src = re.sub(r"SHIM = bytes\.fromhex\(\n.*?\n\)", lambda m: f"SHIM = bytes.fromhex(\n{lines}\n)", src, flags=re.S)
    open(path, "w").write(src)
    print(f"shim: {len(data)} bytes, written to patch_exe.py and tests/unicode_shim.bin")


if __name__ == "__main__":
    main()
