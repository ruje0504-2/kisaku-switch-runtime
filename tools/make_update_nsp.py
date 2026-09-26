#!/usr/bin/env python3
"""把已有的 Program/Control NCA 打成一个**真正的** update（Patch）NSP。

为什么需要这个脚本
------------------
主机把"游戏更新"识别为 ContentMetaType = Patch(0x81) 且带
``PatchMetaExtendedHeader{ ApplicationId = base 标题 }`` 的 CNMT；

* hacbrewpack 只会写 Application(0x80)，连文件名都是 ``Application_<tid>.cnmt``
  （``cnmt.c`` 里 ``cnmt_ctx.header.type = 0x80`` 写死），所以用它打出来的
  ``...0800`` 包会被当成一个**独立应用**而不是更新。
* hacPack 能造 meta NCA，但**不肯自己生成** patch 的 CNMT：
  ``nca.c`` 直接 ``"Creating Patch metadata without providing cnmt is not supported yet!"``
  然后 exit。

所以这里按 switchbrew 的定义（CNMT 页）手工拼 ``Patch_<tid>.cnmt``，再交给
hacPack 的 ``--ncatype meta --titletype patch --cnmt`` 封成 meta NCA，
最后按 PFS0 组成 NSP。

CNMT 布局（全部小端）
---------------------
::

    0x00 CnmtHeader (0x20)
         u64 title_id              = update 标题 ID
         u32 title_version
         u8  meta_type             = 0x81 (Patch)
         u8  meta_platform         = 0x00 (NX)
         u16 extended_header_size  = 0x18
         u16 total_content_entries
         u16 total_content_meta_entries = 0
         u8  attributes / storage_id / content_install_type / reserved = 0
         u32 required_dl_system_version = 0
         u32 reserved = 0
    0x20 PatchMetaExtendedHeader (0x18)
         u64 application_id        = base 标题 ID   <-- 决定它是不是"更新"
         u32 required_system_version
         u32 extended_data_size
         u64 reserved
    0x38 PackagedContent[] (每条 0x38)
         u8[32] sha256(NCA)
         u8[16] content_id = 该哈希前 16 字节（即 NCA 文件名）
         u8[6]  size
         u8     content_type (1=Program, 3=Control)
         u8     id_offset
    末尾  u8[32] digest —— 生产版为全零（switchbrew：只有开发版才算哈希）

用法::

    python3 tools/make_update_nsp.py \\
        --program-nca prog.nca --control-nca control.nca \\
        --hacpack /path/to/hacpack --keyset ~/.switch/prod.keys \\
        --out 交付/kisaku-update.nsp
"""
from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile

CNMT_HEADER_FMT = "<QIBBHHHBBBBII"          # 0x20
PATCH_EXT_FMT = "<QIIQ"                     # 0x18
CONTENT_RECORD_SIZE = 0x38
META_TYPE_PATCH = 0x81
PLATFORM_NX = 0x00
CT_PROGRAM, CT_CONTROL = 1, 3


def parse_version(text: str) -> int:
    """``1.0.1`` / ``0x10001`` -> u32。任天堂用 major<<16 | minor<<8 | micro。"""
    if text.lower().startswith("0x"):
        return int(text, 16)
    parts = [int(p) for p in text.split(".")]
    while len(parts) < 3:
        parts.append(0)
    major, minor, micro = parts[:3]
    return (major << 16) | (minor << 8) | micro


def content_record(path: str, content_type: int) -> bytes:
    data = open(path, "rb").read()
    digest = hashlib.sha256(data).digest()
    content_id = digest[:16]                      # NCA 文件名就是这个
    if os.path.basename(path)[:32].lower() != content_id.hex():
        print("警告: %s 的文件名不是 sha256 前 16 字节" % path, file=sys.stderr)
    return digest + content_id + len(data).to_bytes(6, "little") + bytes([content_type, 0])


