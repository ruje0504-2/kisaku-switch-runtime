"""Extract CBowling numeric tables from the verified Japanese EXE, without running it."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import tempfile
from native_media_tables import EXE_SHA256


def extract(path):
    data = Path(path).read_bytes()
    if hashlib.sha256(data).hexdigest() != EXE_SHA256:
        raise ValueError('Unsupported EXE: Japanese executable SHA256 mismatch')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count, optional_size = struct.unpack_from('<H', data, pe+6)[0], struct.unpack_from('<H', data, pe+20)[0]
    base = struct.unpack_from('<I', data, pe+24+28)[0]
    sections = [struct.unpack_from('<IIII', data, pe+24+optional_size+i*40+8) for i in range(count)]
    def read(va, size):
        for _, rva, length, raw in sections:
            offset = va-base-rva
            if 0 <= offset and offset+size <= length:
                result = data[raw+offset:raw+offset+size]
                if len(result) == size:
                    return result
        raise ValueError(f'Unmapped bowling table: {va:#x}')
    profiles = [list(read(0x5452e8+i*21, 21)) for i in range(9)]
    spread = list(read(0x5453a8, 9))
    # Character 6 has its own rand() path, but retain its table as stored.
    if any(not 1 <= value <= 127 for i, value in enumerate(spread) if i != 6):
        raise ValueError('Invalid bowling random spread')
    def action_stream(va):
        rows = []
        for row in range(256):
            command, delay = struct.unpack('<hH', read(va+row*4, 4))
            if command < -3:
                raise ValueError('Invalid bowling animation command')
            rows.append((command, delay))
            if command == -1:
                return rows
        raise ValueError('Unterminated bowling animation')
    streams = []
    for table, count in [(0x56bb88, 27), (0x56bc18, 3)]:
        for index in range(count):
            streams.append(action_stream(struct.unpack('<I', read(table+index*4, 4))[0]))
    throws = [action_stream(va) for va in (0x542d4c, 0x542dc8, 0x542e44,
              0x542e8c, 0x542edc, 0x542f38, 0x542fa0, 0x543004, 0x54305c)]
    aim = [struct.unpack('<fff', read(0x5453b8+i*12, 12)) for i in range(6)]
    # 4ccebe..4ccf98 constructs character 0's 30 floats on the stack.
    refs = (0,0x548acc,0x548d24, 0,0x548acc,0x548acc,
            0,0x548a78,0x548d20, 0,0x548a78,0x548a78,
            0,0,0x548d1c, 0,0,0, 0,0x548d18,0x548d14,
            0,0x548d18,0x548d10, 0,0x548d0c,0x548d0c,
            0,0x548d0c,0x548d08)
    values = [struct.unpack('<f', read(va,4))[0] if va else 0.0 for va in refs]
    lanes = [[values[i:i+3] for i in range(0,30,3)]]
    lanes += [[struct.unpack('<fff',read(va+i*12,12)) for i in range(10)]
              for va in (0x545400,0x545478,0x5454f0,0x545568)]
    positions = [struct.unpack('<fff', read(0x5431a8+i*12, 12)) for i in range(10)]
    return profiles, spread, streams, positions, throws, aim, lanes


def render(tables):
    profiles, spread, streams, positions, throws, aim, lanes = tables
    lines = ['/* Generated from verified Japanese EXE '+EXE_SHA256+' */',
             'static const unsigned char kisaku_bowling_profiles[9][21]={']
    lines += ['{'+','.join(map(str, row))+'},' for row in profiles]
    lines += ['};', 'static const unsigned char kisaku_bowling_spread[9]={'+','.join(map(str, spread))+'};']
    for index, rows in enumerate(streams):
        lines += [f'static const KBowlingAction kisaku_bowling_action_{index}[]={{']
        lines += ['{'+str(command)+','+str(delay)+'},' for command, delay in rows]
        lines += ['};']
    lines += ['static const KBowlingActionStream kisaku_bowling_actions[30]={']
    lines += [f'{{kisaku_bowling_action_{i},{len(rows)}}},' for i, rows in enumerate(streams)]
    lines += ['};', 'static const float kisaku_bowling_pin_positions[10][3]={']
    lines += ['{'+','.join(float(value).hex()+'f' for value in position)+'},' for position in positions]
    lines += ['};']
    for index, rows in enumerate(throws):
        lines += [f'static const KBowlingAction kisaku_bowling_throw_{index}[]={{']
        lines += ['{'+str(command)+','+str(delay)+'},' for command, delay in rows]
        lines += ['};']
    lines += ['static const KBowlingActionStream kisaku_bowling_throws[9]={']
    lines += [f'{{kisaku_bowling_throw_{i},{len(rows)}}},' for i, rows in enumerate(throws)]
    lines += ['};', 'static const float kisaku_bowling_aim[6][3]={']
    lines += ['{'+','.join(float(v).hex()+'f' for v in row)+'},' for row in aim]
    lines += ['};', 'static const float kisaku_bowling_lanes[5][10][3]={']
    for lane in lanes:
        lines += ['{']
        lines += ['{'+','.join(float(v).hex()+'f' for v in row)+'},' for row in lane]
        lines += ['},']
    return '\n'.join(lines+['};', ''])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe'); parser.add_argument('output')
    args = parser.parse_args()
    output = Path(args.output)
    result = render(extract(args.exe))
    if output.exists() and output.read_text() == result:
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile('w', dir=output.parent, delete=False) as stream:
        stream.write(result); temporary = stream.name
    os.replace(temporary, output)


if __name__ == '__main__':
    main()
