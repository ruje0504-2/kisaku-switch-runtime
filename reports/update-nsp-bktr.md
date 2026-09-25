# 只更新 ExeFS 的 update NSP：字段级配方

2026-09-26。本文记录《鬼作》移植版打「只更新 ExeFS」的 **update（补丁）NSP** 所需的全部字段、
逆向依据和复现命令。结论已在实机验证：装上后名字/图标正常、游戏可启动、romfs 数据全部来自本体。

参考样本（均为真实发行的包，逆向时逐字节对照）：

| 样本 | 用途 |
|---|---|
| `VII Reimagined [0100505021440800][v65536][Update].nsp` | **只更新 ExeFS 的 update**，romfs 段只有 64 KB 叠加表 —— 本工程要复现的形态 |
| `Hollow Knight Silksong [010013C00E930800][v131072][Update].nsp` | 带 Delta 增量的 update（另一条路线，暂未采用） |
| `[01008B538DE50000][v0][Base].nsp` | 本工程本体，用于派生补丁历史 / NACP / BKTR 的 IVFC |

## 一、三条硬性要求（缺一条主机就不认）

| # | 要求 | 缺了会怎样 |
|---|---|---|
| ① | CNMT 必须是 `ContentMetaType = Patch(0x81)`，且带 **PatchMetaExtendedData**（补丁历史） | NCM 拒绝这个 meta：主界面**名字/图标读不出来**，游戏也起不来 |
| ② | 内容 NCA（Program/Control）的 **NCA 头 Title ID 必须是本体**；只有 meta NCA 用 update ID | 内容归属不符预期 |
| ③ | romfs 段必须是 **Patch RomFS**（`fs_header.crypt_type = CRYPT_BKTR`，64 KB 叠加表，全部区域 `is_patch=0` 指回本体） | 主机**不会**把普通 RomFS 叠加到本体上：游戏拿不到 `layer.arc` 等数据，起不来（DBI 里也看不到 romfs） |

补充：`hacbrewpack --noromfs` 产出的「没有 romfs 段」同样是无效的，原因同 ③。

## 二、Patch CNMT 布局（小端）

```
0x00 CnmtHeader (0x20)
     u64 title_id              = update ID（本体 + 0x800）
     u32 title_version
     u8  meta_type             = 0x81
     u8  meta_platform         = 0x00
     u16 extended_header_size  = 0x18
     u16 total_content_entries
     u16 total_content_meta_entries = 0
     u8  attributes / storage_id / content_install_type / reserved = 0
     u32 required_dl_system_version = 0
     u32 reserved = 0
0x20 PatchMetaExtendedHeader (0x18)
     u64 application_id        = 本体 ID
     u32 required_system_version = 本体 ApplicationMetaExtendedHeader 里的同一字段
     u32 extended_data_size    = 后面 PatchMetaExtendedData 的真实长度
     u64 reserved
0x38 PackagedContent[]（每条 0x38）
     u8[32] sha256(NCA)
     u8[16] content_id = 该哈希前 16 字节（= NCA 文件名）
     u8[6]  size
     u8     content_type（1=Program, 3=Control, 4=HtmlDocument, 5=LegalInformation, 0=Meta）
     u8     id_offset
接着 PatchMetaExtendedData（长度 = extended_data_size）
末尾 u8[32] digest —— 生产版为全零（hacbrewpack 打的本体也是全零，实测可用）
```

### PatchMetaExtendedData（补丁历史）

```
u32 PatchHistoryHeaderCount            = 1
u32 PatchDeltaHistoryCount             = 0
u32 PatchDeltaHeaderCount              = 0
u32 FragmentSetCount                   = 0
u32 PatchHistoryContentInfoCount
u32 PatchDeltaPackagedContentInfoCount = 0
u32 Reserved                           = 0
PatchHistoryHeader[1] (0x38)
    ContentMetaKey (0x10) = (本体 ID, 本体 version=0, 本体 meta type=0x80)
    Digest (0x20)         = 本体 CNMT **文件尾部那 0x20 字节**
    u16 ContentInfoCount  = 上面那个 Count
    u8[6] Reserved
PatchHistoryContentInfo[Count] (每条 0x18)
    u8[16] content_id、u8[6] size、u8 content_type、u8 id_offset
    = 本体 CNMT 里全部内容记录 **再加一条本体自己的 meta NCA（type=0）**
```

