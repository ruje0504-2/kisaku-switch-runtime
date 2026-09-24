"""Build a local development SD directory without copying original saves/EXE."""
import argparse
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
ARCHIVES = ("data.arc", "layer.arc", "mes.arc", "music.arc", "effect.arc", "voice.arc", "movie.arc")
PROGRAMS = ("kisaku.nro",)
DEFAULT_FONT = "arshanghaisonggbpro_lt.otf"
TRANSLATION = ROOT / "assets/zh_CN.txt"
UI_TRANSLATION = ROOT / "assets/ui_zh_CN.txt"
FONT_DIRS = (ROOT / "assets", ROOT / "local/fonts")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "交付/SD卡根目录")
    args = parser.parse_args()
    source = args.game.resolve()
    target = args.output.resolve() / "switch/kisaku"
    if target == source or source in target.parents or target in source.parents:
        parser.error("output must be separate from original game data")
    required = [source / name for name in ARCHIVES + ("AI6WIN.ini",)]
    required += [ROOT / "build-switch" / name for name in PROGRAMS]
    missing = [str(p) for p in required if not p.is_file()]
    if missing:
        parser.error("missing inputs: " + ", ".join(missing))
    (target / "game").mkdir(parents=True, exist_ok=True)
    (target / "saves").mkdir(exist_ok=True)
    for path in required:
        destination = target / path.name if path.suffix == ".nro" else target / "game" / path.name
        shutil.copy2(path, destination)
    supplied_font = next((d / DEFAULT_FONT for d in FONT_DIRS if (d / DEFAULT_FONT).is_file()), None)
    if supplied_font:
        destination = target / "game" / DEFAULT_FONT
        shutil.copy2(supplied_font, destination)
    if TRANSLATION.is_file():
        destination = target / "game" / TRANSLATION.name
        shutil.copy2(TRANSLATION, destination)
    if UI_TRANSLATION.is_file():
        destination = target / "game" / UI_TRANSLATION.name
        shutil.copy2(UI_TRANSLATION, destination)
    for name in ("README.md", "THIRD_PARTY.md", "LICENSE"):
        shutil.copy2(ROOT / name, target / name)
    shutil.copytree(ROOT / "reports", target / "reports", dirs_exist_ok=True)
    old_programs = ("kisaku-preview.nro", "kisaku-image-viewer.nro", "kisaku-bootstrap.nro", "kisaku-diagnostic.nro")
    for name in old_programs:
        (target / name).unlink(missing_ok=True)
    (target / "build-manifest.json").unlink(missing_ok=True)
    print(target)


if __name__ == "__main__":
    main()
