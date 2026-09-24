#!/usr/bin/env bash
# 打包《鬼作》直装 NSP：游戏数据进标题 RomFS，存档走 HOS SaveData。
# 运行时侧见 runtime/switch_hos.c；hbmenu 下的 NRO 布局不受影响。
#
# 前置:
#   - /opt/devkitpro（devkitA64 + libnx + tools: elf2nso/npdmtool/nacptool/hactool）
#   - ~/.switch/prod.keys（hacbrewpack 需要）
#   - ~/bin/hacbrewpack（v3.05）
#   - 已由 ./build-switch.sh 产出 build-switch/kisaku-runtime.elf
#   - 游戏数据位于 交付/SD卡根目录/switch/kisaku/game（有版权，不入库）
#   - icon.png（256x256 会转成 JPEG 写进 Control NCA）
#
# 用法: ./make-nsp.sh [输出路径]
#   ROMFS=<目录>      换成小数据冒烟打包（默认用完整游戏数据）
#   TITLE_ID=...      覆盖标题 ID（默认见下，改 ID 会让已装存档找不到）
#   SAVE_SIZE=0x...   覆盖 NACP 存档配额
set -e
cd "$(dirname "$0")"

# 用户指定：0100 = 应用前缀（与参考工程同形状）+ 用户给定的 8B538DE5 + 0000 尾缀。
# 注意中段是用户指定的值；鬼作在 CP932 下是 8B53(鬼)+8DEC(作)，即 8B538DEC。
# 这个 ID 决定存档所在位置，装完后改 ID 会读不到旧存档。
TITLE_ID="${TITLE_ID:-01008B538DE50000}"
TITLE_NAME="${TITLE_NAME:-鬼作}"
PUBLISHER="${PUBLISHER:-elf}"
TITLE_VERSION="${TITLE_VERSION:-1.0.0}"
ICON="${ICON:-$PWD/icon.png}"
SWELF="${SWELF:-$PWD/build-switch/kisaku-runtime.elf}"
DATA="${ROMFS:-$PWD/交付/SD卡根目录/switch/kisaku/game}"
OUT="${1:-$PWD/交付/鬼作-$TITLE_ID.nsp}"
# 每槽实际占用：flag 28,072 + control 12,160 + preview 338,704 + scene 1,228,808
# = 1,607,744 B ≈ 1.53 MiB（4 个选择器 × 100 槽 = 400 槽 ≈ 613 MiB）。
# nacptool 的默认 62 MiB 存不到十几个槽就会写失败，所以显式抬到 640 MiB。
SAVE_SIZE="${SAVE_SIZE:-0x28000000}"       # 640 MiB
JOURNAL_SIZE="${JOURNAL_SIZE:-0x02000000}" #  32 MiB
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

[ -f "$ICON" ] || { echo "缺少图标: $ICON" >&2; exit 1; }
[ -f "$SWELF" ] || { echo "缺少 Switch ELF: $SWELF（请先 ./build-switch.sh）" >&2; exit 1; }
[ -d "$DATA" ] || { echo "缺少游戏数据: $DATA" >&2; exit 1; }
[ -f "$HOME/.switch/prod.keys" ] || { echo "缺少 ~/.switch/prod.keys" >&2; exit 1; }
[ -x "$HOME/bin/hacbrewpack" ] || { echo "缺少 ~/bin/hacbrewpack" >&2; exit 1; }

mkdir -p "$TMP/exefs" "$TMP/control" "$TMP/romfs"
export PATH="/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:$PATH"

echo "[1/6] strip + elf2nso"
aarch64-none-elf-strip "$SWELF" -o "$TMP/exefs-main.elf"
elf2nso "$TMP/exefs-main.elf" "$TMP/exefs/main"

