from pathlib import Path
import json,random,sys
from importlib.machinery import SourceFileLoader
import native_scene_routes as native
highlight='--highlight' in sys.argv
if highlight:native.ENTRIES=native.HIGHLIGHT_ENTRIES
baseline=json.loads(Path('local/nav-highlight-traces.json' if highlight else 'local/nav-native-traces.json').read_text());rng=random.Random(7421)
cases=[(mode,[value]*1000) for mode in (0,1) for value in (0,1,2,3,255)]
cases += [(i%2,[rng.randrange(4) for _ in range(1000)]) for i in range(24)]
for number,(mode,values) in enumerate(cases):
 for node,entry in enumerate(native.ENTRIES):
  result=native.Native(values,mode).run(entry)
  expect=[];style=0
  for q in baseline[node]:
   value=values[q[0]]
   if not value or mode==1 and value==1:continue
   if mode==1 and value==2:style=1
   expect.append(q[:-1]+[q[-1]+42*style])
  if result!=expect:
   Path('local/nav-verify-mismatch.json').write_text(json.dumps({'case':number,'mode':mode,'node':node+1,'values':values,'actual':result,'expected':expect},indent=2));raise AssertionError((number,node+1))
 print('case',number,'all 370 scene functions match',flush=True)
print('PASS: independent native interpreter agrees with flag-gated sprite tables across 34 full map states')
