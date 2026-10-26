#!/usr/bin/env python3
# 字节级内核替换 v4（严格保布局）
# 只把新内核写入"出厂内核槽位"[page_size, page_size+align(factory_kernel_size))，
# 并把该槽位中内核之后的残留字节清零；头部 kernel_size 更新为新长度。
# 槽位之后的所有内容（AVB vbmeta + 签名、尾部 AVBf footer 等）保持原绝对偏移不动。
# 由于 CI 无中兴私钥，无法重签 AVB，故不能用超出原槽位的更大内核（会覆盖 AVB 结构）。
import sys, zlib

def align_up(x, a):
    return (x + a - 1) // a * a

def repack(boot_path, kernel_path, out_path):
    with open(boot_path, 'rb') as f:
        data = bytearray(f.read())
    with open(kernel_path, 'rb') as f:
        kernel = f.read()

    if kernel[:2] == b'\x1f\x8b':
        kernel = zlib.decompress(kernel, 31)
        print("[repack] 输入为 gzip，已解压为裸 Image")

    page_size = int.from_bytes(data[36:40], 'little')
    if page_size < 1024:
        page_size = 4096
    kernel_size = int.from_bytes(data[8:12], 'little')

    kernel_off = page_size
    slot_len = align_up(kernel_size, page_size)   # 出厂内核槽位（对齐后）
    slot_end = kernel_off + slot_len

    print(f"[repack] page={page_size} factory_kernel_size={kernel_size} slot_len={slot_len} "
          f"slot_end={slot_end} new_kernel={len(kernel)} image_total={len(data)}")

    if len(kernel) > slot_len:
        raise SystemExit(
            f"ERROR: 新内核 {len(kernel)} 字节 > 出厂槽位 {slot_len} 字节（超出 {len(kernel)-slot_len}）。\n"
            f"槽位之后紧邻 AVB vbmeta+签名，无私有钥无法重签，故必须精简配置把内核压回槽位内。"
        )

    out = bytearray(data)
    # 写入内核
    out[kernel_off:kernel_off + len(kernel)] = kernel
    # 清零槽位剩余部分（避免残留旧内核字节）
    if slot_end > kernel_off + len(kernel):
        out[kernel_off + len(kernel):slot_end] = b'\x00' * (slot_end - (kernel_off + len(kernel)))
    # 更新头部 kernel_size
    out[8:12] = len(kernel).to_bytes(4, 'little')

    with open(out_path, 'wb') as f:
        f.write(out)

    print(f"[repack] 已写出 {out_path} ({len(out)} 字节), kernel={len(kernel)}, header.kernel_size 已更新")
    print(f"[repack] 槽位剩余已清零 {slot_len - len(kernel)} 字节; 槽位之后(AVB 区)保持原样")
    print("[repack] 新内核 ARM64 魔数 @off+0x38:", bytes(out[kernel_off + 0x38:kernel_off + 0x3c]))

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print("usage: repack.py <boot.bin> <new_kernel_Image> <out.img>")
        sys.exit(1)
    repack(sys.argv[1], sys.argv[2], sys.argv[3])
