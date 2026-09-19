"""Read-only MSVC RTTI/vtable inventory; requires pefile (local research only)."""
import argparse,struct,json
import pefile
p=argparse.ArgumentParser();p.add_argument('exe');args=p.parse_args()
pe=pefile.PE(args.exe);d=bytes(pe.__data__);base=pe.OPTIONAL_HEADER.ImageBase
out={};pos=0
while (pos:=d.find(b'.?AVCFunc',pos))>=0:
 end=d.index(b"\x00",pos);name=d[pos:end].decode();td=pe.get_rva_from_offset(pos)-8+base;pos+=1
 q=0
 while (q:=d.find(struct.pack('<I',td),q))>=0:
  col=pe.get_rva_from_offset(q)-12+base;q+=1;v=0
  while (v:=d.find(struct.pack('<I',col),v))>=0:
   vt=pe.get_rva_from_offset(v)+4+base;v+=1
   target=struct.unpack('<I',pe.get_data(vt-base,4))[0]
   if base+pe.sections[0].VirtualAddress<=target<base+pe.sections[0].VirtualAddress+pe.sections[0].Misc_VirtualSize:out[name]={'vtable':hex(vt),'dispatcher':hex(target)}
print(json.dumps(out,indent=2))
