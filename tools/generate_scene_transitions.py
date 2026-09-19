"""Extract the original module aliases and directed navigation edge catalog."""
import json,struct,os
from pathlib import Path
import pefile
p=pefile.PE(os.environ.get('KAWA2_EXE','游戏日文数据/game/AI6WIN.exe'));base=p.OPTIONAL_HEADER.ImageBase
lines=['/* Original AI6WIN tables: 4d7cf8 (16-byte aliases), 4d6588 (6-byte edges). */','static const struct {uint16_t id;const char *names[3];} scene_modules[]={']
for i in range(370):
 d=p.get_data(0x4d7cf8-base+i*16,16);ident=struct.unpack_from('<H',d)[0]
 names=[p.get_data(a-base,260).split(b'\0')[0].decode('ascii') for a in struct.unpack_from('<III',d,2)]
 assert ident==i+1
 lines.append('{%d,{%s}},'%(ident,','.join(json.dumps(n) for n in names)))
lines += ['};','static const struct {uint16_t flag,from,to;} scene_edges[]={']
for i in range(1000):
 q=struct.unpack('<HHH',p.get_data(0x4d6588-base+i*6,6));assert q[0]<1000 and q[1]<=370 and q[2]<=370
 lines.append('{%d,%d,%d},'%q)
lines+=['};'];Path('runtime/scene_transitions.inc').write_text('\n'.join(lines)+'\n')
print('Extracted 370 scene alias records and 1000 directed edge records')