echo "[2/6] npdm（titleid $TITLE_ID）"
cat > "$TMP/npdm.json" <<EOF
{
  "name": "kisaku",
  "title_id": "0x$TITLE_ID",
  "title_id_range_min": "0x$TITLE_ID",
  "title_id_range_max": "0x01ffffffffffffff",
  "main_thread_stack_size": "0x100000",
  "main_thread_priority": 44,
  "default_cpu_id": 0,
  "process_category": 0,
  "pool_partition": 0,
  "is_64_bit": true,
  "address_space_type": 1,
  "is_retail": true,
  "filesystem_access": {
    "permissions": "0xFFFFFFFFFFFFFFFF"
  },
  "service_host": [
    "*"
  ],
  "service_access": [
    "*"
  ],
  "kernel_capabilities": [
    {
      "type": "kernel_flags",
      "value": {
        "highest_thread_priority": 59,
        "lowest_thread_priority": 28,
        "highest_cpu_id": 2,
        "lowest_cpu_id": 0
      }
    },
    {
      "type": "syscalls",
      "value": {
        "svcUnknown00": "0x00",
        "svcSetHeapSize": "0x01",
        "svcSetMemoryPermission": "0x02",
        "svcSetMemoryAttribute": "0x03",
        "svcMapMemory": "0x04",
        "svcUnmapMemory": "0x05",
        "svcQueryMemory": "0x06",
        "svcExitProcess": "0x07",
        "svcCreateThread": "0x08",
        "svcStartThread": "0x09",
        "svcExitThread": "0x0A",
        "svcSleepThread": "0x0B",
        "svcGetThreadPriority": "0x0C",
        "svcSetThreadPriority": "0x0D",
        "svcGetThreadCoreMask": "0x0E",
        "svcSetThreadCoreMask": "0x0F",
        "svcGetCurrentProcessorNumber": "0x10",
        "svcSignalEvent": "0x11",
        "svcClearEvent": "0x12",
        "svcMapSharedMemory": "0x13",
        "svcUnmapSharedMemory": "0x14",
        "svcCreateTransferMemory": "0x15",
        "svcCloseHandle": "0x16",
        "svcResetSignal": "0x17",
        "svcWaitSynchronization": "0x18",
        "svcCancelSynchronization": "0x19",
        "svcArbitrateLock": "0x1A",
        "svcArbitrateUnlock": "0x1B",
        "svcWaitProcessWideKeyAtomic": "0x1C",
        "svcSignalProcessWideKey": "0x1D",
        "svcGetSystemTick": "0x1E",
        "svcConnectToNamedPort": "0x1F",
        "svcSendSyncRequestLight": "0x20",
        "svcSendSyncRequest": "0x21",
        "svcSendSyncRequestWithUserBuffer": "0x22",
        "svcSendAsyncRequestWithUserBuffer": "0x23",
        "svcGetProcessId": "0x24",
        "svcGetThreadId": "0x25",
        "svcBreak": "0x26",
        "svcOutputDebugString": "0x27",
        "svcReturnFromException": "0x28",
        "svcGetInfo": "0x29",
        "svcFlushEntireDataCache": "0x2A",
        "svcFlushDataCache": "0x2B",
        "svcMapPhysicalMemory": "0x2C",
        "svcUnmapPhysicalMemory": "0x2D",
        "svcGetDebugFutureThreadInfo": "0x2E",
        "svcGetLastThreadInfo": "0x2F",
        "svcGetResourceLimitLimitValue": "0x30",
        "svcGetResourceLimitCurrentValue": "0x31",
        "svcSetThreadActivity": "0x32",
        "svcGetThreadContext3": "0x33",
        "svcWaitForAddress": "0x34",
        "svcSignalToAddress": "0x35",
        "svcUnknown36": "0x36",
        "svcUnknown37": "0x37",
        "svcUnknown38": "0x38",
        "svcUnknown39": "0x39",
        "svcUnknown3a": "0x3A",
        "svcUnknown3b": "0x3B",
        "svcDumpInfo": "0x3C",
        "svcDumpInfoNew": "0x3D",
        "svcUnknown3e": "0x3E",
        "svcUnknown3f": "0x3F",
        "svcCreateSession": "0x40",
        "svcAcceptSession": "0x41",
        "svcReplyAndReceiveLight": "0x42",
        "svcReplyAndReceive": "0x43",
        "svcReplyAndReceiveWithUserBuffer": "0x44",
        "svcCreateEvent": "0x45",
        "svcUnknown46": "0x46",
        "svcUnknown47": "0x47",
        "svcMapPhysicalMemoryUnsafe": "0x48",
        "svcUnmapPhysicalMemoryUnsafe": "0x49",
        "svcSetUnsafeLimit": "0x4A",
        "svcCreateCodeMemory": "0x4B",
        "svcControlCodeMemory": "0x4C",
        "svcSleepSystem": "0x4D",
        "svcReadWriteRegister": "0x4E",
        "svcSetProcessActivity": "0x4F",
        "svcCreateSharedMemory": "0x50",
        "svcMapTransferMemory": "0x51",
        "svcUnmapTransferMemory": "0x52",
        "svcDebugActiveProcess": "0x60",
        "svcBreakDebugProcess": "0x61",
        "svcTerminateDebugProcess": "0x62",
        "svcGetDebugEvent": "0x63",
        "svcContinueDebugEvent": "0x64",
        "svcGetProcessList": "0x65",
        "svcGetThreadList": "0x66",
        "svcGetDebugThreadContext": "0x67",
        "svcSetDebugThreadContext": "0x68",
        "svcQueryDebugProcessMemory": "0x69",
        "svcReadDebugProcessMemory": "0x6A",
        "svcWriteDebugProcessMemory": "0x6B",
        "svcSetHardwareBreakPoint": "0x6C",
        "svcGetDebugThreadParam": "0x6D",
        "svcConnectToPort": "0x72",
        "svcSetProcessMemoryPermission": "0x73",
        "svcMapProcessMemory": "0x74",
        "svcUnmapProcessMemory": "0x75",
        "svcQueryProcessMemory": "0x76",
        "svcMapProcessCodeMemory": "0x77",
        "svcUnmapProcessCodeMemory": "0x78"
      }
    },
    {
      "type": "application_type",
      "value": 1
    },
    {
      "type": "min_kernel_version",
      "value": "0x30"
    },
    {
      "type": "handle_table_size",
      "value": 512
    },
    {
      "type": "debug_flags",
      "value": {}
    }
  ],
  "program_id": "0x$TITLE_ID",
  "program_id_range_min": "0x$TITLE_ID",
  "program_id_range_max": "0x01ffffffffffffff"
}
EOF
npdmtool "$TMP/npdm.json" "$TMP/exefs/main.npdm"

