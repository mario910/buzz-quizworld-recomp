"""Pinned inventory of shared93F0 and its six builtin input families."""
import argparse,hashlib,importlib.util,json,re,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];PIN='5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812'
ENTRY_PIN='40386ad034b6fc29264cc5c985d75a415dd2e480c36a7f99b9a474f94a390962';PREFIX='buzz_job_6eba_spu_func_'
ROOTS=(0x9700,0xa8f8,0xa6a8,0xa390,0x9e70,0x97f8)
FAMILIES=({0x9700,0x97a8},{0xa8f8,0xaaf8,0xab00},{0xa6a8,0xa750,0xa860,0xa864,0xa868},{0xa390,0xa5d0,0xa5d8},{0x9e70,0xa170},{0x97f8,0x9bb0})
spec=importlib.util.spec_from_file_location('input_ring',ROOT/'tools/inspect/geometry-ring-audit.py');ring=importlib.util.module_from_spec(spec);spec.loader.exec_module(ring)
def source_graph(raw):
 if hashlib.sha256(raw).hexdigest()!=PIN:raise ValueError('Unreviewed input lift')
 fs={int(pc,16):body for pc,body in re.findall(r'(?ms)^void '+PREFIX+r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}',raw.decode())}
 todo=[0x93f0];edges={}
 while todo:
  pc=todo.pop()
  if pc in edges or pc==0x95dc:continue
  body=ring.reachable_text(fs[pc])
  if re.search(r'\bspu_(wrch|rdch|rchcnt|sync|dsync|stop)\(',body):raise ValueError('New input I/O frontier')
  targets={int(t,16) for t in re.findall(PREFIX+r'([0-9A-F]+)',body)}
  if pc==0x9580:targets.update(ROOTS)
  edges[pc]=targets;todo.extend(targets)
 expected={0x93f0,0x9408,0x9498,0x9518,0x9580,0x2760}|set.union(*FAMILIES)
 if set(edges)!=expected or len(edges)!=23:raise ValueError('Input standard graph changed')
 for family in FAMILIES:
  if set.union(*(edges[p] for p in family))-family not in ({0x9498},{0x9498,0x2760}):raise ValueError('New builtin input frontier')
 return dict(standard_blocks=len(edges),data_blocks=sum(map(len,FAMILIES)),prefix_and_return_blocks=5,slot_publication_helper='2760',frame_bytes=448,frontiers=['95DC'],edges={f'{p:04X}':[f'{t:04X}' for t in sorted(ts)] for p,ts in sorted(edges.items())},formats=[dict(format=i,root=f'{ROOTS[i]:04X}',blocks=[f'{p:04X}' for p in sorted(f)],normal_block_count=len(f)) for i,f in enumerate(FAMILIES)])
def validate_table(entry):
 if len(entry)!=2048+0x40000:raise ValueError('Wrong GPR/LS snapshot size')
 offsets=struct.unpack_from('>6I',entry,2048+0x95c4)
 if tuple(0x95c4+x for x in offsets)!=ROOTS:raise ValueError('Input dispatch table changed')
 return [f'{x:04X}' for x in ROOTS]
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,default=ROOT/'analysis/native-geometry/phase3/input-audit-v1.json');args=parser.parse_args()
 entry=(ROOT/'analysis/native-geometry/phase3/conversion-audit-v1/pc_54E8.bin').read_bytes();assert hashlib.sha256(entry).hexdigest()==ENTRY_PIN
 graph=source_graph((ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes());graph.update(source_sha256=PIN,table_95C4=validate_table(entry),entry_sha256=ENTRY_PIN,entry_provenance='Real54E8 GPR/LS snapshot used only for image41 LS constants,not a captured builtin input root',native_gates='Input1 standalone tests separate;remaining2..5/shared93F0 production and real/live/final gates OPEN')
 encoded=(json.dumps(graph,indent=2)+'\n').encode();assert not args.output.exists() or args.output.read_bytes()==encoded,'Never overwrite differing audit evidence'
 if not args.output.exists():args.output.write_bytes(encoded)
 print('PASS input inventory:23 original standard blocks/17 data blocks/6 builtin families/448-byte frame/2760 publication;95DC custom frontier;native production/cold/final gates OPEN')
if __name__=='__main__':main()
