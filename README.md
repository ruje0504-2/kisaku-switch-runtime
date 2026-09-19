# 鬼作 / Kisaku — Switch 移植工程

**当前为 0.1.0 开发预览，已能进入并操作日文标题菜单，剧情仍未接通，不是可通关版本。**

基于 [kawa2-switch-runtime](https://github.com/ruje0504-2/kawa2-switch-runtime)，固定参考提交 `ef076b486ad9a14052402cfa79d39178e7ecdf93`。仅复用其 AI6WIN 运行时基础；该项目对《河原崎家の一族2》的通关、存档及鉴赏验证不能沿用到《鬼作》。原版数据不属于本项目源码或 GPL 许可范围。

## 已实现和验证

- 读取本地原版的七个 ARC 归档，使用 `startup.mes`、`layer.arc`。
- 原生 AKB 解码：24/32 位颜色、Alpha、背景填充、局部矩形、上下行方向与差分重建；2,191 张图片全部通过 C 解码器检查。
- 681 个 MES 脚本全部通过字节码结构检查；启动流程注册 51 个函数。
- 适配 9,192 字节变量区、600 个短整数变量及 15,000 字节原始变量区；创建 14 个图层。
- 根据原程序实现 `27/3` 的窗口显示适配与 `31/10/0` 的文字区初始化（坐标 32,8；560×54）。
- 按原程序适配 raw 区三种内存视图、局部参数赋值、选择框初始化和独立动画状态；日文解码固定 CP932。
- 主机与 Switch 构建通过。启动执行 160 次调用、481 帧，到达日文标题菜单；支持鼠标、方向键和确认，结果写入原版系统变量 18。
- 已接入日文媒体表、消息窗口皮肤与文字区域、日记清空与重绘、参数窗口前三行重绘、CNormalSelect 选项调用、CFuncExec 翻页和 31/524 状态图绘制。未实现的剧情接口保留参数并明确报错。
- 静态反编译识别的 5,307 个函数已导出到 `local/decompiled/`；伪代码需汇编核对，不是完成的移植源码。
- 设置和诊断输出放在单独目录，不读写原版 `save/`。尚未适配的旧存档格式不开放保存。

AKB 的像素方向、裁剪、透明度、背景和坏输入有合成样本测试，并通过 AddressSanitizer/UBSan。批量解码成功不等于所有画面的视觉效果已经逐一确认。

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

# 开发预览：标题菜单可操作，进入剧情仍会报告未支持接口
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

- `kisaku.nro`：主程序，当前可操作日文标题菜单；剧情仍未接通。
- `kisaku-preview.nro`：兼容此前文件名，与 `kisaku.nro` 内容完全一致。
- `kisaku-image-viewer.nro`：原版 AKB 资源查看器，左右切换，X 切换透明混合。
- `kisaku-bootstrap.nro`：启动接口诊断，结果写到 `bootstrap-result.txt`。
- `kisaku-diagnostic.nro`：七个归档和启动脚本检查。

数据放在 `switch/kisaku/game/`，预览设置放在 `switch/kisaku/saves/`。用 HOME 菜单关闭；没有把 + 映射为退出。**本次未进行 Switch 实机测试。**

## 汉化补丁

本阶段只实现日文原版，汉化补丁暂不处理。

## 下一步

1. 根据静态反编译结果继续推进 open.mes 后续分支，接通开场媒体与进度恢复。
2. 补齐普通/附录选择框资源变体和键鼠输入边界。
3. 完成消息窗口可见状态下的原生精灵运动，继续核对《鬼作》的扩展调用表。
4. 适配原生进度、存读档、日程/地图、鉴赏及各小游戏，再做逐路线回归和实机验证。

详见 [移植记录](reports/porting.md)、[资源审计](reports/inventory.json)、[原生接口表](reports/exec-dispatch.json)。`runtime/` 中仍保留参考运行时的其他游戏功能，未通过《鬼作》验证的扩展接口会明确报错，不会静默跳过。

源码采用 GPL-2.0-or-later；来源与依赖见 [THIRD_PARTY.md](THIRD_PARTY.md)。本工程没有上传或发布原版素材、存档或 Windows 程序。