echo "[3/6] nacp + 图标"
nacptool --create "$TITLE_NAME" "$PUBLISHER" "$TITLE_VERSION" "$TMP/control/control.nacp"
# nacptool 只填部分语言槽，这里按 libnx 的 NacpStruct 补齐 16 个槽，并把存档
# 配额写足。语言槽是交错的：NacpLanguageEntry { name[0x200]; author[0x100]; }
# 每条 0x300，name 在 0x300*i、author 在 0x300*i+0x200。
# NACP 偏移（libnx switch/nacp.h）：0x3080 UserAccountSaveDataSize、
# 0x3088 UserAccountSaveDataJournalSize。
python3 - "$TMP/control/control.nacp" "$TITLE_NAME" "$PUBLISHER" "$SAVE_SIZE" "$JOURNAL_SIZE" <<'PYEOF'
import struct, sys
path, name, publisher, save, journal = sys.argv[1:6]
d = bytearray(open(path, 'rb').read())
assert len(d) == 0x4000, len(d)
nb, pb = name.encode('utf-8'), publisher.encode('utf-8')
assert len(nb) < 0x200 and len(pb) < 0x100
for i in range(16):
    o = 0x300 * i
    d[o:o + 0x200] = nb + b'\0' * (0x200 - len(nb))
    d[o + 0x200:o + 0x300] = pb + b'\0' * (0x100 - len(pb))
struct.pack_into('<Q', d, 0x3080, int(save, 0))
struct.pack_into('<Q', d, 0x3088, int(journal, 0))
open(path, 'wb').write(d)
print(f"nacp: 16 语言槽 name={name!r} publisher={publisher!r}; "
      f"存档 {int(save,0)//1024//1024} MiB + {int(journal,0)//1024//1024} MiB journal")
PYEOF
# Switch 图标固定 256x256 baseline JPEG。优先用系统工具，其次 ImageMagick，
# 最后才回退到 Pillow（本机未必装）。
ICON_JPEG="$TMP/icon.jpg"
if command -v sips >/dev/null 2>&1; then
    sips -s format jpeg -z 256 256 "$ICON" --out "$ICON_JPEG" >/dev/null
    sips -d all "$ICON_JPEG" >/dev/null 2>&1 || true
elif command -v magick >/dev/null 2>&1; then
    magick "$ICON" -resize 256x256! -strip -quality 90 "$ICON_JPEG"
elif command -v convert >/dev/null 2>&1; then
    convert "$ICON" -resize 256x256! -strip -quality 90 "$ICON_JPEG"
else
    python3 - "$ICON" "$ICON_JPEG" <<'PYEOF'
from PIL import Image
import sys
Image.open(sys.argv[1]).convert('RGB').resize((256, 256), Image.LANCZOS).save(sys.argv[2], 'JPEG', quality=90)
PYEOF
fi
# hacbrewpack 按语言名找图标；全部 16 个槽都放同一张，避免非美区显示空白。
for lang in AmericanEnglish BritishEnglish Japanese French German LatAmSpanish Spanish \
            Italian Dutch CanFrench Portuguese Russian Korean TradChinese SimpChinese Reserved; do
    cp "$ICON_JPEG" "$TMP/control/icon_$lang.dat"
