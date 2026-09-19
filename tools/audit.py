"""Reproducible, metadata-only inventory and candidate MES dialect validation."""
import argparse
from collections import Counter
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import struct
from ai6arc import Archive


def scan_mes(data, version):
    if len(data) < 4:
        raise ValueError('missing MES header')
    count, = struct.unpack_from('<I', data)
    base = 4 + count * 4
    if base > len(data):
        raise ValueError('invalid message table')
    ip = base
    boundaries, targets, messages = set(), [], []
    ops = Counter()
    known = set(range(0x1c)) | {0x1d} | set(range(0x32, 0x44)) | set(range(0xfa, 0x100))
    while ip < len(data):
        boundaries.add(ip - base)
        op = data[ip]
        ops[f'{op:02x}'] += 1
        ip += 1
        if op not in known:
            raise ValueError(f'unknown opcode {op:02x} at {ip-1-base:#x}')
        if op in (10, 11, 0x33):
            end = data.find(b'\0', ip)
            if end < 0:
                raise ValueError('unterminated string')
            ip = end + 1
        elif op in (0x14, 0x15, 0x16, 0x19, 0x1a, 0x32):
            if ip + 4 > len(data):
                raise ValueError('truncated operand')
            value, = struct.unpack_from('>I', data, ip)
            if op in (0x14, 0x15, 0x16, 0x1a):
                targets.append(value)
            if op == 0x19:
                messages.append(ip - 1 - base)
            ip += 4
        elif op == 0x1b or (version == 1 and op in (0x10, 0x36)):
            ip += 1
        if ip > len(data):
            raise ValueError('truncated operand')
    bad = [t for t in targets if t not in boundaries]
    header = list(struct.unpack_from(f'<{count}I', data, 4))
    return dict(opcodes=dict(ops), instructions=sum(ops.values()), messages=count,
                invalid_targets=len(bad), message_table_matches=header == messages)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('data', type=Path)
    parser.add_argument('--output', type=Path, default=Path('reports/inventory.json'))
    args = parser.parse_args()
    report = {'archives': {}, 'mes_candidates': {str(v): {'parsed': 0, 'valid': 0, 'failures': [], 'opcodes': Counter()} for v in (0, 1)}}
    for path in sorted(args.data.glob('*.arc')):
        archive = Archive(path)
        # Verify every compressed payload; inspect one header per extension without exporting content.
        samples = {}
        decoded = 0
        for e in archive.entries:
            ext = Path(e.name).suffix.lower()
            payload = None
            if e.packed != e.size or ext not in samples or ext == '.mes':
                payload = archive.read(e)
                decoded += 1
            if ext not in samples:
                samples[ext] = {'entry': asdict(e), 'magic_hex': payload[:24].hex()}
            if ext == '.mes':
                for version in (0, 1):
                    stats = report['mes_candidates'][str(version)]
                    try:
                        result = scan_mes(payload, version)
                        stats['parsed'] += 1
                        stats['opcodes'].update(result['opcodes'])
                        if result['invalid_targets'] == 0 and result['message_table_matches']:
                            stats['valid'] += 1
                        else:
                            stats['failures'].append({'name': e.name, 'invalid_targets': result['invalid_targets'], 'message_table_matches': result['message_table_matches']})
                    except ValueError as error:
                        stats['failures'].append({'name': e.name, 'error': str(error)})
        report['archives'][path.name] = {
            'bytes': path.stat().st_size, 'entries': len(archive.entries),
            'extensions': dict(Counter(Path(e.name).suffix.lower() for e in archive.entries)),
            'compressed_entries': sum(e.size != e.packed for e in archive.entries),
            'payloads_checked': decoded, 'samples': samples,
        }
        print(f'{path.name}: {len(archive.entries)} entries, {decoded} payloads checked', flush=True)
    exe = args.data.parent / 'AI6WIN.exe'
    if exe.exists():
        report['exe'] = {'bytes': exe.stat().st_size, 'sha256': hashlib.sha256(exe.read_bytes()).hexdigest()}
    for v, stats in report['mes_candidates'].items():
        print(f'MES candidate {v}: parsed={stats["parsed"]}, structurally valid={stats["valid"]}', flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    main()