def parse_base_cnmt(data: bytes) -> dict:
    """从本体（Application）CNMT 里取出补丁历史需要的字段。"""
    tid, ver, mt, _mp, ehs, tce, _tcme = struct.unpack_from("<QIBBHHH", data, 0)
    contents = []
    off = 0x20 + ehs
    for _ in range(tce):
        contents.append((data[off + 0x20:off + 0x30],
                         int.from_bytes(data[off + 0x30:off + 0x36], "little"),
                         data[off + 0x36]))
        off += 0x38
    required_system_version = 0
    if mt == 0x80:                      # ApplicationMetaExtendedHeader
        _patch_id, required_system_version, _req_app = struct.unpack_from("<QII", data, 0x20)
    return {"id": tid, "version": ver, "type": mt, "attributes": data[0x14],
            "contents": contents, "digest": data[-0x20:],
            "required_system_version": required_system_version}


def build_patch_extended_data(base: dict, base_meta_nca: str) -> bytes:
    """PatchMetaExtendedData —— 记录这个补丁打的是本体的哪些内容。

    主机靠这段历史判断补丁是否合法；缺了它（extended_data_size = 0）补丁会被
    判为非法，表现为「读不到名字/图标 + 起不来」。字段语义实测自两个真实更新
    （VII Reimagined 只更新 ExeFS、Silksong 带 Delta 增量）：

      PatchHistoryHeader.ContentInfoCount == 本体内容数 + 1
                                             （+1 是本体自己的 meta NCA，type=0）
      PatchHistoryHeader.Digest           == 本体 CNMT 文件尾部那 0x20 字节
      PatchHistoryHeader.ContentMetaKey   == (本体 id, 本体版本, 本体 meta 类型)
    """
    meta_id = bytes.fromhex(os.path.basename(base_meta_nca).split(".")[0])
    infos = list(base["contents"]) + [(meta_id, os.path.getsize(base_meta_nca), 0)]
    out = struct.pack("<IIIIIII", 1, 0, 0, 0, len(infos), 0, 0)
    out += struct.pack("<QIB3x", base["id"], base["version"], base["type"])
    out += base["digest"]
    out += struct.pack("<H6x", len(infos))
    for content_id, size, content_type in infos:
        out += content_id + size.to_bytes(6, "little") + bytes([content_type, 0])
    return out


def build_patch_cnmt(update_title_id: int, base_title_id: int, version: int,
                     ncas: list[tuple[str, int]], required_system_version: int = 0,
                     extended_data: bytes = b"") -> bytes:
    records = b"".join(content_record(p, t) for p, t in ncas)
    ext = struct.pack(PATCH_EXT_FMT, base_title_id, required_system_version,
                      len(extended_data), 0)
    header = struct.pack(
        CNMT_HEADER_FMT,
        update_title_id, version, META_TYPE_PATCH, PLATFORM_NX,
        len(ext),                # extended_header_size 必须是扩展头真实长度
        len(ncas),               # total_content_entries
        0,                       # total_content_meta_entries
        0, 0, 0, 0,              # attributes / storage_id / install type / reserved
        0, 0)                    # required_dl_system_version / reserved
    body = header + ext + records + extended_data
    return body + b"\0" * 0x20   # 生产版 digest 为全零


def build_pfs0(entries: list[tuple[str, bytes]]) -> bytes:
    """PFS0（NSP 就是 PFS0）。

    注意：条目里的 offset 是**相对数据区起点**的，不是相对文件开头
    （hacbrewpack 的 NSP 里第一条就是 0）。先按 0x20 对齐拼出数据区并
    记录相对偏移，再回填表项，避免偏移与真实位置错位。
    """
    names = b""
    name_off: dict[str, int] = {}
    for name, _ in entries:
        if name not in name_off:
            name_off[name] = len(names)
            names += name.encode() + b"\0"

    body = bytearray()
    offsets = []
    for _, blob in entries:
        offsets.append(len(body))
        body += blob
        body += b"\0" * ((-len(body)) % 0x20)
    assert len(offsets) == len(entries)

    table = b"".join(
        struct.pack("<QQI4x", off, len(blob), name_off[name])
        for (name, blob), off in zip(entries, offsets))
    return (b"PFS0" + struct.pack("<III", len(entries), len(names), 0)
            + table + names + bytes(body))