实测校验（Silksong，本体与 update 都有）：

```
update.PatchHistoryHeader[0].digest
  == sha256 文件尾部?? 不是 —— 它是本体 CNMT 末尾那 0x20 字节，逐字节相同
update.PatchHistoryHeader[0].ContentInfoCount == 本体内容数 + 1
```

于是 `extended_data_size = 0x1C + 0x38 + N*0x18`。鬼作本体 2 条内容 → N=3 → `0x9C`，
CNMT 总长 356 字节。

## 三、BKTR「Patch RomFS」叠加段

VII 那个只更新 ExeFS 的包，romfs 段共 `0x10000` 字节，**里面没有一个字节的游戏数据**，全是两张表：

```
fs_header[1]: version=2, partition_type=0(ROMFS), fs_type=3(FS_ROMFS), crypt_type=4(CRYPT_BKTR)
superblock = IVFC 头(0xE0) + 0x18 填充 + relocation_header(0x20) + subsection_header(0x20)

[0x0000,0x4000) relocation block 头
    u32 _0x0=0, u32 num_buckets=1, u64 total_size=本体 romfs 段大小
    u64 bucket_virtual_offsets[2046] 全 0
[0x4000,0x8000) relocation bucket
    u32 _0x0=0, u32 num_entries=1, u64 virtual_offset_end=本体 romfs 段大小
    entry[0] (0x14, packed) = { virt=0, phys=0, is_patch=0 }   ← 全部从本体读
[0x8000,0xC000) subsection block 头（num_buckets=1, total_size=0x8000）
[0xC000,0x10000) subsection bucket（num_entries=1, physical_offset_end=0x8000）
    entry[0] (0x10) = { offset=0, _0x8=0, ctr_val=0 }

relocation_header  = { offset=0,    size=0x8000, magic="BKTR", ver=1, entries=1 }
subsection_header  = { offset=0x8000, size=0x8000, magic="BKTR", ver=1, entries=1 }
```

关键推论（两个真实样本都吻合）：

* **IVFC 头直接照抄本体的**。零改动补丁的虚拟镜像就是本体镜像，所以 `level_headers[0..5]`
  与 `master_hash` 与本体的 romfs 段完全一致（鬼作本体 `master_hash = 4540657d…`）。
  实测本体 IVFC 与 BKTR 样本同构：`L0..L3 = 0x4000`、`L4` = 哈希表、`L5` = 数据，
  且 `L5.logical_offset = 前几级之和`。
* **`total_size` / `virtual_offset_end` = 本体 romfs 段大小**（鬼作 `0xD7D6C000`），
  它比 `L5.offset + L5.size` 略大（段尾填充）。
* **加密不用改**。hactool 对 BKTR 用 `nca_update_bktr_ctr(ctr, ctr_val, ofs)`；当 `ctr_val=0`
  且 subsection 从 0 开始时，它退化成普通 CTR，与 hacPack 现有加密完全等价。

hactool 认定「Patch RomFS」靠的就是 `crypt_type == CRYPT_BKTR`（`nca.c` 里 BKTR 分支），
**不是** IVFC 的 `id`（普通 RomFS 也是 `0x20000`）。

## 四、内容 NCA 的归属

* Program / Control NCA 头 `title_id` = **本体 ID**；meta NCA = update ID。
* Program NCA 里的 ExeFS `main.npdm`（即 ACID/NPDM）也必须是本体 ID，
  否则 hacPack 会以 `TitleID mismatch! ACI0 TitleID: …` 直接退出。
  注意 `npdmtool` 认的字段是 **`program_id` / `program_id_range_min`**
  （JSON 里后出现的键覆盖先前键；写了两套时以 `program_id` 为准）。
