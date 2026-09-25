#!/usr/bin/env python3
"""Summarise the complete original-MES native-call inventory.

Input is the TSV emitted by build/sysflow-all.  The walker executes every
original MES entry with real VM bytecode and records each distinct
module/IP/main/sub combination.  This report deliberately keeps calls that
are not reachable from the normal title route: those are the small-game and
appendix paths that a route smoke test misses.
"""
import argparse, json, re
from collections import Counter, defaultdict
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument("tsv",type=Path)
parser.add_argument("--output",type=Path,required=True)
args=parser.parse_args()

rows=[]
for raw in args.tsv.read_text(errors="replace").splitlines()[1:]:
    p=raw.split("\t",5)
    if len(p)<6: continue
    module,ip,main,sub,sp,stack=p
    rows.append({"module":module,"ip":int(ip),"main":int(main),"sub":int(sub),"sp":int(sp),"stack":stack})

by_main=Counter(r["main"] for r in rows)
by_sub=Counter(r["sub"] for r in rows if r["main"]==31)
modules=defaultdict(set)
for r in rows:
    if r["main"]==31: modules[r["sub"]].add(r["module"])

# These are the calls currently decoded by bootstrap_dispatch.  The report
# does not call an unlisted subcall safe: it is only a review aid and the
# runtime still preserves arguments and rejects it.
known={0,1,3,10,11,12,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,
       40,41,43,64,65,66,67,70,110,111,310,320,340,520,521,522,523,524,525,
       526,527,528,612,810,812,910,1010,1011,1012,1013,1020,
       210,610,611,710,711}
observed=sorted(by_sub)
result={
    "rows":len(rows),
    "main_counts":dict(sorted(by_main.items())),
    "main31":{
        "observed_subcalls":observed,
        "counts":{str(k):by_sub[k] for k in observed},
        "modules":{str(k):sorted(modules[k]) for k in observed},
        "not_in_dispatch_inventory":[k for k in observed if k not in known],
    },
    "distinct_sites":len({(r["module"],r["ip"],r["main"],r["sub"]) for r in rows}),
}
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n")
print(json.dumps(result["main31"],ensure_ascii=False,indent=2))