def read_pfs0(path: str) -> dict[str, bytes]:
    """从 PFS0（NSP）里取出所有文件。条目 offset 相对数据区起点。"""
    with open(path, "rb") as f:
        magic, count, strsize, _ = struct.unpack("<4sIII", f.read(0x10))
        if magic != b"PFS0":
            raise ValueError("不是 PFS0: %s" % path)
        entries = [struct.unpack("<QQI4x", f.read(0x18)) for _ in range(count)]
        strtab = f.read(strsize)
        data0 = 0x10 + 0x18 * count + strsize
        out = {}
        for off, size, nameoff in entries:
            name = strtab[nameoff:strtab.index(b"\0", nameoff)].decode()
            f.seek(data0 + off)
            out[name] = f.read(size)
    return out


def extract_base_cnmt(nca_path: str, hactool: str, keyset: str, workdir: str) -> bytes:
    """用 hactool 把本体 meta NCA 里的 .cnmt 解出来。"""
    out = os.path.join(workdir, "base-cnmt")
    os.makedirs(out, exist_ok=True)
    proc = subprocess.run(
        [hactool, "-k", os.path.abspath(keyset), "--disablekeywarns",
         "-t", "nca", "-x", "--section0dir=" + out, os.path.abspath(nca_path)],
        capture_output=True, text=True)
    names = [f for f in os.listdir(out) if f.endswith(".cnmt")]
    if proc.returncode != 0 or not names:
        sys.stderr.write(proc.stdout + proc.stderr)
        raise SystemExit("hactool 未能解出本体 CNMT: %s" % nca_path)
    with open(os.path.join(out, names[0]), "rb") as fh:
        return fh.read()


def pfs0_members(path: str) -> list:
    """列出 NSP/PFS0 的成员（不读数据）：[(name, offset, size)]，offset 相对数据区。"""
    with open(path, "rb") as f:
        magic, count, strsize, _ = struct.unpack("<4sIII", f.read(0x10))
        if magic != b"PFS0":
            raise ValueError("不是 PFS0: %s" % path)
        entries = [struct.unpack("<QQI4x", f.read(0x18)) for _ in range(count)]
        strtab = f.read(strsize)
        return [(strtab[o:strtab.index(b"\0", o)].decode(), off, size)
                for off, size, o in entries]


def read_member(path: str, name: str, want: int | None = None) -> bytes:
    """从 PFS0 里读一个成员；want 给定时只读开头（用于只取 NCA 头）。"""
    with open(path, "rb") as f:
        magic, count, strsize, _ = struct.unpack("<4sIII", f.read(0x10))
        entries = [struct.unpack("<QQI4x", f.read(0x18)) for _ in range(count)]
        strtab = f.read(strsize)
        data0 = 0x10 + 0x18 * count + strsize
        for off, size, nameoff in entries:
            if strtab[nameoff:strtab.index(b"\0", nameoff)].decode() == name:
                f.seek(data0 + off)
                return f.read(size if want is None else min(want, size))
    raise KeyError("NSP 里没有 %s" % name)


def header_key(keyset: str) -> str:
    for line in open(keyset):
        if line.strip().startswith("header_key"):
            return line.split("=", 1)[1].strip()
    raise SystemExit("prod.keys 里找不到 header_key")


