#!/usr/bin/env python3
"""
v11 vendor_boot repack tool.

Replaces lib/modules/unisoc-sched.ko and lib/modules/unisoc_last_kmsg.ko inside
the vendor_boot ramdisk (concatenated newc cpio archives, lz4-legacy
compressed), recompresses, and rebuilds the vendor_boot image with an updated
vendor_ramdisk_size header field. Everything else is preserved byte-identical.

Usage:
  repack_vendor_boot.py <orig_vendor_boot.bin> <new_unisoc-sched.ko> \
      <new_unisoc_last_kmsg.ko> <output.img> [lz4_binary]

Requires the lz4 CLI (default: "lz4") for legacy compress/decompress.
"""
import os
import struct
import subprocess
import sys
import tempfile

PAGE = 4096
TOTAL_SIZE = 104857600  # 100 MiB partition size
REPLACE = {
    "lib/modules/unisoc-sched.ko": None,
    "lib/modules/unisoc_last_kmsg.ko": None,
}


def run_lz4(lz4bin, args, data=None):
    """Run lz4 CLI with data on stdin, return stdout as bytes (always uses -c)."""
    r = subprocess.run([lz4bin] + args + ["-c"], input=data or b"",
                       capture_output=True)
    if r.returncode != 0:
        raise RuntimeError("lz4 failed: " + r.stderr.decode("utf-8", "replace").strip())
    return r.stdout


HEXDIGITS = set(b"0123456789abcdefABCDEF")
# 合法 cpio 路径名字符集：可打印 ASCII（含 '[' '!' 等，如 toybox 的 system/bin/[ symlink）
NAME_CHARS = set(range(0x20, 0x7F))


def parse_entry(buf, pos):
    """Parse a newc cpio entry at pos. Returns dict or None if not an entry.

    严格校验，防止 .ko 二进制数据中伪 "070701" magic 把顺序 walker 带偏：
    1. magic 之后 104 字节必须全是 ASCII hex（真 cpio 头的 13 个字段都是 8 hex）
    2. namesize/filesize 合理
    3. name 必须是可打印路径字符
    """
    if buf[pos:pos + 6] != b"070701":
        return None
    if pos + 110 > len(buf):
        return None
    hdr = buf[pos:pos + 110]
    if not all(c in HEXDIGITS for c in hdr[6:110]):
        return None
    try:
        namesize = int(hdr[94:102], 16)  # c_namesize (abs 94..101)
        filesize = int(hdr[54:62], 16)   # c_filesize (abs 54..61)
    except ValueError:
        return None
    if not (1 <= namesize <= 4096) or filesize > 200 * 1024 * 1024:
        return None
    name_start = pos + 110
    name_end = name_start + namesize
    if name_end > len(buf):
        return None
    raw = buf[name_start:name_end - 1]
    if not raw or not all(c in NAME_CHARS for c in raw):
        return None
    try:
        name = raw.decode("ascii")
    except UnicodeDecodeError:
        return None
    # newc 规则: 固定头(110) + 路径名 总长补齐到 4 的倍数（不是 namesize 本身！）
    # 头部起始位置恒为 4 对齐（从偏移 0 开始归纳保持），故用绝对偏移计算最稳
    name_pad = (-(pos + 110 + namesize)) % 4
    data_start = name_end + name_pad
    data_end = data_start + filesize
    if data_end > len(buf):
        return None
    data_pad = (-filesize) % 4
    total = 110 + namesize + name_pad + filesize + data_pad
    return {
        "hdr": hdr, "name": name, "name_bytes": buf[name_start:name_end],
        "name_pad": name_pad, "data": buf[data_start:data_end],
        "data_pad": data_pad, "total": total,
    }


