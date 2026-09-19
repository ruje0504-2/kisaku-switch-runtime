"""Run from project root with pefile/capstone installed; KAWA2_EXE may override EXE.
Produces Switch C data and an independent uncompressed native execution reference.
"""
import json,struct,sys
from pathlib import Path
from native_scene_routes import Native,ENTRIES,HIGHLIGHT_ENTRIES
highlight="--highlight" in sys.argv
if highlight:ENTRIES=HIGHLIGHT_ENTRIES
traces=[Native([1]*1000).run(entry) for entry in ENTRIES]
glyphs=sorted({tuple(q[3:]) for t in traces for q in t})
lines=['/* Native 451f90 route functions; generated and checked by tools/native_scene_routes.py. */','static const struct {uint8_t w,h,x,y;} route_glyphs[]={']
lines += ['{%s},'%','.join(map(str,g)) for g in glyphs]
lines+=['};','static const struct {int16_t x,y;uint16_t flag;uint8_t glyph;} route_sprites[]={'];offsets=[0];reference=bytearray()
for trace in traces:
 reference+=struct.pack('<I',len(trace))
 for row in trace:
  flag,x,y,w,h,sx,sy=row;assert -32768<=x<32768 and -32768<=y<32768 and 0<=flag<1000
  lines.append('{%d,%d,%d,%d},'%(x,y,flag,glyphs.index((w,h,sx,sy))));reference+=struct.pack('<7i',*row)
 offsets.append(offsets[-1]+len(trace))
lines+=['};','static const uint16_t route_offsets[]={',','.join(map(str,offsets)),'};']
Path('runtime/scene_highlight.inc' if highlight else 'runtime/scene_links.inc').write_text(('\n'.join(lines)+'\n').replace('route_', 'highlight_' if highlight else 'route_').replace('451f90','450ab0' if highlight else '451f90'));Path('local').mkdir(exist_ok=True)
Path('local/nav-highlight-reference.bin' if highlight else 'local/nav-native-reference.bin').write_bytes(reference)
Path('local/nav-highlight-traces.json' if highlight else 'local/nav-native-traces.json').write_text(json.dumps(traces))
print('Generated',offsets[-1],'sprites from',len(ENTRIES),'scene dispatch entries')
