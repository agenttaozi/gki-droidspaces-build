#!/usr/bin/env python3
# 字节级内核替换 v2：基于 boot 镜像头部字段定位内核区域，把新编译的裸 ARM64 Image 填回。
# 保留原始头部与尾部（ramdisk 在 vendor_boot，不动）；同步更新头部 kernel_size 字段。
import sys, zlib

def align_up(x, a):
    return (x + a - 1) // a * a

def repack(boot_path, kernel_path, out_path):
    with open(boot_path, 'rb') as f:
        data = bytearray(f.read())
    with open(kernel_path, 'rb') as f:
        kernel = f.read()

    # 若传入的是 gzip 压缩内核，自动解压为裸 Image（原始镜像就是裸 Image）
    if kernel[:2] == b'\x1f\x8b':
        kernel = zlib.decompress(kernel, 31)
        print("[repack] 输入为 gzip，已解压为裸 Image")

    page_size   = int.from_bytes(data[36:40], 'little') or 4096
    kernel_size = int.from_bytes(data[8:12], 'little')   # 头部 kernel_size 字段
    if page_size < 1024:
        page_size = 4096
    print(f"[repack] page_size={page_size} header_kernel_size={kernel_size} new_kernel={len(kernel)}")

    kernel_off = page_size
    region_len = align_up(kernel_size, page_size)        # 内核实际占用的页数（对齐后）
    if len(kernel) > region_len:
        raise SystemExit(
            f"ERROR: 新内核 {len(kernel)} 字节 > 可用槽位 {region_len} 字节，塞不进分区！\n"
            f"请确认 CONFIG_DEBUG_INFO=n、关闭 LTO，或减小内核体积后重试。"
        )

    new_kernel = kernel + b'\x00' * (region_len - len(kernel))

    # 把头部 kernel_size 更新为新内核真实长度（bootloader 按此加载）
    data[8:12] = len(kernel).to_bytes(4, 'little')

    out = data[:kernel_off] + new_kernel + data[kernel_off + region_len:]
    with open(out_path, 'wb') as f:
        f.write(out)
    print(f"[repack] 已写出 {out_path} ({len(out)} 字节)")
    print("[repack] 新内核 ARM64 魔数 @off+0x38:", out[kernel_off + 0x38:kernel_off + 0x3c])

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print("usage: repack.py <boot.bin> <new_kernel_Image> <out.img>")
        sys.exit(1)
    repack(sys.argv[1], sys.argv[2], sys.argv[3])