def _xts_decrypt_python(enc: bytes, key_hex: str) -> bytes:
    """AES-128-XTS in pure python (tools/aes_xts.py)."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from aes_xts import decrypt_nca_header as _impl
    return _impl(enc, key_hex)


def decrypt_nca_header(enc: bytes, key_hex: str) -> bytes:
    """NCA 头用 AES-128-XTS(header_key) 加密，每 0x200 字节一个扇区，
    tweak = 扇区号（16 字节大端）。实测与 hactool 解出的明文头逐字节一致。

    默认走自带的纯 Python 实现：macOS 的 /usr/bin/openssl 是 LibreSSL，
    对 ``enc -aes-128-xts`` 只会静默输出空内容，OpenSSL >= 3.6 的 enc 更是
    直接报 "enc XTS ciphers not supported"。openssl 路径保留给能用的主机。
    """
    try:
        out = _xts_decrypt_python(enc, key_hex)
        if len(out) == len(enc) // 0x200 * 0x200:
            return out
    except Exception:
        pass
    out = b""
    for sector in range(len(enc) // 0x200):
        iv = sector.to_bytes(16, "big").hex()
        proc = subprocess.run(
            ["openssl", "enc", "-d", "-aes-128-xts", "-K", key_hex, "-iv", iv],
            input=enc[sector * 0x200:(sector + 1) * 0x200], capture_output=True)
        if proc.returncode != 0 or len(proc.stdout) != 0x200:
            raise SystemExit("AES-XTS 解密 NCA 头失败（python 与 openssl 都不可用）")
        out += proc.stdout
    return out


def base_romfs_info(hdr: bytes) -> tuple:
    """从解密后的本体 NCA 头里取 romfs 段(1)的 IVFC 头(0xE0) 与段大小。

    NCA 头布局：section_entries[4] @0x240（每项 0x10，单位 0x200）、
    fs_headers[4] @0x400（每项 0x200），段 1 的 IVFC 头在 fs_header[1]+8。
    """
    start, end = struct.unpack_from("<II", hdr, 0x240 + 1 * 0x10)
    return hdr[0x600 + 8:0x600 + 8 + 0xE0], (end - start) * 0x200


def version_display(text: str, value: int) -> bytes:
    if not text.lower().startswith("0x"):
        return text.encode()[:0x10]
    return ("%d.%d.%d" % ((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF)).encode()


def run(cmd: list, cwd: str, env=None):
    proc = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout + proc.stderr)
        raise SystemExit("命令失败: %s" % " ".join(str(c) for c in cmd[:5]))
    return proc


def pick_one(directory: str, tag: str) -> str:
    names = [f for f in os.listdir(directory) if f.endswith(".nca")]
    if len(names) != 1:
        raise SystemExit("%s NCA 产物异常: %s" % (tag, names))
    return os.path.join(directory, names[0])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--base-titleid", default="01008B538DE50000",
                    help="base 应用标题 ID（16 位十六进制）")
    ap.add_argument("--update-titleid", default=None,
                    help="update 标题 ID，默认 = base + 0x800")
    ap.add_argument("--version", default="1.0.1",
                    help="标题版本，如 1.0.1 或 0x10001")
    ap.add_argument("--program-nca", help="Program NCA（或改用 --from-nsp）")
    ap.add_argument("--control-nca", help="Control NCA（或改用 --from-nsp）")
    ap.add_argument("--from-nsp", default=None,
                    help="从一个已有 NSP 里取 Program/Control NCA，"
                         "例如 hacbrewpack 打出来的 UPDATE=1 产物")
    ap.add_argument("--hacpack", default="hacpack", help="hacPack 可执行文件")
    ap.add_argument("--keyset", default=os.path.expanduser("~/.switch/prod.keys"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--workdir", default=None, help="临时目录，默认自动创建")
    ap.add_argument("--base-cnmt-nca", default=None,
                    help="本体 Program 之外的 meta NCA（*.cnmt.nca）。给了它就按真实"
                         "更新那样生成 PatchMetaExtendedData（补丁历史）；不给就是"
                         "extended_data_size=0，实测会被主机判为非法补丁")
    ap.add_argument("--hactool", default="/opt/devkitpro/tools/bin/hactool")
    ap.add_argument("--base-nsp", default=None,
                    help="本体 NSP。给了它 + --exefsdir 就跑完整流水线：只从本体里读需要"
                         "的碎片（meta NCA / control NCA / Program 头），自己用 hacPack 造"
                         "BKTR Patch RomFS 的 Program、派生 NACP 的 Control 和带补丁历史"
                         "的 Patch CNMT，最后组出真正的 update NSP")
    ap.add_argument("--exefsdir", default=None,
                    help="新 ExeFS 目录（含 main 与 main.npdm），配合 --base-nsp")
    ap.add_argument("--required-system-version", default=None,
                    help="默认沿用本体的 RequiredSystemVersion")
    args = ap.parse_args()

    base_id = int(args.base_titleid, 16)
    update_id = int(args.update_titleid, 16) if args.update_titleid else base_id + 0x800
    version = parse_version(args.version)

    tmp = args.workdir or tempfile.mkdtemp(prefix="make-update-")
    os.makedirs(tmp, exist_ok=True)

    if args.base_nsp:
        # ---------------------------------------------------------------- 完整流水线
        # 只从本体 NSP 里读需要的碎片，不复制 3.4 GB 的游戏数据。
        if not os.path.isfile(args.base_nsp):
            print("缺少: %s" % args.base_nsp, file=sys.stderr)
            return 1
        if not args.exefsdir or not os.path.isdir(args.exefsdir):
            print("--base-nsp 需要配合 --exefsdir（含 main 与 main.npdm）", file=sys.stderr)
            return 1
        members = pfs0_members(args.base_nsp)
        meta_name = next((n for n, _, _ in members if n.endswith(".cnmt.nca")), None)
        ctrl_prog = sorted(((n, s) for n, _, s in members
                            if n.endswith(".nca") and not n.endswith(".cnmt.nca")),
                           key=lambda kv: kv[1])
        if meta_name is None or len(ctrl_prog) != 2:
            print("本体 NSP 成员不符合预期: %s" % [n for n, _, _ in members], file=sys.stderr)
            return 1
        (control_name, control_size), (program_name, program_size) = ctrl_prog
        print("本体 NSP: meta=%s  control=%s(%d)  program=%s(%d)"
              % (meta_name, control_name, control_size, program_name, program_size))

        # ① meta NCA -> 本体 CNMT（补丁历史的来源）
        base_meta_nca = os.path.join(tmp, meta_name)
        with open(base_meta_nca, "wb") as fh:
            fh.write(read_member(args.base_nsp, meta_name))
        base = parse_base_cnmt(extract_base_cnmt(base_meta_nca, args.hactool,
                                                 args.keyset, tmp))

        # ② control NCA -> 本体 NACP + 图标，只把 display_version 抬到新版本
        #    （归属字段必须保持本体，否则主机的名字/图标会读不出来）
        base_ctrl = os.path.join(tmp, "base-control")
        os.makedirs(base_ctrl, exist_ok=True)
        base_ctrl_nca = os.path.join(tmp, control_name)
        with open(base_ctrl_nca, "wb") as fh:
            fh.write(read_member(args.base_nsp, control_name))
        subprocess.run([args.hactool, "-k", os.path.abspath(args.keyset),
                        "--disablekeywarns", "-t", "nca", "-x",
                        "--section0dir=" + base_ctrl, base_ctrl_nca], capture_output=True)
        nacp = os.path.join(base_ctrl, "control.nacp")
        if not os.path.isfile(nacp):
            print("没能从本体 Control NCA 解出 control.nacp", file=sys.stderr)
            return 1
        data = bytearray(open(nacp, "rb").read())
        old_display = bytes(data[0x3060:0x3070]).split(b"\0")[0]
        data[0x3060:0x3070] = version_display(args.version, version).ljust(0x10, b"\0")
        with open(nacp, "wb") as fh:
            fh.write(bytes(data))
        print("本体 NACP: display %r -> %r，save_data_owner_id 保持 0x%016X"
              % (old_display, bytes(data[0x3060:0x3070]).split(b"\0")[0],
                 struct.unpack_from("<Q", data, 0x3078)[0]))

        # ③ 只读 Program NCA 的前 0xC00 字节 -> 解密 -> BKTR 需要的 IVFC 与虚拟大小
        enc = read_member(args.base_nsp, program_name, want=0xC00)
        hdr = decrypt_nca_header(enc, header_key(args.keyset))
        ivfc, virt = base_romfs_info(hdr)
        ivfc_path = os.path.join(tmp, "base-ivfc.bin")
        with open(ivfc_path, "wb") as fh:
            fh.write(ivfc)
        print("本体 romfs 段: 虚拟大小 0x%X  IVFC master_hash=%s"
              % (virt, ivfc[0xC0:0xE0].hex()))

        # ④ hacPack 造 Control（本体 ID；NACP 原样不动）
        ctrl_out = os.path.join(tmp, "control-out")
        os.makedirs(ctrl_out, exist_ok=True)
        run([args.hacpack, "--keyset", os.path.abspath(args.keyset), "--type", "nca",
             "--ncatype", "control", "--titleid", "%016x" % base_id,
             "--romfsdir", base_ctrl, "--outdir", ctrl_out], tmp)

        # ⑤ hacPack 造 Program（本体 ID + BKTR Patch RomFS 叠加表）
        empty_romfs = os.path.join(tmp, "empty-romfs")
        os.makedirs(empty_romfs, exist_ok=True)
        prog_out = os.path.join(tmp, "program-out")
        os.makedirs(prog_out, exist_ok=True)
        env = dict(os.environ, HACPACK_BKTR_IVFC=ivfc_path, HACPACK_BKTR_SIZE="0x%X" % virt)
        run([args.hacpack, "--keyset", os.path.abspath(args.keyset), "--type", "nca",
             "--ncatype", "program", "--titleid", "%016x" % base_id,
             "--exefsdir", args.exefsdir, "--romfsdir", empty_romfs,
             "--outdir", prog_out], tmp, env=env)

        args.control_nca = pick_one(ctrl_out, "Control")
        args.program_nca = pick_one(prog_out, "Program")
        args.base_cnmt_nca = base_meta_nca       # 交给下面的补丁历史逻辑
        print("内容 NCA 就绪: Program=%s  Control=%s"
              % (os.path.basename(args.program_nca), os.path.basename(args.control_nca)))
        print()

    if args.from_nsp:
        if not os.path.isfile(args.from_nsp):
            print("缺少: %s" % args.from_nsp, file=sys.stderr)
            return 1
        blobs = read_pfs0(args.from_nsp)
        ncas = sorted((n, b) for n, b in blobs.items()
                      if n.endswith(".nca") and not n.endswith(".cnmt.nca"))
        if len(ncas) != 2:
            print("源 NSP 里期望 2 个非 meta NCA，实际 %d 个: %s"
                  % (len(ncas), [n for n, _ in ncas]), file=sys.stderr)
            return 1
        (control_name, control_blob), (program_name, program_blob) = [
            (n, b) for n, b in sorted(ncas, key=lambda kv: len(kv[1]))]  # 小的当 Control
        args.control_nca = os.path.join(tmp, control_name)
        args.program_nca = os.path.join(tmp, program_name)
        open(args.control_nca, "wb").write(control_blob)
        open(args.program_nca, "wb").write(program_blob)
        print("从 %s 取出: Program=%s Control=%s"
              % (os.path.basename(args.from_nsp), program_name, control_name))

    for name in ("program_nca", "control_nca"):
        if not getattr(args, name):
            print("需要 --program-nca/--control-nca 或 --from-nsp", file=sys.stderr)
            return 1
    for p in (args.program_nca, args.control_nca, args.keyset):
        if not os.path.isfile(p):
            print("缺少: %s" % p, file=sys.stderr)
            return 1
    if shutil.which(args.hacpack) is None and not os.path.isfile(args.hacpack):
        print("缺少 hacPack: %s" % args.hacpack, file=sys.stderr)
        return 1

    print("base   = 0x%016X" % base_id)
    print("update = 0x%016X   version = 0x%X" % (update_id, version))

    extended_data = b""
    required_system_version = (int(args.required_system_version, 0)
                               if args.required_system_version else None)
    if args.base_cnmt_nca:
        if not os.path.isfile(args.base_cnmt_nca):
            print("缺少: %s" % args.base_cnmt_nca, file=sys.stderr)
            return 1
        base = parse_base_cnmt(extract_base_cnmt(args.base_cnmt_nca, args.hactool,
                                                 args.keyset, tmp))
        extended_data = build_patch_extended_data(base, args.base_cnmt_nca)
        if required_system_version is None:
            required_system_version = base["required_system_version"]
        print("本体   = 0x%016X  version 0x%X  type 0x%02X  内容 %d 条"
              % (base["id"], base["version"], base["type"], len(base["contents"])))
        print("补丁历史: history 头 1 条 + 内容清单 %d 条  extended_data_size=0x%X"
              % (len(base["contents"]) + 1, len(extended_data)))
    if required_system_version is None:
        required_system_version = 0

    cnmt = build_patch_cnmt(update_id, base_id, version,
                            [(args.program_nca, CT_PROGRAM), (args.control_nca, CT_CONTROL)],
                            required_system_version, extended_data)
    print("已生成 Patch CNMT: %d 字节（0x%X）" % (len(cnmt), len(cnmt)))

    cnmt_name = "Patch_%016x.cnmt" % update_id
    cnmt_path = os.path.join(tmp, cnmt_name)
    with open(cnmt_path, "wb") as fh:
        fh.write(cnmt)

    meta_out = os.path.join(tmp, "meta")
    os.makedirs(meta_out, exist_ok=True)
    cmd = [args.hacpack, "--keyset", os.path.abspath(args.keyset),
           "--type", "nca", "--ncatype", "meta", "--titletype", "patch",
           "--titleid", "%016x" % update_id,
           "--cnmt", cnmt_path, "--outdir", meta_out,
           "--tempdir", os.path.join(tmp, "hacpack_temp"),
           "--backupdir", os.path.join(tmp, "hacpack_backup")]
    print("hacPack:", " ".join(cmd[1:]))
    proc = subprocess.run(cmd, cwd=tmp, capture_output=True, text=True)
    sys.stdout.write(proc.stdout)
    sys.stderr.write(proc.stderr)
    if proc.returncode != 0:
        print("hacPack 失败（exit %d）" % proc.returncode, file=sys.stderr)
        return 1

    metas = [f for f in os.listdir(meta_out) if f.endswith(".nca")]
    if len(metas) != 1:
        print("meta NCA 产物异常: %s" % metas, file=sys.stderr)
        return 1
    meta_path = os.path.join(meta_out, metas[0])
    print("meta NCA: %s（%d 字节）" % (metas[0], os.path.getsize(meta_path)))

    entries = [
        (metas[0], open(meta_path, "rb").read()),
        (os.path.basename(args.program_nca), open(args.program_nca, "rb").read()),
        (os.path.basename(args.control_nca), open(args.control_nca, "rb").read()),
    ]
    nsp = build_pfs0(entries)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as fh:
        fh.write(nsp)
    print("\n写出 NSP: %s（%d 字节）" % (args.out, len(nsp)))
    for name, blob in entries:
        print("   %-44s %d" % (name, len(blob)))
    print("sha256: %s" % hashlib.sha256(nsp).hexdigest())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
