#!/usr/bin/env python3
"""Run deterministic real-script routes in separate temporary save directories."""
import argparse
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

base = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--choice', nargs='+', choices=['first', 'last', 'cycle', 'explore'], default=['first', 'last', 'cycle', 'explore'])
parser.add_argument('--frames', type=int, default=120000)
args = parser.parse_args()
default_source = base / '鬼作' if (base / '鬼作').is_dir() else base.parent / '鬼作'
source = Path(__import__('os').environ.get('KISAKU_GAME', str(default_source)))
logs = base / 'local/routes'
logs.mkdir(parents=True, exist_ok=True)

def run(policy):
    with tempfile.TemporaryDirectory(prefix='kisaku-route-') as directory:
        root = source
        with (logs / f'{policy}.log').open('w') as out, (logs / f'{policy}.stderr').open('w') as err:
            result = subprocess.run([str(base / 'build/kisaku-bootstrap'), str(root), '--new-game', '--frames', str(args.frames), '--choice', policy], stdout=out, stderr=err, env={**__import__('os').environ, 'KISAKU_SAVE_DIR': directory})
        output = (logs / f'{policy}.log').read_text()
        # The bootstrap probe deliberately reports a title return as
        # "gameplay incomplete"; do not mistake that startup diagnostic for
        # a completed route.
        if result.returncode == 0 and 'gameplay incomplete' in output.lower():
            result = subprocess.CompletedProcess(result.args, 3)
        return policy, result.returncode, output

with ThreadPoolExecutor(max_workers=min(3, len(args.choice))) as pool:
    for policy, code, output in pool.map(run, args.choice):
        print(f'Route {policy}: exit {code}\n{output}', flush=True)
