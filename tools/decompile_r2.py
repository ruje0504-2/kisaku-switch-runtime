"""Statically analyze and decompile a local PE through r2ghidra.

No debugging, emulation, or target execution is used. Requires r2pipe/pefile
and a built r2ghidra plugin. Output is local research material, not a port.
"""
import argparse
import concurrent.futures
import hashlib
import json
import pathlib
import re
import time
import pefile
import r2pipe


def isolated_retry(exe, plugin, sleigh, address):
    """Bad inferred stack variables must not crash the remaining export."""
    for use_variables in (True, False):
        r = None
        try:
            r = r2pipe.open(str(exe), flags=['-2', '-e', 'scr.color=0'])
            r.cmd('L ' + str(plugin))
            r.cmd('e r2ghidra.sleighhome=' + str(sleigh))
            if not use_variables:
                r.cmd('e r2ghidra.vars=false')
            r.cmd(f'af @ {address}')
            source = r.cmd(f'pdg @ {address}')
            if '{' in source and 'ghidra decompiler error' not in source.lower():
                return source
        except (OSError, RuntimeError, BrokenPipeError):
            pass
        finally:
            if r:
                try:
                    r.quit()
                except (OSError, RuntimeError, BrokenPipeError):
                    pass
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('exe', type=pathlib.Path)
    ap.add_argument('--plugin', required=True, type=pathlib.Path)
    ap.add_argument('--sleigh', required=True, type=pathlib.Path)
    ap.add_argument('--output', type=pathlib.Path, default=pathlib.Path('local/decompiled'))
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    input_hash = hashlib.sha256(args.exe.read_bytes()).hexdigest()
    manifest = args.output / 'input.json'
    previous = manifest if manifest.exists() else args.output / 'summary.json'
    if previous.exists():
        if json.loads(previous.read_text()).get('exe_sha256') != input_hash:
            raise ValueError('Output belongs to a different executable; use a new output directory')
    elif any(args.output.glob('*.c')):
        raise ValueError('Unverified cached C files; use a new output directory')
    manifest.write_text(json.dumps({'exe_sha256': input_hash, 'exe': str(args.exe.resolve())}, indent=2))
    r = r2pipe.open(str(args.exe.resolve()), flags=['-2', '-e', 'scr.color=0', '-e', 'bin.relocs.apply=true'])
    started = time.time()
    try:
        r.cmd('L ' + str(args.plugin.resolve()))
        r.cmd('e r2ghidra.sleighhome=' + str(args.sleigh.resolve()))
        if 'r2ghidra' not in r.cmd('Lc'):
            raise RuntimeError('r2ghidra plugin was not loaded')
        r.cmd('e r2ghidra.timeout=0')
        print('Analyzing native functions (static only)', flush=True)
        r.cmd('aaa')
        pe = pefile.PE(str(args.exe))
        seeds = set()
        for section in pe.sections:
            if not section.Characteristics & 0x20000000:
                continue
            data = section.get_data()
            base = pe.OPTIONAL_HEADER.ImageBase + section.VirtualAddress
            for match in re.finditer(b'\xcc\x55\x8b\xec', data):
                seeds.add(base + match.start() + 1)
        known = {f['addr'] for f in r.cmdj('aflj')}
        for address in sorted(seeds - known):
            r.cmd(f'af @ 0x{address:x}')
        functions = r.cmdj('aflj')
        (args.output / 'functions.json').write_text(json.dumps(functions, indent=2))
        print('Functions:', len(functions), flush=True)
        # Initialize Sleigh in the parent before using the plugin's fork timeout.
        # This also keeps malformed functions from terminating the entire export.
        warm = r.cmd(f"pdg @ 0x{functions[0]['addr']:x}")
        if '{' not in warm:
            raise RuntimeError('Decompiler initialization produced no C')
        r.cmd('e r2ghidra.timeout=15')
        results = []
        for i, function in enumerate(functions):
            address = function['addr']
            target = args.output / f'{address:08x}.c'
            source = target.read_text() if target.exists() and target.stat().st_size else r.cmd(f'pdg @ 0x{address:x}')
            ok = bool(source.strip()) and '{' in source and 'ghidra decompiler error' not in source.lower()
            if i == 0 and not ok:
                raise RuntimeError('Decompiler produced no C for the first function; inspect plugin configuration')
            target.write_text(source)
            results.append({'address': hex(address), 'name': function['name'], 'decompiled': ok})
            if i % 200 == 0:
                print(f'Exported {i}/{len(functions)} functions', flush=True)
        failed = [item for item in results if not item['decompiled']]
        def retry(item):
            source = isolated_retry(args.exe.resolve(), args.plugin.resolve(), args.sleigh.resolve(), item['address'])
            if source:
                (args.output / f"{int(item['address'], 16):08x}.c").write_text(source)
                item['decompiled'] = True
        if failed:
            print(f'Retrying {len(failed)} functions with independent analysis', flush=True)
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                list(pool.map(retry, failed))
        summary = {'exe_sha256': input_hash,
                   'functions': len(results), 'decompiled': sum(x['decompiled'] for x in results),
                   'elapsed_seconds': round(time.time() - started, 1), 'results': results,
                   'limitations': 'Recovered pseudocode requires assembly verification; indirect calls and types may be unresolved.'}
        (args.output / 'summary.json').write_text(json.dumps(summary, indent=2))
        print('Completed:', {k: v for k, v in summary.items() if k != 'results'}, flush=True)
    finally:
        r.quit()


if __name__ == '__main__':
    main()
