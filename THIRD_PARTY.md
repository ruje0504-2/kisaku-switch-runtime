# 来源与许可

本工程代码采用 GPL-2.0-or-later，见 LICENSE。原版游戏数据不在此许可范围内。

格式研究参考：

- TesterTesterov/AI6WINArcTool：GPL-2.0，ARC 字段布局和 Silky LZSS 参数。
- TesterTesterov/AI6WINScriptTool：GPL-2.0，AI6 字节码操作数布局及消息表。
- ruje0504-2/kawaxp-switch-runtime：GPL-2.0-or-later，Switch 平台架构研究；`runtime/ax.c` 和 `runtime/ax.h` 由其 AX 播放器改写，保留 GPL-2.0-or-later 许可。
- morkt/GARbro：MIT，`tools/rmt.py` 的格式与重建算法参考 `ArcFormats/elf/ImageRMT.cs`；VSD 流定位参考 `ArcFormats/elf/ArcVSD.cs`。

音视频依赖 FFmpeg（libavformat、libavcodec、libswscale、libswresample），通过 devkitPro / 主机安装的版本链接。分发二进制时应同时提供相应依赖的许可及可获得的对应源码；实际启用组件和许可证以构建所用 FFmpeg 配置为准。SDL2 / SDL2_test 用于前端显示和音频输出。

FreeType用于字体栅格化，按其GPLv2许可选项与本工程链接。Switch 上的系统界面文字由运行设备的 HOS 共享字体提供；剧情对白/选项使用的 `assets/arshanghaisonggbpro_lt.otf` 随本仓库分发（打包时复制到 SD 卡与 NSP 的 `game/` 数据目录），来源与许可见 `assets/README-font.md`：思源宋体（Source Han Serif / Noto Serif CJK，Adobe 与 Google）的第三方重导出文件，适用 SIL Open Font License 1.1，许可全文见 `assets/LICENSE-OFL-1.1.txt`。CP932/GBK码点映射由 `tools/generate_codepages.py` 使用Python标准编解码器生成，未复制Python编解码器实现。

GARbro 相关版权与许可：

Copyright (C) 2017 by morkt (ImageRMT.cs)

Copyright (C) 2015 by morkt (ArcVSD.cs)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.

本移植运行时基于 https://github.com/ruje0504-2/kawa2-switch-runtime （GPL-2.0-or-later）。
AKB 解码参考 https://github.com/morkt/GARbro/blob/master/ArcFormats/Silky/ImageAKB.cs ，Copyright (C) 2015 by morkt，适用上列 MIT 许可。


## AMD FidelityFX FSR 1.0

`tools/fsr1_gles.inc` and the scalar test reference adapt EASU/RCAS from [AMD ffx_fsr1.h](https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/ffx-fsr/ffx_fsr1.h), version v1.20210629. GLES2 sampling and reciprocal operations are adapted; this is not an unmodified official binary.

Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :
The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