done
python3 - "$ICON_JPEG" <<'PYEOF'
import sys
d = open(sys.argv[1], 'rb').read()
assert d[:2] == b'\xff\xd8', 'not a JPEG'
i, w, h, progressive = 2, None, None, False
while i < len(d) - 1:
    if d[i] != 0xFF:
        i += 1; continue
    m = d[i + 1]
    if m in (0xD8, 0x01) or 0xD0 <= m <= 0xD7:
        i += 2; continue
    if m == 0xDA:
        break
    seg = int.from_bytes(d[i + 2:i + 4], 'big')
    if m == 0xC2:
        progressive = True
    if 0xC0 <= m <= 0xCF and m not in (0xC4, 0xC8, 0xCC):
        h = int.from_bytes(d[i + 5:i + 7], 'big')
        w = int.from_bytes(d[i + 7:i + 9], 'big')
    i += 2 + seg
assert (w, h) == (256, 256), f'icon must be 256x256, got {w}x{h}'
assert not progressive, 'progressive JPEG is not accepted by HOS'
print(f"icon: 256x256 baseline JPEG, {len(d)} B")
PYEOF

echo "[4/6] 拷贝游戏数据到 RomFS"
cp -R "$DATA"/. "$TMP/romfs/"
du -sh "$TMP/romfs" | awk '{print "romfs:", $1}'

echo "[5/6] hacbrewpack 打包 NSP"
mkdir -p "$(dirname "$OUT")"
OUT="$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"
cd "$TMP"
"$HOME/bin/hacbrewpack" --keyset "$HOME/.switch/prod.keys" \
    --titleid "$TITLE_ID" --titlename "$TITLE_NAME" --titlepublisher "$PUBLISHER" \
    --nologo >/dev/null
NSP_RESULT="$(find hacbrewpack_nsp -maxdepth 1 -type f -name '*.nsp' -print -quit)"
[ -n "$NSP_RESULT" ] || { echo "hacbrewpack 未生成 NSP" >&2; exit 1; }
cp "$NSP_RESULT" "$OUT"

echo "[6/6] 自检：从产物 NSP 的 Control NCA 读回 NACP"
# hacbrewpack 会按 --titlename/--titlepublisher 重写 16 个语言槽，所以必须从
# 最终 NCA 读回，而不是只看打包前的 control.nacp。
HACTOOL="/opt/devkitpro/tools/bin/hactool"
if [ -x "$HACTOOL" ]; then
  rm -rf "$TMP/verify" && mkdir -p "$TMP/verify"
  "$HACTOOL" -t pfs0 -k "$HOME/.switch/prod.keys" --outdir="$TMP/verify" "$OUT" >/dev/null 2>&1 || true
  CTRL=""
  for f in "$TMP/verify"/*.nca; do
    case "$f" in *cnmt.nca) continue;; esac
    if "$HACTOOL" -t nca -k "$HOME/.switch/prod.keys" "$f" 2>/dev/null | grep -q 'Content Type: *Control'; then CTRL="$f"; break; fi
  done
  if [ -n "$CTRL" ]; then
    rm -rf "$TMP/nacp" && mkdir -p "$TMP/nacp"
    "$HACTOOL" -t nca -k "$HOME/.switch/prod.keys" --romfsdir="$TMP/nacp" "$CTRL" >/dev/null 2>&1
    python3 - "$TMP/nacp/control.nacp" "$TITLE_NAME" "$PUBLISHER" "$SAVE_SIZE" <<'PYEOF'
import struct, sys
path, name, author, save = sys.argv[1:5]
d = open(path, 'rb').read()
nb, pb = name.encode('utf-8'), author.encode('utf-8')
langs = ['AmericanEnglish','BritishEnglish','Japanese','French','German','LatAmSpanish',
         'Spanish','Italian','Dutch','CanFrench','Portuguese','Russian','Korean',
         'TradChinese','SimpChinese','Reserved']
bad = []
for i in range(16):
    n = d[0x300*i:0x300*i+0x200].split(b'\0')[0].decode('utf-8','replace')
    a = d[0x300*i+0x200:0x300*i+0x300].split(b'\0')[0].decode('utf-8','replace')
    if n != name or a != author: bad.append(f'{langs[i]}: name={n!r} author={a!r}')
if bad:
    print('  NACP 自检失败:'); [print('   ', b) for b in bad]; sys.exit(1)
got = struct.unpack_from('<Q', d, 0x3080)[0]
want = int(save, 0)
if got != want:
    print(f'  NACP 存档配额自检失败: {got} != {want}'); sys.exit(1)
print(f'  NACP 16 个语言槽: name={name!r} author={author!r} ✓')
print(f'  存档配额: {got//1024//1024} MiB + '
      f'{struct.unpack_from("<Q", d, 0x3088)[0]//1024//1024} MiB journal ✓')
PYEOF
  else
    echo "  （未找到 Control NCA，跳过 NACP 自检）"
  fi
else
  echo "  （未找到 hactool，跳过 NACP 自检）"
fi
ls -la "$OUT" | awk '{print "NSP:", $5, "bytes", $NF}'
echo "完成: $OUT"
