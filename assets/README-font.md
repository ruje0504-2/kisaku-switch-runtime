# 运行时字体

`arshanghaisonggbpro_lt.otf` 是移植运行时用于**剧情对白与选项文字**的中日文字体。
Switch 上的系统界面（历史、回想、设置、姓名、地点标签等）走 HOS 共享字体，不使用该文件。

- 文件内 name 表：家族名 `思源宋体cn Bold`，版本 `Version 2.00`，由第三方用
  FontCreator 13.0.0.2675 重导出（2022-12-11）。
- 上游：Source Han Serif / Noto Serif CJK（Adobe、Google）。
- 许可：SIL Open Font License 1.1，全文见 `LICENSE-OFL-1.1.txt`。
- 本仓库按用户提供的文件原样随附，未做任何修改。注意 name 表仍保留上游保留字体名
  （思源宋体 / Source），若按 OFL §3 严格解释，重导出属于衍生版本；如需要可在
  上游取得原始 OFL 文件替换。

打包与测试脚本按 `assets/` → `local/fonts/` 的顺序查找该文件（`tools/package_sd.py`
把它复制到 SD 卡与 NSP 的 `game/` 数据目录；`test-host.sh` 与
`tools/test_present_gles.sh` 用它跑字体相关回归）。