* Control NCA 的 NACP **必须是本体 NACP 的派生**：只有 `display_version` 抬版本，
  `save_data_owner_id` / `presence_group_id` / `add_on_content_base_id` 保持本体。
  **不能用 nacptool 重新生成**（它会把归属写成标题自己的 ID）；
  hacPack 造 Control 时不会动 NACP，所以这一步必须走 hacPack。
  hacbrewpack 会按 `--titleid` 就地重写 `./control/control.nacp` 的归属字段，
  所以更新包的 Control 不能交给它。

## 五、工具与复现

### 1. hacPack 需要 BKTR 补丁

上游 `hacPack`（原仓库已删除，用 fork）只会写 `CRYPT_CTR` / `CRYPT_NONE`，
造不出 Patch RomFS。`local/hacpack` 用的是打过补丁的构建：

* 源码改动：`research/hacPack/nca.c` 的 `nca_create_program()` 段 1 分支新增 BKTR 模式
  （程序 NCA 的 romfs 段改写成上面的 0x10000 叠加表）。
* 启用方式（环境变量）：
  * `HACPACK_BKTR_IVFC` = 存放本体 romfs 段 IVFC 头（0xE0 字节）的文件
  * `HACPACK_BKTR_SIZE` = 本体 romfs 段大小（十六进制）
* 构建：

```sh
cp config.mk.template config.mk && make     # 需要 mbedtls（仓库自带）
```

### 2. 完整流程

```sh
./build-switch.sh                                    # 产出 build-switch/kisaku-runtime.elf
UPDATE=1 BASE_NSP=<本体NSP> HACPACK=<hacpack> \
    ./make-nsp.sh 交付/鬼作-update-01008B538DE50800.nsp
```

`make-nsp.sh` 在 UPDATE 模式下的分工：

1. `[1/6]`/`[2/6]` 用新 ELF 生成 ExeFS（`main` + `main.npdm`，npdm 用**本体 ID**）
2. `[4/6]`/`[5/6]` 调 `tools/make_update_nsp.py` 走完整流水线
3. 本体 NSP 只被**读取少量碎片**，不复制 3.4 GB 游戏数据：
   * meta NCA（4 KB）→ 解出本体 CNMT → 补丁历史
   * Control NCA（560 KB）→ 解出本体 NACP + 图标 → 派生更新包的 Control
   * Program NCA 的**前 0xC00 字节** → 用 AES-128-XTS 解出 NCA 头
     → 取 `fs_header[1]` 的 IVFC 头与 romfs 段大小 → BKTR 参数

   注意 NCA 头解密的 tweak 是**大端**扇区号：
   `openssl enc -d -aes-128-xts -K <header_key> -iv <扇区号大端16字节>`，
   逐 0x200 字节一段（实测与 hactool 解出的明文头逐字节一致）。

## 六、验证清单

打出包后逐项核对（同样适用于 code review）：

* `hactool -t pfs0 -i <nsp>`：3 个成员，meta 在前
* 各 NCA 的 `Content Type` / `Title ID`：Program/Control = 本体，Meta = update
* Program NCA `Section 1: Partition Type: Patch RomFS`、`Size = 0x10000`
* 解 `fs_header[1]`：`02 00 00 03 04`（version2 / partition0 / fs_type3 / **crypt_type4**）
* IVFC 头与本体逐字节一致，`master_hash` 相同
* relocation/subsection 两张表的字段与第三节一致，其余全零
* CNMT：`type=0x81`、`application_id=本体`、`extended_data_size` 与文件长度自洽
  （`0x20 + 0x18 + N*0x38 + ext + 0x20 == 文件长度`）
* Control NACP：`display_version` 已抬版本、`save_data_owner_id` = 本体
* ExeFS `main` 的 sha256 == 新 ELF 经 `strip + elf2nso` 的产物

## 七、已知限制

* 本方案是**零改动叠加**：补丁不替换任何 romfs 文件。要顺带改图集/数据，
  得把对应文件放进叠加表（`is_patch=1` + subsection 表 + 各自的 `ctr_val`），
  这部分尚未实现。
* `relocation`/`subsection` 只实现了单 bucket 单条目（覆盖全部虚拟空间）；
  多 bucket 的分布规律未逆向。
* 签名由 hacPack 用自签密钥生成，需要 sigpatches 才能安装（与本体 NSP 一致）。
* 未验证：把该 update 装到「没装本体」的主机上会怎样。
