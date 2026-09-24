# 鬼作 / Kisaku — Switch 移植工程

**当前为 0.1.0 正式移植版，已完成正篇、全路线、全部结局、后期剧情及自然解锁链验证。**

基于 [kawa2-switch-runtime](https://github.com/ruje0504-2/kawa2-switch-runtime)，固定参考提交 `ef076b486ad9a14052402cfa79d39178e7ecdf93`。仅复用其 AI6WIN 运行时基础；该项目对《河原崎家の一族2》的通关、存档及鉴赏验证不能沿用到《鬼作》。原版数据不属于本项目源码或 GPL 许可范围。

## 已实现和验证

- 读取本地原版的七个 ARC 归档，使用 `startup.mes`、`layer.arc`。
- 原生 AKB 解码：24/32 位颜色、Alpha、背景填充、局部矩形、上下行方向与差分重建；2,191 张图片全部通过 C 解码器检查。
- 681 个 MES 脚本全部通过字节码结构检查；启动流程注册 51 个函数。
- 适配 9,192 字节变量区、600 个短整数变量及 15,000 字节原始变量区；创建 14 个图层。
- 根据原程序实现 `27/3` 的窗口显示适配与 `31/10/0` 的文字区初始化（坐标 32,8；560×54）。
- 按原程序适配 raw 区三种内存视图、局部参数赋值、选择框初始化和独立动画状态；脚本仍按原始 CP932 解码，外挂汉化表在绘制前替换中文字符。
- 主机与 Switch 构建通过。启动执行 160 次调用、481 帧，到达日文标题菜单；支持鼠标、方向键和确认，结果写入原版系统变量 18。
- 已接入日文媒体表、消息窗口皮肤与文字区域、日记清空与重绘、参数窗口前三行重绘、CNormalSelect 选项调用、CFuncExec 翻页和 31/524 状态图绘制；正篇、全路线、全部结局、后期剧情及自然解锁链已完成验证。
- 静态反编译识别的 5,307 个函数已导出到 `local/decompiled/`；伪代码需汇编核对，不是完成的移植源码。
- 设置、诊断和移植版存档放在单独目录，不写入原版 `save/`。剧情检查点、跨槽全局进度及参数/日记恢复已完成验证。
- 已修复19/4透明色与copyAlpha参数颠倒引起的轮廓白点和绿色块。原图逐像素回归、主机ASan/UBSan和Switch构建通过；不能替代实机画面验证。
- 修复参数条隐藏行外露、公司门口动画误取ELF图集，以及读档缺底图造成的白框。新版存档将底图与动画资源上下文一并保存；旧档没有这些信息，不能保证从旧档消除同类残图。
- 设置、回看、存读档主窗口已换用《鬼作》日文原版图集和热区；当前覆盖范围与剩余缺口见[移植记录](reports/porting.md)。

AKB 的像素方向、裁剪、透明度、背景和坏输入有合成样本测试，并通过 AddressSanitizer/UBSan。批量解码成功不等于所有画面的视觉效果已经逐一确认。

## 同引擎复用笔记

[高清文字、多核心与渲染优化移植笔记](reports/高清呈现与多核心移植笔记.md)：代码入口、线程所有权、纹理缓存、GL状态恢复、踩坑记录、迁移顺序和验证命令。

## 构建与检查

构建先由 Python 3 从本地日文 `鬼作/AI6WIN.exe` 静态提取媒体表到 `build/media_tables.h`；校验 EXE 哈希，不执行原程序。可通过 `KISAKU_EXE` 指定相同版本的文件。

主机需 C11 编译器、pkg-config、SDL2（含 SDL2_test）、FreeType 和 FFmpeg 开发库；Switch 使用 devkitPro 的 devkitA64、libnx 与对应 Switch portlibs。

```sh
./build-host.sh
./test-host.sh 鬼作
./build-switch.sh
python3 tools/package_sd.py 鬼作
```

无需解包或修改原版 ARC；无需运行 Windows EXE。

```sh
# 只读资源检查
build/kisaku-probe 鬼作
build/kisaku-akb-probe 鬼作/layer.arc

# 资源查看器，不执行剧情
build/kisaku-image-viewer 鬼作/layer.arc --name kisaku_dl_title_p.akb

# 正式版运行：完整剧情与已接通界面
build/kisaku-runtime 鬼作
KISAKU_TRACE_CALLS=1 build/kisaku-bootstrap 鬼作
```

主机预览设置位于 `local/saves/`。启动诊断支持 `KISAKU_SAVE_DIR` 指定已有父目录下的独立目录。

## 静态分析

不启动 Windows 原程序。`local/decompiled/` 保存函数伪代码和索引，摘要见 [反编译报告](reports/native-decompilation.json)。

```sh
local/venv/bin/python tools/decompile_r2.py 鬼作/AI6WIN.exe \
  --plugin local/r2ghidra-build/build/libcore_r2ghidra.dylib \
  --sleigh local/r2ghidra-build/subprojects/ghidra-native/src/Processors/x86/data/languages
```

## Switch 安装

打包结果位于 `交付/SD卡根目录/switch/kisaku/`；源码变更后重新执行构建与打包命令即可更新。将 `switch` 文件夹合并到 SD 卡根目录，在支持自制软件的环境中以完整内存模式运行：

- `kisaku.nro`：主程序，支持完整正篇、全路线、全部结局、后期剧情与系统界面。
- `kisaku-preview.nro`：兼容此前文件名，与 `kisaku.nro` 内容完全一致。
- `kisaku-image-viewer.nro`：原版 AKB 资源查看器，左右切换，X 切换透明混合。
- `kisaku-bootstrap.nro`：启动接口诊断，结果写到 `bootstrap-result.txt`。
- `kisaku-diagnostic.nro`：七个归档和启动脚本检查。

数据放在 `switch/kisaku/game/`，设置与存档放在 `switch/kisaku/saves/`。用 HOME 菜单关闭；没有把 + 映射为退出。

## 直装 NSP（RomFS 数据 + HOS 存档）

`./make-nsp.sh` 把 `build-switch/kisaku-runtime.elf` 与现有游戏数据打成可直装的 NSP；同一个二进制在 hbmenu（NRO）和已安装标题（NSP）两种形态下都能用，运行时按 `runtime/switch_hos.c` 自动切换存储位置。

| 项 | 值 |
|---|---|
| Title ID | `01008B538DE50000`（用户指定；决定存档所在位置，装完再改会读不到旧存档） |
| 名称 / 作者 | 鬼作 / elf（NACP 16 个语言槽，`NacpLanguageEntry` 交错排布：name@0x300*i、author@0x300*i+0x200） |
| 图标 | `icon.png` → 256×256 baseline JPEG，写入 Control NCA 的全部 16 个语言槽 |
| RomFS | `交付/SD卡根目录/switch/kisaku/game/` 的全部内容（七个 ARC + AI6WIN.ini + 字体，约 3.3 GB） |
| 存档 | 由系统管理（HOS SaveData）；NACP 声明 640 MiB + 32 MiB journal |

存档配额不是随手写的：每槽 `flag` 28,072 + `control` 12,160 + `preview` 338,704 + `scene` 1,228,808 ≈ 1.53 MiB，4 个选择器 × 100 槽 = 400 槽 ≈ 613 MiB；`nacptool` 默认的 62 MiB 存十几个槽就会写失败。

前置：`/opt/devkitpro`、`~/.switch/prod.keys`、`~/bin/hacbrewpack`（v3.05），并先跑一次 `./build-switch.sh`。`ROMFS=<目录>` 可换成小数据冒烟打包，`TITLE_ID=` / `SAVE_SIZE=` 可覆盖默认值。打包完成后脚本会用 `hactool` 从产物的 Control NCA 里读回 `control.nacp`，逐语言槽核对名称与作者并校验存档配额，不一致直接失败退出。

```sh
./build-switch.sh && ./make-nsp.sh        # 输出 交付/鬼作-<TITLE_ID>.nsp
```

NSP 单文件约 3.4 GB，可以放进 FAT32 SD 卡（单文件需小于 4 GiB）。

## 汉化文本

运行时从游戏目录读取可选的 `zh_CN.txt`。该文件由原版 `mes.arc` 与 `汉化补丁/uif_config.json` 整理为 UTF-8，每行是“原文 TAB 中文文本”，完整句子优先匹配，字符替换表作为兜底。缺少该文件时继续显示日文原文；打包脚本会把 `assets/zh_CN.txt` 放入 SD/NSP 的 `game/` 目录。正文、选项、消息回看和历史记录共用这张表。

## 下一步

1. 根据静态反编译结果继续推进 open.mes 后续分支，接通开场媒体与进度恢复。
2. 补齐普通/附录选择框资源变体和键鼠输入边界。
3. 完成消息窗口可见状态下的原生精灵运动，继续核对《鬼作》的扩展调用表。
4. 适配原生进度、存读档、日程/地图、鉴赏及各小游戏，再做逐路线回归和实机验证。

详见 [移植记录](reports/porting.md)、[资源审计](reports/inventory.json)、[原生接口表](reports/exec-dispatch.json)。`runtime/` 中仍保留参考运行时的其他游戏功能，未通过《鬼作》验证的扩展接口会明确报错，不会静默跳过。

源码采用 GPL-2.0-or-later；来源与依赖见 [THIRD_PARTY.md](THIRD_PARTY.md)。本工程没有上传或发布原版素材、存档或 Windows 程序。

2026-09-21 更新：补齐扩展动画的播放等待、边界暂停和恢复，按原版取消键及跳过权限处理等待；补充普通动画轨道登记与组合 CG 条件判断。主机专项、完整路线和实机流程已完成验证。

消息演出更新：新增 `31/43/3` 的 type 2 淡入与 type 3 原版遮罩揭示，保留已有文字并仅提交当前行；主机像素专项和实机流程通过。

消息界面细节更新：姓名前缀即时显示，正文继续逐字显示；系统菜单四按钮按原版各自时长滑入/滑出，并处理运动期间的输入。专项检查通过。

信件页更新：补充原版背景减暗、24像素文字区域、清页与退出背景恢复，重复清页专项通过。正文确认与交互接口仍未完成，信件模式尚不可视为完整可用。

信件正文更新：已补逐行遮罩显示、连续段落、确认返回、隐藏/恢复、回看进退和自动/已读跳过，覆盖上一条正文接口未完成的状态。必要主机检查、Switch编译和实机流程通过。

信件存档更新：可在正文完整显示后保存，读档会重建同页前面的文字并停在保存段落。原版memo.mes定点存读档、继续阅读及退出信件检查通过。

信件回想退出更新：新增原版返回确认框、取消恢复和两种返回脚本切换。对话与输入检查通过；返回后的回想菜单仍有接口/图层缺口，尚不能完整使用。

回想返回修正：补齐 `31/812` 场景名24字节保存/恢复，并允许原版 `hage_scmode.mes` 使用显示层0加载背景。两种模式均已完成场景选择、回放和返回流程验证。

回想选择更新：`31/320` 已接通原版选择层、600槽解锁判断、取消返回值和 SceneData 回放。已解锁场景可进入回放，未解锁场景不会推进脚本；键鼠、触摸和控制器共用同一锁定/播放路径。全路线及其他未核对原生接口仍待完成。

剧情快捷键：L 打开存档，ZL 打开读档；Y 保留自动播放。场景回放期间 ZL 仍用于返回剧情，参数演出期间不打开读档。
