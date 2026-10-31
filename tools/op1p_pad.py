#!/usr/bin/env python3
"""Pad an .op1p to a given file length and memory size: op1p_pad.py in.op1p out.op1p FILE_LEN [MEM_SIZE]

The OP-1 loader reads the whole file into a heap block and allocates the plugin's memory (image + bss) while that
block is still there (plugload.c, load_one), so where the plugin lands, and everything allocated after it, follows
the file's length and the memory size. Two builds of the same length and size load at the same addresses (with the
same plugins before them). The file grows by NULs after the SVG (the format allows them: op1p_load.c, "SVG text +
NUL + padding"); the memory by a larger bss (the header's bss_size; the loader zeroes it). Code and data untouched."""
import struct, sys, zlib

def main():
    src, dst, flen = sys.argv[1], sys.argv[2], int(sys.argv[3])
    d = bytearray(open(src, 'rb').read())
    tmagic, head, blen, _ = struct.unpack('<4I', d[-16:])
    body = d[head:head + blen]
    image, bss = struct.unpack_from('<2I', body, 32)            # op1p_hdr: 4I, name[16], image_size, bss_size
    if len(sys.argv) > 4:
        mem = int(sys.argv[4])
        assert mem >= image + bss, f'memory {mem} < image {image} + bss {bss}'
        struct.pack_into('<I', body, 36, mem - image)
    extra = flen - len(d)
    assert extra >= 0 and extra % 4 == 0, f'file {len(d)} B -> {flen} B: must grow by a multiple of 4'
    assert head and d[head - 1] == 0, 'no SVG head to pad'
    out = d[:head] + bytes(extra) + body + struct.pack('<4I', tmagic, head + extra, blen, zlib.crc32(bytes(body)))
    open(dst, 'wb').write(out)
    print(f'{dst}: {len(out)} B, memory {sum(struct.unpack_from("<2I", body, 32))} B')

if __name__ == '__main__':
    main()
