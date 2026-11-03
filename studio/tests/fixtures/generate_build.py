"""Frozen, authored reference-writer fixtures. Python is not used by Studio."""
import copy
import json
import math
import struct
import sys
import tempfile
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/questions"))
import buzzpack
import buzzq
import clu
import sdi
import at3enc
from kip_export import wem_to_at9
import numpy as np

folder = Path(__file__).parent
categories = [
    (("AddQuestionCategoryItem",), [235, "Science", "CAT_SCIENCE", 0, 0, 1, 100, 1]),
    (("AddQuestionCategoryItem",), [237, "General", "CAT_GENERAL", 235, 0, 1, 100, 1]),
    (("AddQuestionCategoryItem",), [436, "Last", "CAT_LAST", 235, 0, 0, 100, 1]),
    (("AddQuestionCategoryItem",), [199, "Movies", "CAT_MOVIES", 0, 0, 1, 100, 2]),
    (("AddQuestionCategoryItem",), [208, "Cinema", "CAT_CINEMA", 199, 0, 1, 100, 2]),
]
info = clu.call_script([(("AddPackInfo",), [100, "PACK0100", "PackTitlePk100Pack",
    "PackDescPk100Pack", "p100PackGame1p", "p100PackGame4p", "p100PackGame8p"])])
header = bytearray(2048)
for index, value in {0:1, 4:640, 5:368, 6:640*368, 7:25000, 9:0x50, 12:10240, 13:1}.items():
    struct.pack_into('>I',header,index*4,value)
header[64:72] = b'SDIVIDEO'
struct.pack_into('>I',header,0x50,8192)
struct.pack_into('>I',header,0x4c,sdi.header_checksum(header))
padding_pack = b'\0\0\1\xba'+sdi._scr_bytes(0)+sdi._mux_bytes(sdi.MUX_RATE)+b'\0\0\1\xbe'+struct.pack('>H',2030)+b'\xff'*2030
sheet = bytes(header)+padding_pack*4
(folder/'build-sheet-template.sdi').write_bytes(sheet)
template = folder/'build-template.pak'
with zipfile.ZipFile(template,'w',zipfile.ZIP_STORED) as archive:
    for lang in ('ENG','POL'):
        archive.writestr(f'{lang}/SCRIPTS/GENERATED/questioncategories.clu',clu.dump(clu.call_script(categories)))
        archive.writestr(f'{lang}/SCRIPTS/GENERATED/addpackinfo.clu',clu.dump(info))
        texts=['Science','General','Movies','Cinema']
        hdr,hst=buzzq.encode_strings(texts)
        archive.writestr(f'{lang}/NAMEDTEXT/default.hdr',hdr)
        archive.writestr(f'{lang}/NAMEDTEXT/default.hst',hst)
        names=['CAT_SCIENCE','CAT_GENERAL','CAT_MOVIES','CAT_CINEMA']
        ndx='\ufeff'+''.join(f'{buzzpack.ndx_hash(k)} {i+1}\r\n' for i,k in enumerate(names))
        archive.writestr(f'{lang}/NAMEDTEXT/default.ndx',ndx.encode('utf-8'))
        archive.writestr(f'{lang}/QUESTIONASSETS/VIDEO/pics002.sdi',sheet)
    archive.writestr('ENG/DATA/owned.dat',b'Authored test fixture, not retail data.')
source={'pack':{'title':'Owned fixture','description':'Test fixture','language':'en','topic':'Science','category_base':700},'questions':[
    {'id':'letters','subject':235,'category':237,'question':'Which letter comes first?',
     'answers':['B','C','D','A'],'correct_index':3,'difficulty':2},
    {'id':'movies','subject':199,'category':208,'question':'What can a cinema show?',
     'answers':['A tree','A film','A river','A cloud'],'correct_index':1,'difficulty':1}]}
(folder/'build-source-en.json').write_text(json.dumps(source,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
reference=copy.deepcopy(source)
for q in reference['questions']:
    i=q.pop('correct_index');a=q['answers'];q['answers']=[a[i]]+a[:i]+a[i+1:]
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp)/'source.json';p.write_text(json.dumps(reference),encoding='utf-8')
    built,_=buzzpack.build(p,template,tmp,103)
    (folder/'build-reference.pak').write_bytes(built.read_bytes())
# Non-silent signed-16 input with a deterministic mixture of tones/noise.
state=7;samples=[]
for i in range(4096):
    state=(1664525*state+1013904223)&0xffffffff
    v=.3*math.sin(2*math.pi*440*i/48000)+.1*math.sin(2*math.pi*1300*i/48000)+(state/2**32-.5)*.05
    samples.append(round(v*32767))
pcm=struct.pack('<4096h',*samples)
(folder/'atrac-input.pcm').write_bytes(pcm)
(folder/'atrac-reference.rsf').write_bytes(at3enc.to_rsf(at3enc.encode(np.frombuffer(pcm,dtype='<i2').astype(float)/32768)))
fmt=bytearray(32)
struct.pack_into('<HHIIHHH',fmt,0,0xfffc,1,48000,8000,128,0,14)
struct.pack_into('<HI',fmt,18,1024,4)
fmt[24:28]=bytes.fromhex('fe7f0000')
struct.pack_into('<I',fmt,28,4096)
body=b'WAVEfmt '+struct.pack('<I',len(fmt))+fmt+b'data'+struct.pack('<I',64)+bytes(range(64))
wem=b'RIFF'+struct.pack('<I',len(body))+body
(folder/'wem-input.riff').write_bytes(wem)
(folder/'wem-at9-reference.riff').write_bytes(wem_to_at9(wem))
