#!/usr/bin/env python3
"""Run deterministic real-script routes in separate temporary save directories."""
import argparse
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

base = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--choice', nargs='+', choices=['first', 'last', 'cycle', 'explore'], default=['first', 'last'])
parser.add_argument('--frames', type=int, default=120000)
args = parser.parse_args()
source = base / '游戏日文数据/game/ELFIMAGE'
logs = base / 'local/routes'
logs.mkdir(parents=True, exist_ok=True)

def run(policy):
    with tempfile.TemporaryDirectory(prefix='kawa2-route-') as directory:
        root = Path(directory)
        for name in ['mes.arc', 'rmt.arc', 'data.arc', 'movie.arc', 'music.arc', 'voice.arc', 'effect.arc', 'AI6WIN.ini', 'save', 'save52', 'save53', 'save54']:
            (root / name).symlink_to(source / name)
        with (logs / f'{policy}.log').open('w') as out, (logs / f'{policy}.stderr').open('w') as err:
            result = subprocess.run([str(base / 'build/kawa2-bootstrap'), str(root), '--new-game', '--frames', str(args.frames), '--choice', policy], stdout=out, stderr=err)
        return policy, result.returncode, (logs / f'{policy}.log').read_text()

with ThreadPoolExecutor(max_workers=min(3, len(args.choice))) as pool:
    for policy, code, output in pool.map(run, args.choice):
        print(f'Route {policy}: exit {code}\n{output}', flush=True)