def rebuild_cpio(buf, replacements, stats):
    """Walk concatenated cpio archives, replacing module data in-place."""
    out = bytearray()
    pos = 0
    n = len(buf)
    while pos < n:
        idx = buf.find(b"070701", pos)
        if idx < 0:
            out += buf[pos:]
            break
        out += buf[pos:idx]  # verbatim gap (padding/zeros between archives)
        entry = parse_entry(buf, idx)
        if entry is None:
            out += b"070701"  # false positive inside file data
            pos = idx + 6
            continue
        if entry["name"] in replacements:
            new_data = replacements[entry["name"]]
            hdr = bytearray(entry["hdr"])
            # c_filesize at entry offset 54..61, 8 hex chars, keep case style
            old_field = entry["hdr"][54:62]
            style = "X" if any(c in b"ABCDEF" for c in old_field) else "x"
            field = ("%08" + style) % len(new_data)
            if len(field) != 8:
                sys.exit(f"ERROR: new filesize {len(new_data)} does not fit 8 hex chars")
            hdr[54:62] = field.encode("ascii")
            out += bytes(hdr)
            out += entry["name_bytes"]
            out += b"\x00" * entry["name_pad"]
            out += new_data
            out += b"\x00" * ((-len(new_data)) % 4)
            stats[entry["name"]] = (entry["total"], len(new_data))
        else:
            out += buf[idx:idx + entry["total"]]
        pos = idx + entry["total"]
    return bytes(out)


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        sys.exit(1)
    orig_path, sched_ko, lastkmsg_ko, out_path = sys.argv[1:5]
    lz4bin = sys.argv[5] if len(sys.argv) > 5 else "lz4"

    orig = open(orig_path, "rb").read()
    if orig[:8] != b"VNDRBOOT":
        sys.exit("not a vendor_boot image")
    ramdisk_size, = struct.unpack_from("<I", orig, 24)
    print(f"orig vendor_ramdisk_size = {ramdisk_size}")

    ramdisk_lz4 = orig[PAGE:PAGE + ramdisk_size]
    cpio = run_lz4(lz4bin, ["-d", "-l", "-f"], ramdisk_lz4)
    print(f"ramdisk decompressed: {len(cpio)} bytes")

    replacements = dict(REPLACE)
    replacements["lib/modules/unisoc-sched.ko"] = open(sched_ko, "rb").read()
    replacements["lib/modules/unisoc_last_kmsg.ko"] = open(lastkmsg_ko, "rb").read()

    stats = {}
    new_cpio = rebuild_cpio(cpio, replacements, stats)
    for k, (old_sz, new_sz) in stats.items():
        print(f"replaced {k}: {old_sz} -> {new_sz} bytes")
    missing = [k for k in replacements if k not in stats]
    if missing:
        sys.exit(f"ERROR: modules not found in ramdisk: {missing}")

    new_lz4 = run_lz4(lz4bin, ["-9", "-l", "-f"], new_cpio)
    print(f"recompressed: {len(new_lz4)} bytes (was {ramdisk_size})")

    # rebuild image:
    #   [header page][new ramdisk lz4][page pad][middle: dtb/table/bootconfig/AVB blob]
    #   [zero fill][last page: AVB footer verbatim]
    # The bootloader derives dtb/table/bootconfig positions sequentially after the
    # page-padded ramdisk, so a whole-page shift of the middle is safe.
    header = bytearray(orig[:PAGE])
    struct.pack_into("<I", header, 24, len(new_lz4))
    rd_pad_end = (PAGE + ramdisk_size + PAGE - 1) // PAGE * PAGE
    footer = bytearray(orig[-PAGE:])            # AVB footer page (magic "AVBf" at end)
    middle = bytearray(orig[rd_pad_end:len(orig) - PAGE])
    # trim trailing zero padding of the middle (between AVB blob end and footer);
    # everything up to the last non-zero byte is preserved verbatim
    m_end = len(middle)
    while m_end > 0 and middle[m_end - 1] == 0:
        m_end -= 1
    middle = middle[:m_end]
    print(f"middle content: {len(middle)} bytes (trimmed from "
          f"{len(orig) - PAGE - rd_pad_end})")
    # update stale ramdisk_size references (vendor ramdisk table entry)
    old_le = struct.pack("<I", ramdisk_size)
    new_le = struct.pack("<I", len(new_lz4))
    patched = 0
    for region in (middle, footer):
        i = 0
        while True:
            j = region.find(old_le, i)
            if j < 0:
                break
            region[j:j + 4] = new_le
            patched += 1
            i = j + 4
    print(f"size references patched: {patched}")

    pad = (-(PAGE + len(new_lz4))) % PAGE
    head_len = PAGE + len(new_lz4) + pad + len(middle)
    if head_len > TOTAL_SIZE - PAGE:
        sys.exit(f"ERROR: content too large: {head_len} + footer > {TOTAL_SIZE}")
    gap = TOTAL_SIZE - PAGE - head_len
    out = bytes(header) + new_lz4 + b"\x00" * pad + bytes(middle) \
        + b"\x00" * gap + bytes(footer)
    assert len(out) == TOTAL_SIZE, len(out)
    with open(out_path, "wb") as f:
        f.write(out)
    print(f"wrote {out_path}: {len(out)} bytes "
          f"(ramdisk region shift: {PAGE + len(new_lz4) + pad - rd_pad_end:+d} bytes)")

    # self-verify: decompress the new ramdisk and check the modules are present
    verify = run_lz4(lz4bin, ["-d", "-l", "-f"], out[PAGE:PAGE + len(new_lz4)])
    ok = all(
        verify.find(open(p, "rb").read()) >= 0
        for p in (sched_ko, lastkmsg_ko)
    )
    print("verify: new module data present in output ramdisk:", ok)
    if not ok:
        sys.exit("ERROR: verification failed")


if __name__ == "__main__":
    main()
