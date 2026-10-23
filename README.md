# W210DS GKI 内核补完 · Droidspaces 命名空间支持

为中兴平板 W210DS（内核 `5.15.178-android13-8-00012-g4ea0fcb5d130-ab13530115`，GKI 通用内核，
基础源码提交 `4ea0fcb5d1308f2f5a5d` / AOSP release `android13-5.15-2025-03_r6`）重建内核，
开启 Droidspaces 所需的全部命名空间 / cgroup 特性，并直接打包成可刷入的 `boot_a` 镜像。

## ✅ 根因已确认（已通过 adb 在你平板上实测）

从设备拉取 `/proc/config.gz` 逐符号核对，结论是**内核配置确实缺项**（不是 SELinux，
因为 `unshare` 返回的是 `Invalid argument` = EINVAL，内核态不支持，而非 `Operation not permitted`）：
`CONFIG_PID_NS` / `CONFIG_USER_NS` / `CONFIG_DEVTMPFS` / `CONFIG_MACVLAN` / `CONFIG_CGROUP_PIDS` /
`CONFIG_CFS_BANDWIDTH` / `CONFIG_IP6_NF_NAT` 均为 `not set`。

**关键坑**：`CONFIG_IPC_NS` 在配置里**连选项都没有**，原因是它依赖 `CONFIG_SYSVIPC`，
而 `CONFIG_SYSVIPC` 也是 `not set`。所以重建时必须先开 `SYSVIPC` 才能开 `IPC_NS`。本项目已处理。

> 因此：必须重编内核，且用**设备自己的真实 `.config`** 当编译底座最稳（只翻转缺失项，驱动/选项不变）。

## 工作原理

- 仓库里的 `config/w210ds_base.config` 是你设备当前内核的完整配置（已拉取存档）。
- 构建时把它作为 `.config` 基底，仅用 `scripts/config --enable` 打开缺失项（含 `SYSVIPC`），
  并注入 SukiSU（保留你的 root），然后用 `LLVM=1` 编译出**未压缩的裸 ARM64 Image**。
- 打包采用**字节级内核替换**（`scripts/repack.py`）：解析 boot 镜像头部定位内核区域，
  把新内核填回，保留原始头部（含更新 `kernel_size`）与尾部；ramdisk 在 `vendor_boot`，不动。
- 产物 `boot_a_droidspaces.img` 与你原始 `boot_a.bin` 同尺寸（64MB），可直接刷入。

## 使用方法（GitHub Actions，免费 runner）

1. 把本仓库（`gki-droidspaces-build/`）整个推到你自己的 GitHub 仓库。
2. 二选一提供原始 `boot_a.bin`：
   - **方式 A（推荐）**：把 `boot_a.bin` 放到本仓库 `assets/boot_a.bin` 后提交；或
   - **方式 B**：在仓库 `Settings → Secrets and variables → Actions` 加变量 `BOOT_URL`，
     值为可直接下载 `boot_a.bin` 的直链（GitHub Release / raw 等）。
3. 在 `Actions` 标签里手动运行 `Build W210DS GKI (Droidspaces)`。
   - 可用输入：`SUKISU`（默认 `true`，注入 SukiSU 保留 root）、`KERNEL_COMMIT`（默认 `4ea0fcb5d1308f2f5a5d`）。
4. 运行完在 Artifacts 里下载 `boot_a_droidspaces.img`。

> 构建中途会打印关键配置校验结果（`PID_NS -> CONFIG_PID_NS=y` 等），方便确认真的开上了。

## 刷入（请先备份！）

```bash
# 备份当前 boot（救命用）
# 先用 fastboot boot 只启动不刷入，确认能开机 / root 正常 / Droidspaces 通过，再 flash
fastboot boot boot_a_droidspaces.img
# 确认无误后再：
fastboot flash boot_a boot_a_droidspaces.img
# vendor_boot 不要动
```

> 若设备启用 AVB 校验导致不开机：`fastboot flash --disable-verification vbmeta`
> （你已用 sukiSU，bootloader 多半已解锁 / AVB 已关，通常无需此步）。

## 救砖

随时可用原始 `boot_a.bin` 救回：
```bash
fastboot flash boot_a /path/to/original/boot_a.bin
```

## 文件说明

- `.github/workflows/build.yml` — CI：克隆 GKI 源码 → 注入 SukiSU → 以设备配置为基底开启命名空间 → 编译裸 Image → 字节级替换打包。
- `config/w210ds_base.config` — 你设备当前内核的完整 `.config`（构建基底，已存档）。
- `config/droidspaces.config` — 要开启的符号清单（参考/文档用，实际由 workflow 用 `scripts/config` 逐项开启）。
- `scripts/repack.py` — 头部字段驱动的字节级内核替换，输出 `boot_a_droidspaces.img`。
- `assets/` — 放原始 `boot_a.bin`（方式 A）。
