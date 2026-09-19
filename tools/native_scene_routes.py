"""Bounded interpreter for original map route functions and drawing primitives.
Only used offline; no Windows code is executed by the Switch runtime.
"""
import pefile,capstone,struct,json,collections,os
from pathlib import Path
P=pefile.PE(os.environ.get('KAWA2_EXE','游戏日文数据/game/AI6WIN.exe'));BASE=P.OPTIONAL_HEADER.ImageBase
MD=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);MD.detail=True
CACHE={}
def ins(at):
 if at not in CACHE:CACHE[at]=next(MD.disasm(P.get_data(at-BASE,16),at,count=1))
 return CACHE[at]
NAV=0x10000;SCENE=0x20000;SCREEN=0x30000;VTABLE=0x40000;DRAW=0x50000
def entries(table):
 result=[]
 for i in range(1,371):
  a=ins(struct.unpack('<I',P.get_data(table-BASE+4*i,4))[0]);result.append(a.operands[0].imm if a.mnemonic=='call' else None)
 return result
ENTRIES=entries(0x452e9c)
HIGHLIGHT_ENTRIES=entries(0x4519bc)
class Native:
 def __init__(self,values,mode=0):
  self.values=values;self.mem={};self.r=collections.defaultdict(int);self.r['esp']=0x100000;self.zero=False;self.sign=False;self.output=[];self.flag=-1
  for a,v in [(0x514e50,NAV),(NAV+4,mode),(NAV+0x10,SCREEN),(NAV+0x1c,0x60000),(SCREEN,VTABLE),(VTABLE+0x44,DRAW)]:self.write(a,v,4)
 def write(self,a,v,n):
  for i in range(n):self.mem[a+i]=(v>>(8*i))&255
 def read(self,a,n):
  if SCENE+0xa3c<=a<SCENE+0xa3c+1000 and n==1:self.flag=a-SCENE-0xa3c;return self.values[self.flag]
  if any(a+i not in self.mem for i in range(n)):
   if NAV<=a<NAV+128:return 0
   raise ValueError(('read',hex(a),n))
  return sum(self.mem[a+i]<<(8*i) for i in range(n))
 def addr(self,a,o):return self.r[a.reg_name(o.mem.base)]+self.r[a.reg_name(o.mem.index)]*o.mem.scale+o.mem.disp
 def get(self,a,o):
  if o.type==capstone.x86.X86_OP_IMM:return o.imm&0xffffffff
  if o.type==capstone.x86.X86_OP_REG:return self.r[a.reg_name(o.reg)]
  return self.read(self.addr(a,o),o.size)
 def put(self,a,o,v):
  v&=(1<<(8*o.size))-1
  if o.type==capstone.x86.X86_OP_REG:self.r[a.reg_name(o.reg)]=v
  else:self.write(self.addr(a,o),v,o.size)
 def push(self,v):self.r['esp']-=4;self.write(self.r['esp'],v,4)
 def pop(self):v=self.read(self.r['esp'],4);self.r['esp']+=4;return v
 def hook(self,target):
  if target==0x44b4e0:self.r['eax']=SCENE
  elif target==0x44ee60:
   self.write(NAV+8,self.pop(),4);self.write(NAV+12,self.pop(),4)
  elif target==0x450380:
   for i in range(4):self.pop()
   self.r['eax']=1
  elif target==DRAW:
   q=[self.pop() for _ in range(8)];assert q[4]==0x60000 and q[7]==1,q
   q=[v if v<0x80000000 else v-0x100000000 for v in q]
   self.output.append([self.flag,q[0],q[1],q[2],q[3],q[5],q[6]])
   self.r['eax']=0
  else:return False
  return True
 def run(self,entry):
  if entry is None:return []
  self.push(0);at=entry
  for budget in range(100000):
   if not at:return self.output
   a=ins(at);op=a.mnemonic;o=a.operands;nextat=at+a.size
   try:
    if op in ('mov','movzx'):self.put(a,o[0],self.get(a,o[1]))
    elif op=='lea':self.put(a,o[0],self.addr(a,o[1]))
    elif op in ('add','sub','xor'):
     x,y=self.get(a,o[0]),self.get(a,o[1]);v=x+y if op=='add' else x-y if op=='sub' else x^y;self.put(a,o[0],v);self.zero=(v&0xffffffff)==0;self.sign=bool(v&0x80000000)
    elif op=='imul':
     assert len(o)==3;self.put(a,o[0],self.get(a,o[1])*self.get(a,o[2]))
    elif op in ('cmp','test'):
     x,y=self.get(a,o[0]),self.get(a,o[1]);v=x-y if op=='cmp' else x&y;self.zero=(v&0xffffffff)==0;self.sign=bool(v&0x80000000)
    elif op=='push':self.push(self.get(a,o[0]))
    elif op=='pop':self.put(a,o[0],self.pop())
    elif op.startswith('ret'):nextat=self.pop();self.r['esp']+=o[0].imm if o else 0
    elif op in ('call','jmp'):
     target=self.get(a,o[0])
     if self.hook(target):
      if op=='jmp':nextat=self.pop()
     else:
      if op=='call':self.push(nextat)
      nextat=target
    elif op in ('je','jne','jle','jg'):
     cond={'je':self.zero,'jne':not self.zero,'jle':self.zero or self.sign,'jg':not self.zero and not self.sign}[op]
     if cond:nextat=self.get(a,o[0])
    elif op=='nop':pass
    else:raise ValueError(op)
   except Exception as e:raise RuntimeError((hex(at),a.mnemonic,a.op_str,str(e))) from e
   at=nextat
  raise RuntimeError('budget exceeded')
if __name__=='__main__':
 traces=[]
 for scene,entry in enumerate(ENTRIES,1):
  trace=Native([1]*1000).run(entry);traces.append(trace)
 print('scenes',len(traces),'sprites',sum(map(len,traces)),'flags',len(set(q[0] for t in traces for q in t)))
 Path('local/nav-native-traces.json').write_text(json.dumps(traces))
