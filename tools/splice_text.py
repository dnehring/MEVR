"""Make a disassemblable copy of a Mass Effect LE exe from the mod's decrypted .text dump.

The exe's .text is encrypted on disk (EA DRM). The mod saves the decrypted section once per exe
build (engine/code_dump, %LOCALAPPDATA%\\MEVR\\dump). This writes a copy of the exe with that
section spliced back in, for local research with standard tools, e.g.:

    python -I tools/splice_text.py <MassEffect1.exe> <dump.text.bin> <out.exe>
    dumpbin /disasm /range:0x1404C5300,0x1404C5400 <out.exe>

Local research data only: never commit or share the output. Read-only on the inputs.
"""
import struct
import sys


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    exe_path, dump_path, out_path = sys.argv[1:]
    image = bytearray(open(exe_path, 'rb').read())
    dump = open(dump_path, 'rb').read()

    pe = struct.unpack_from('<I', image, 0x3C)[0]
    count = struct.unpack_from('<H', image, pe + 6)[0]
    timestamp = struct.unpack_from('<I', image, pe + 8)[0]
    optional_size = struct.unpack_from('<H', image, pe + 20)[0]
    table = pe + 24 + optional_size
    for i in range(count):
        entry = table + i * 40
        if image[entry:entry + 8].rstrip(b'\0') != b'.text':
            continue
        virtual_size, rva, raw_size, raw_offset = struct.unpack_from('<IIII', image, entry + 8)
        if len(dump) != virtual_size:
            sys.exit(f'dump is {len(dump):#x} bytes but .text is {virtual_size:#x}: wrong exe build?')
        size = min(raw_size, virtual_size)
        image[raw_offset:raw_offset + size] = dump[:size]
        open(out_path, 'wb').write(image)
        print(f'exe timestamp 0x{timestamp:08X}; spliced {size:#x} bytes at .text (rva {rva:#x}) -> {out_path}')
        return
    sys.exit('no .text section')


if __name__ == '__main__':
    main()
