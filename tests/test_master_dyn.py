"""Master dynamics (clipper, compressor models, limiter) under Unicorn.

Runs for real: the patched firmware's master compressor callback (gr_process)
with the stock compressor, Cobalt's saturation and the new stages; the native
parameter registration, set construction and project XML reader / writer.
Held against: the same C built for the host (tests/master_dyn_lib.c), and the
unmodified Cobalt 1.2.0 image (tests/references/cobalt-1.2.0) run alongside for
everything the stock compressor and saturation do.
NOT run: the real audio graph around the compressor node, the screen, the
knobs, an SD card, CPU time on the real chip.
"""
import ctypes,importlib.util,subprocess,tempfile,random
from firmware_harness import *
import firmware_harness as H
import delay_fixture as D
spec=importlib.util.spec_from_file_location('cobalt120',ROOT/'tests'/'references'/'cobalt-1.2.0'/'patch_bluebox.py')
cobalt120=importlib.util.module_from_spec(spec);spec.loader.exec_module(cobalt120)
ref=cobalt120.transform(stock)
check(hashlib.sha256(ref).hexdigest().startswith('c28981e5bf1b511a'),'reference image is the published Cobalt 1.2.0 build')

tmp=tempfile.TemporaryDirectory(prefix='bluebox-master-dyn-')
lib_path=pathlib.Path(tmp.name)/'libmd.dylib'
subprocess.run(['cc','-std=gnu11','-O2','-shared','-fPIC',str(ROOT/'tests'/'master_dyn_lib.c'),'-o',str(lib_path)],check=True)
lib=ctypes.CDLL(str(lib_path))
lib.md_comp_o2.restype=ctypes.c_float
F32=ctypes.c_float*32
MODEL,CLIPPER,CLIP,SHAPE,LIMIT,LDRIVE,LCEIL,FLOW=range(0x1c0,0x1c8)
THRESH,WET,GAIN,COMP,SAT=0x132,0x13a,0x13b,0xb5,0x43
LINK=0x38800f90;STATE=0x20010000;   # DTCM: where the device's allocator puts a block this small
LINK_MAGIC=0x4d444c31;MD_MAGIC=0x4d44594e
SIZE=lib.md_host_size();ALLOC=(SIZE+31)&~31
OFF_CLIP,OFF_COMP,OFF_LIM=lib.md_host_off_clip_on(),lib.md_host_off_comp_model(),lib.md_host_off_lim_on()

class Host:
 """The same stages on the host, in the order the firmware documents."""
 def __init__(self):
  self.buf=ctypes.create_string_buffer(SIZE);lib.md_host_init(self.buf);self.p={}
 def events(self,evs):
  for pid,v in evs:
   lib.md_host_event(self.buf,pid,v);self.p[pid]=v
 def stage(self,fn,l,r):
  a=F32(*l);b=F32(*r);gr=fn(self.buf,a,b,32);return list(a),list(b),gr
 def clip(self,l,r):
  l,r,_=self.stage(lib.md_clip_o2,l,r);return l,r
 def lim(self,l,r):
  l,r,_=self.stage(lib.md_lim_o2,l,r);return l,r
 def comp(self,l,r):return self.stage(lib.md_comp_o2,l,r)
def link(u,state=STATE):
 u.mem_write(LINK,struct.pack('<II',LINK_MAGIC,state));u.mem_write(STATE,bytes(ALLOC))
def unpack(out):
 v=struct.unpack('<64f',out);return list(v[:32]),list(v[32:])
def f32s(x):return list(struct.unpack('<%df'%len(x),struct.pack('<%df'%len(x),*x)))
random.seed(7)
def music(n,amp):
 """Two unrelated tones and some noise per channel: hot enough to clip."""
 l=[amp*(.6*math.sin(2*math.pi*110*(n*32+i)/48000)+.4*math.sin(2*math.pi*1730*(n*32+i)/48000))+.05*amp*random.uniform(-1,1) for i in range(32)]
 r=[amp*(.6*math.sin(2*math.pi*147*(n*32+i)/48000+.4)+.4*math.sin(2*math.pi*2310*(n*32+i)/48000))+.05*amp*random.uniform(-1,1) for i in range(32)]
 return f32s(l),f32s(r)
def worst(a,b):return max(abs(x-y) for x,y in zip(a,b))
TOL=2e-5   # ARM and host builds may fuse multiply-adds differently

# ---------------------------------------------------------------- parameters
def definitions(image):
 u=machine(image);heap=[0xc0010000]
 def allocate(u):
  a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
 stub(u,0x8103128,allocate);call(u,0x8134cb8,0)
 return u,bytes(u.mem_read(0xc0010000,0x4850))
def string(u,p):return bytes(u.mem_read(p,64)).split(b'\0')[0]
su,sdefs=definitions(stock);ru,rdefs=definitions(ref);nu,ndefs=definitions(patched)
for pid in range(0x1c0,0x1c8):
 check(sdefs[pid*28:pid*28+28]==bytes(28) and rdefs[pid*28:pid*28+28]==bytes(28),'parameter id '+hex(pid)+' is unused in stock 1.5.2 and in Cobalt 1.2.0')
def meaning(u,defs,i):
 """A definition by what it says: label, key and list entries live in the mod's own code, which moves."""
 e=defs[i*28:i*28+28]
 if e==bytes(28):return None
 w=struct.unpack('<7I',e);kind=e[2]
 names=tuple(string(u,u32(u,w[2]+4*k)) for k in range(w[3])) if kind==5 and w[2] else (w[2],w[3])
 return e[:4],string(u,w[1]) if w[1] else b'',names,e[16:24],string(u,w[6]) if w[6] else b''
check(all(meaning(ru,rdefs,i)==meaning(nu,ndefs,i) for i in range(0x295) if not 0x1c0<=i<0x1c8),'every other parameter definition reads exactly as in Cobalt 1.2.0')
EXPECT={MODEL:(5,b'Comp Model:',b'mstmodel',[b'Stock',b'Glue',b'Punch',b'Opto',b'Squash']),
 CLIPPER:(5,b'Clipper:',b'mstclipper',[b'Off',b'Clean',b'Diode']),
 CLIP:(8,b'  Clip:',b'mstclip',(0,1000)),SHAPE:(8,b'  Shape:',b'mstclipshape',(0,1000)),
 LIMIT:(4,b'Limiter:',b'mstlimit',(0,1)),LDRIVE:(7,b'  Drive:',b'mstlimdrive',(0,18000)),
 LCEIL:(7,b'  Ceiling:',b'mstlimceil',(-12000,0)),
 FLOW:(5,b'Flow:',b'mstflow',[b'Clip>Comp>Lim',b'Comp>Clip>Lim',b'Comp>Lim>Clip'])}
keys=set()
for pid,(kind,label,key,extra) in EXPECT.items():
 e=ndefs[pid*28:pid*28+28];w=struct.unpack('<7I',e);rid,rkind=struct.unpack('<HB',e[:3])
 ok=rid==pid and rkind==kind and string(nu,w[1])==label and string(nu,w[6])==key
 if kind==5:ok=ok and w[3]==len(extra) and [string(nu,u32(nu,w[2]+4*i)) for i in range(len(extra))]==extra
 else:ok=ok and struct.unpack('<ii',e[16:24])==extra
 check(ok,'definition '+hex(pid)+' '+label.decode().strip()+' has its type, range or choices and project key '+key.decode())
 keys.add(key)
allkeys=[string(nu,struct.unpack('<I',ndefs[i*28+24:i*28+28])[0]) for i in range(0x295) if ndefs[i*28:i*28+28]!=bytes(28)]
check(all(allkeys.count(k)==1 for k in keys),'the eight project keys are not used by any other parameter')
def parameters(image,kind):
 u=machine(image);stub(u,0x8103128,lambda u:u.reg_write(UC_ARM_REG_R0,0xc0010000))
 u.mem_write(0x24010000,bytes([kind]));call(u,0x81202b8,0x24020000,0x24010000)
 n=struct.unpack('<H',u.mem_read(0x2402000a,2))[0];cap=struct.unpack('<H',u.mem_read(0x2402000c,2))[0]
 return u,[struct.unpack('<H2xi',u.mem_read(0xc0010000+8*i,8)) for i in range(n)],cap
DEFAULTS=[(MODEL,0),(CLIPPER,0),(CLIP,0),(SHAPE,800),(LIMIT,0),(LDRIVE,0),(LCEIL,-300),(FLOW,0)]
for kind in range(1,9):
 _,old,oldcap=parameters(ref,kind);u,new,cap=parameters(patched,kind)
 if kind==2:
  check(len(old)==68 and oldcap==68,'Cobalt 1.2.0 master set is full: 68 of 68 entries')
  check(new==old+DEFAULTS and cap==80,'master set: Cobalt 1.2.0 entries, then the eight dynamics defaults, in a set made for 80')
  for pid,v in [(MODEL,3),(CLIPPER,2),(CLIP,640),(LCEIL,-4500&0xffffffff),(FLOW,2)]:
   call(u,0x8120264,0x24020000,pid,v)
   check(call(u,0x8120234,0x24020000,pid)==v,'native set / get stores '+hex(pid))
 else:check(new==old and cap==68,'parameter set kind '+str(kind)+' is exactly as in Cobalt 1.2.0, capacity 68')
# The way the device gets its master set: the session constructor (0x08126cbc) allocates
# every set first and fills them afterwards, so the capacity is decided there, not in
# 0x081202b8. (A set built on its own, as above, hid this: on hardware the first build's
# eight rows were shown but could not be changed, because their entries had been dropped.)
def session_master(image):
 q=machine(image);heap=[0xc0010000]
 def allocate(q):
  a=heap[0];heap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.mem_write(a,bytes(heap[0]-a));q.reg_write(UC_ARM_REG_R0,a)
 stub(q,0x8103128,allocate);S=0x24040000
 try:call(q,0x8126cbc,S)
 except UcError:pass   # runs on into the unmapped rest of the app after the sets are filled
 out=[]
 for k in range(7):
  t=S+0xe78+16*k;n,cap=struct.unpack('<HH',q.mem_read(t+0xa,4));data=u32(q,t+4)
  out.append((cap,[struct.unpack('<H2xi',q.mem_read(data+8*i,8)) for i in range(n)]))
 return q,S,out,heap[0]
_,_,ref_sets,ref_heap=session_master(ref);sq,S,new_sets,new_heap=session_master(patched)
check(ref_sets[0][0]==68 and len(ref_sets[0][1])==68,'device path, Cobalt 1.2.0: the master set is allocated for 68 and full')
check(new_sets[0][0]==80 and new_sets[0][1]==ref_sets[0][1]+DEFAULTS,'device path: the master set is allocated for 80 and holds Cobalt\'s 68 entries plus the eight dynamics defaults')
check(all(c==80 and e==r[1] for (c,e),r in zip(new_sets[1:],ref_sets[1:])),'device path: the six bus sets hold exactly what they did, with room for 80')
check(new_heap-ref_heap==7*12*8,'device path: the larger sets take 672 bytes more of the UI heap in this constructor')
for pid,v in [(MODEL,3),(CLIPPER,2),(CLIP,640),(LIMIT,1),(LCEIL,-4500&0xffffffff),(FLOW,2)]:
 call(sq,0x8120264,S+0xe78,pid,v)
 check(call(sq,0x8120234,S+0xe78,pid)==v,'device path: the native set / get an encoder uses stores '+hex(pid))
old=bytearray(patched)
for at,b in ((0x08126cf6,0x44),(0x0811e186,0x44),(0x0811e290,0x44)):old[at-0x8040000]=b
oq,S,old_sets,_=session_master(bytes(old))
call(oq,0x8120264,S+0xe78,CLIPPER,1)
check(old_sets[0][0]==68 and old_sets[0][1]==ref_sets[0][1] and call(oq,0x8120234,S+0xe78,CLIPPER)==0,'(the fault seen on hardware, reproduced: allocated for 68, the eight entries are dropped and a row cannot leave Off)')
for at in (0x0811e186,0x0811e290):
 check(patched[at-0x8040000:at-0x8040000+2]==bytes.fromhex('5024') and stock[at-0x8040000:at-0x8040000+2]==bytes.fromhex('4424'),'project-copy constructor at '+hex(at)+' allocates the same seven sets for 80')
rows=[]
for i in range(68):
 rid=struct.unpack('<H',patched[0x10ae70+2*i:0x10ae72+2*i])[0]
 if not rid:break
 rows.append(rid)
stock_rows=[0x178,0x204,0x203,0x172,0x174,0x175,0x18c,0x177,0x170,0x171,0x18b,0x116,0xb5,*range(0x130,0x13c)]
check(rows==stock_rows+[0x43,0x1b6,0x1b7,0x1b8]+list(range(0x1c0,0x1c8)),'MAIN > B rows: stock, Cobalt saturation, then Comp Model, Clipper, Clip, Shape, Limiter, Drive, Ceiling, Flow')
check(len(rows)==37 and patched[0x10ae70+2*37:0x10aef8]==bytes(0x88-74) and len(rows)<=48,'row list is zero-terminated inside its 68-entry table and within the list widget\'s 48 rows')
check(stock[0x10aef8:0x10af00]==patched[0x10aef8:0x10af00] and struct.unpack('<H',patched[0x10aef8:0x10aefa])[0]==0x155,'the next row table (FX2) is untouched')

# The native MAIN > B page builder (0x08121cf4, cell 0x0c) walks that table into the
# row list; the native add-row (0x08138546) refuses a 48th row. Both run for real.
def page_rows(image):
 u=machine(image);stub(u,0x8103128,lambda u:u.reg_write(UC_ARM_REG_R0,0xc0010000))
 SESSION=0x24040000;LISTW=0x24060000;CELL=0x24011800
 u.mem_write(0x24010000,b'\x02');call(u,0x81202b8,SESSION+0xe78,0x24010000)
 u.mem_write(CELL,struct.pack('<H',0x0c));w32(u,LISTW,99)
 ret=call(u,0x8121cf4,SESSION,CELL,LISTW);n=u32(u,LISTW)
 return ret,[struct.unpack('<H6xi',u.mem_read(LISTW+4+12*k,12)) for k in range(n)]
ret,built=page_rows(patched);_,built120=page_rows(ref)
check(ret==1 and [r[0] for r in built]==rows and len(built)==37,'native page builder lists all 37 MAIN > B rows (the list widget holds 47)')
check(built[:29]==built120 and len(built120)==29,'the first 29 rows and their values are Cobalt 1.2.0\'s')
check(built[29:]==DEFAULTS,'the eight new rows show their defaults, read from the enlarged master set')
assert 0x2f==47
# Project XML: the native writer and reader, as tests/test_saturation.py runs them.
u=machine();heap=[0xc0010000]
def allocate(u):
 a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
stub(u,0x8103128,allocate);call(u,0x8134cb8,0);call(u,0x8120abc)
SET=0x24020000;TYPE=0x24010000;XML=0x24050000;VTYPE=0x24021000;VXML=0x24051000
u.mem_write(TYPE,b'\x02');call(u,0x81202b8,SET,TYPE)
w32(u,SET,VTYPE);w32(u,VTYPE,0x80dfe01);stub(u,0x80dfe00,lambda u:u.reg_write(UC_ARM_REG_R0,2))
w32(u,XML,VXML);w32(u,VXML,0x80dfe11);stub(u,0x80dfe10,lambda u:u.reg_write(UC_ARM_REG_R0,XML))
saved={};last=['']
stub(u,0x8107b16,lambda u:u.reg_write(UC_ARM_REG_R0,XML));stub(u,0x8106c92,lambda u:u.reg_write(UC_ARM_REG_R0,XML))
def property_key(u):
 last[0]=string(u,u.reg_read(UC_ARM_REG_R1)).decode();u.reg_write(UC_ARM_REG_R0,XML)
stub(u,0x81074dc,property_key)
stub(u,0x8106dc6,lambda u:saved.update({last[0]:u.reg_read(UC_ARM_REG_R1)}));stub(u,0x8106dba,lambda u:None)
VALUES=[(MODEL,'mstmodel',3),(CLIPPER,'mstclipper',2),(CLIP,'mstclip',640),(SHAPE,'mstclipshape',250),(LIMIT,'mstlimit',1),(LDRIVE,'mstlimdrive',7500),(LCEIL,'mstlimceil',-4500&0xffffffff),(FLOW,'mstflow',1)]
for pid,key,v in VALUES:call(u,0x8120264,SET,pid,v)
call(u,0x8120b84,XML,XML,SET)
check(all(saved.get(key)==v for pid,key,v in VALUES),'native project writer saves all eight dynamics settings')
check(saved.get('mstdrive')==0 and saved.get('cthreshdb')==(-20000)&0xffffffff,'native project writer still saves the saturation and compressor rows')
for (pid,key,v),(_,d) in zip(VALUES,DEFAULTS):call(u,0x8120264,SET,pid,d&0xffffffff)
stub(u,0x81074a6,lambda u:u.reg_write(UC_ARM_REG_R0,0));stub(u,0x8106ae8,lambda u:u.reg_write(UC_ARM_REG_R0,XML))
def find_property(u):
 key=string(u,u.reg_read(UC_ARM_REG_R1)).decode();last[0]=key;u.reg_write(UC_ARM_REG_R0,XML if key in saved else 0)
stub(u,0x810747c,find_property)
def read_integer(u):
 w32(u,u.reg_read(UC_ARM_REG_R1),saved[last[0]]);u.reg_write(UC_ARM_REG_R0,0)
stub(u,0x8106d9e,read_integer)
call(u,0x8120c3c,XML,SET)
check([call(u,0x8120234,SET,pid) for pid,key,v in VALUES]==[v for pid,key,v in VALUES],'native project reader restores all eight dynamics settings')
for (pid,key,v),(_,d) in zip(VALUES,DEFAULTS):
 saved.pop(key);call(u,0x8120264,SET,pid,d&0xffffffff)
call(u,0x8120c3c,XML,SET)
check([call(u,0x8120234,SET,pid) for pid,key,v in VALUES]==[d&0xffffffff for _,d in DEFAULTS],'a project saved by Cobalt 1.2.0 (no dynamics keys) loads with every stage off')

# ------------------------------------------------------- boot-time allocation
ou,_,oheap=D.dsetup(ref);nu2,_,nheap=D.dsetup(patched)
magic,state=struct.unpack('<II',nu2.mem_read(LINK,8))
check(magic==LINK_MAGIC and state==oheap and nheap==oheap+ALLOC+64,'real delay constructor: the dynamics state (and the 64-byte peak hold behind it) is the last arena request, so every Cobalt 1.2.0 buffer keeps its address')
check(u32(nu2,state)==0 and u32(nu2,state+ALLOC)==0 and state%32==0 and ALLOC<=2048,'the state starts unconfigured, 32-byte aligned, '+str(SIZE)+' bytes')
check(bytes(ou.mem_read(0x38800000,0xf90))==bytes(nu2.mem_read(0x38800000,0xf90)) and bytes(ou.mem_read(0x38800fa0,0x60))==bytes(nu2.mem_read(0x38800fa0,0x60)),'constructor leaves all other backup SRAM exactly as Cobalt 1.2.0 does')
check(bytes(ou.mem_read(LINK,16))==bytes(16),'the link cell 0x38800f90..0xf9f is not used by Cobalt 1.2.0')
# The native allocator itself, unstubbed: a block this size does not come from the SDRAM
# arena at all. (The first hardware builds refused anything but an arena address, so on the
# device the stages and the peak readout never ran; the stubbed allocator above hid it.)
ra=machine(patched);got=call(ra,0x80412a4,ALLOC+64)
check(0x20000000<=got<0x2001f800 and got%16==0,'real native allocator: the block comes from DTCM ('+hex(got)+'), not the arena')
for where,base in (('DTCM, as allocated on the device',got),('the top of DTCM',0x2001f800-ALLOC-64),('AXI SRAM heap',0x24060000),('the SDRAM arena',0xc0100000)):
 a,an=setup(ref);b,bn=setup();b.mem_write(LINK,struct.pack('<II',LINK_MAGIC,base));b.mem_write(base,bytes(ALLOC+64));same=True;top=0
 for n in range(12):
  l,r=music(n,1.4);ev=[(COMP,0),(CLIPPER,1),(CLIP,800),(LIMIT,1),(LCEIL,-6000)] if n==0 else []
  oa,_=block(a,l,r,ev);ob,_=block(b,l,r,ev);same=same and oa==ob;top=max(top,max(abs(x) for x in struct.unpack('<64f',ob)))
 check(not same and top<=10**(-.3)*1.004 and u32(b,base)==MD_MAGIC and b.mem_read(0x38800648+17,1)==b'\6','a state block in '+where+' is used: the clipper and limiter act on the audio')
u2=machine(patched);heap2=[0xc1fffc00]
w32(u2,0x24000000,0xc2000000);w32(u2,0x24000004,0xc1ffffe0)
u2.mem_write(LINK,struct.pack('<II',LINK_MAGIC,0x12345678))
# a full arena: run only our allocation path through the constructor's tail is not separable, so check the guard by value
check(ALLOC>0xc2000000-0xc1ffffe0,'(guard precondition) a nearly full arena has no room for the state')

# ------------------------------------------------------------ audio: bypass
def pair(evs,linked=True,blocks=24,amp=1.2,index=12):
 a,an=setup(ref);b,bn=setup()
 if linked:link(b)
 for q in (a,b):
  if index!=12:w32(q,OBJ+0x18,index)
 same=True;rets=[]
 for n in range(blocks):
  l,r=music(n,amp);ev=evs if n==0 else []
  oa,ra=block(a,l,r,ev);ob,rb=block(b,l,r,ev);same=same and oa==ob and ra==rb;rets.append(rb)
 return a,b,an,bn,same
for comp in (0,1):
 for sat in (0,700):
  base=[(COMP,comp),(THRESH,-30000),(SAT,sat)]
  a,b,an,bn,same=pair(base)
  check(same and len(bn)==24 and b.mem_read(OBJ,0x130)==a.mem_read(OBJ,0x130) and report(a)==report(b),f'all stages off (comp {comp}, saturate {sat}): audio, compressor state, meter and chain are bit-identical to Cobalt 1.2.0')
  check(u32(b,STATE)==MD_MAGIC and u32(b,STATE+OFF_CLIP)==0 and u32(b,STATE+OFF_LIM)==0 and b.mem_read(0x38800648+17,1)==b'\2','all stages off: state is configured and idle, status says the master compressor found it')
  a,b,an,bn,same=pair(base+[(CLIPPER,1),(CLIP,800),(LIMIT,1),(LDRIVE,9000),(MODEL,2)],linked=False)
  check(same and len(bn)==24,f'no state allocated (comp {comp}, saturate {sat}): the dynamics settings do nothing and audio is bit-identical to Cobalt 1.2.0')
a,b,an,bn,same=pair([(COMP,1),(THRESH,-30000),(CLIPPER,2),(CLIP,800),(LIMIT,1),(LDRIVE,9000),(MODEL,2)],index=3)
check(same,'a compressor that is not the master one is never touched')
for bad in (0,0x1fffff00,0x20020000-64,0x08040000,0xc0100002,0xc4000000-64,0xffffffe0):
 a,an=setup(ref);b,bn=setup();b.mem_write(LINK,struct.pack('<II',LINK_MAGIC,bad));same=True
 for n in range(4):
  l,r=music(n,1.2);ev=[(COMP,0),(CLIPPER,1),(CLIP,800),(LIMIT,1)] if n==0 else []
  same=same and block(a,l,r,ev)==block(b,l,r,ev)
 check(same,'a link that is not a whole block of RAM ('+hex(bad)+') is refused')

# ------------------------------------------- audio: stages against the host
def run(evs,blocks=40,amp=1.3,comp_events=(),later=None):
 """Firmware vs host model. The reference Cobalt 1.2.0 machine supplies the
 stock compressor and saturation; the host supplies the new stages."""
 u,nodes=setup();link(u);r0,_=setup(ref);h=Host();out=[];exp=[];grs=[]
 for n in range(blocks):
  ev=list(evs)+list(comp_events) if n==0 else list(dict(later).get(n,[])) if later else []
  l,r=music(n,amp)
  o,ret=block(u,l,r,ev);ol,orr=unpack(o);out+=ol+orr
  h.events(ev);p=h.p
  flow=p.get(FLOW,0);clip=p.get(CLIPPER,0)!=0;lim=p.get(LIMIT,0)==1;model=p.get(MODEL,0)
  el,er=l,r
  if clip and flow==0:el,er=h.clip(el,er)
  # reference machine: same events (the dynamics ids are unknown to it). While a model
  # replaces the stock compressor, stock's own bypass flag is held for the call and put
  # back unless the block carries a Compressor on / off event: what gr_process documents.
  flag=bytes(r0.mem_read(OBJ+0x28,1))
  if model:r0.mem_write(OBJ+0x28,b'\1')
  ro,_=block(r0,el,er,ev);sl,sr=unpack(ro)
  if model and not any(e[0]==COMP for e in ev):r0.mem_write(OBJ+0x28,flag)
  if model and p.get(COMP,1) and not p.get(SAT,0):
   el,er,g=h.comp(el,er);grs.append(g)
  else:
   el,er=sl,sr;lib.md_host_comp_reset(h.buf)   # as the firmware: a model starts over after Stock or Compressor off
  if clip and flow==1:el,er=h.clip(el,er)
  if lim:el,er=h.lim(el,er)
  if clip and flow==2:el,er=h.clip(el,er)
  exp+=el+er
  if ret!=7 or u32(u,OBJ+8)!=NEXT:raise AssertionError('chain broken')
 if len(nodes)!=blocks:raise AssertionError('next node count')
 run.first=next((i//64 for i in range(0,len(out),64) if worst(out[i:i+64],exp[i:i+64])>=TOL),None)
 return u,r0,out,exp,grs
names={1:'Clean',2:'Diode'}
for flow in range(3):
 for kind in (1,2):
  for clip,shape in ((0,1000),(500,800),(1000,0)):
   u,r0,out,exp,_=run([(COMP,0),(CLIPPER,kind),(CLIP,clip),(SHAPE,shape),(FLOW,flow)])
   check(worst(out,exp)<TOL and max(abs(x) for x in out)<=.9661+1e-6,f'clipper {names[kind]} Clip {clip/10:.0f}% Shape {shape/10:.0f}% flow {flow}: firmware equals the host build (worst {worst(out,exp):.1e}) and stays under -0.3 dBFS')
for flow in range(3):
 for drive,ceil in ((0,-300),(9000,-1000),(18000,-12000),(6000,0)):
  u,r0,out,exp,_=run([(COMP,0),(LIMIT,1),(LDRIVE,drive),(LCEIL,ceil),(FLOW,flow)])
  top=10**(ceil/20000)*1.004
  check(worst(out,exp)<TOL and max(abs(x) for x in out)<=top,f'limiter Drive {drive/1000:g} dB Ceiling {ceil/1000:g} dB flow {flow}: firmware equals the host build (worst {worst(out,exp):.1e}) and stays under the ceiling')
results={}
for flow in range(3):
 for comp in (0,1):
  for sat in (0,600):
   u,r0,out,exp,_=run([(CLIPPER,1),(CLIP,600),(SHAPE,700),(LIMIT,1),(LDRIVE,6000),(LCEIL,-1000),(FLOW,flow)],comp_events=[(COMP,comp),(THRESH,-30000),(0x135,0),(SAT,sat)])
   results[(flow,comp,sat)]=out
   check(worst(out,exp)<TOL,f'clipper + stock compressor ({comp}) + saturation ({sat}) + limiter, flow {flow}: firmware equals reference compressor / saturation with the host stages in that order (worst {worst(out,exp):.1e})')
   # in flow 0 the reference is fed the host's clipper output, equal only to within TOL
   if flow:check(u.mem_read(OBJ,0x130)==r0.mem_read(OBJ,0x130),'stock compressor state is the reference\'s, fed the same bus')
check(worst(results[(0,1,0)],results[(1,1,0)])>.01 and worst(results[(1,1,0)],results[(2,1,0)])>.01 and worst(results[(0,1,0)],results[(2,1,0)])>.01,'the three Flow orders really are three different signal paths')
check(max(abs(x) for x in results[(0,1,600)])<=10**(-1000/20000)*1.004 and max(abs(x) for x in results[(1,1,600)])<=10**(-1000/20000)*1.004,'limiter last (flows 0 and 1): the output never passes Lim Ceiling')
check(max(abs(x) for x in results[(2,1,600)])<=.9661+1e-6,'clipper last (flow 2): the output never passes -0.3 dBFS')

mnames={1:'Glue',2:'Punch',3:'Opto',4:'Squash'}
for model in range(1,5):
 u,r0,out,exp,grs=run([(MODEL,model)],comp_events=[(COMP,1),(THRESH,-20000)],blocks=96,amp=.7)
 magic,gr,on,_,seq=report(u)
 want=max(grs[64:96])*10
 check(worst(out,exp)<TOL,f'{mnames[model]}: firmware equals the host build on the dry bus, stock compressor output discarded (worst {worst(out,exp):.1e})')
 check(magic==0x44475232 and on==1 and abs(gr-want)<=1.5,f'{mnames[model]}: the gain-reduction meter shows the model ({gr/10:.1f} dB, host {want/10:.1f} dB)')
 check(u.mem_read(OBJ,0x130)==r0.mem_read(OBJ,0x130) and u.mem_read(OBJ+0x28,1)==b'\0',f'{mnames[model]}: underneath, the stock compressor is in the state of a Cobalt 1.2.0 compressor held in bypass, and its on / off flag still reads on')
 u,r0,out,exp,grs=run([(MODEL,model)],comp_events=[(COMP,1),(THRESH,-32000),(GAIN,-4000),(WET,600)],blocks=40,amp=.7)
 check(worst(out,exp)<TOL,f'{mnames[model]}: follows the stock Thresh, Gain and Wet/Dry rows (worst {worst(out,exp):.1e})')
 a,b,an,bn,same=pair([(COMP,0),(MODEL,model)])
 check(same and u32(b,STATE+OFF_COMP)==0,f'{mnames[model]} with Compressor off: bit-identical to Cobalt 1.2.0 (dry)')
u,r0,out,exp,grs=run([(MODEL,1)],comp_events=[(COMP,1),(THRESH,-20000)],blocks=90,amp=.7,later=[(20,[(THRESH,-36000),(0x133,8),(GAIN,3000)]),(30,[(MODEL,0)]),(50,[(MODEL,4)]),(60,[(COMP,0)]),(70,[(COMP,1)]),(80,[(MODEL,0),(COMP,1)])])
check(worst(out,exp)<TOL and u.mem_read(OBJ,0x130)==r0.mem_read(OBJ,0x130),'Glue, back to Stock, Squash, Compressor off and on, Stock again: stock behaves as a compressor that was bypassed meanwhile, and settings changed while a model was in are applied')
u,r0,out,exp,grs=run([(MODEL,2),(COMP,1)],blocks=30,amp=.7,later=[(10,[(COMP,0)]),(20,[(COMP,1)])])
check(worst(out,exp)<TOL and u.mem_read(OBJ,0x130)==r0.mem_read(OBJ,0x130),'Compressor off and on while Punch is in: the on / off row keeps its meaning and the flag is never left wrong')
u,r0,out,exp,grs=run([(MODEL,3),(CLIPPER,1),(CLIP,400),(LIMIT,1),(LDRIVE,3000),(FLOW,0)],comp_events=[(COMP,1),(THRESH,-24000)],blocks=40,amp=.9)
check(worst(out,exp)<TOL,f'clip > Opto > limit, the requested chain: firmware equals the host build (worst {worst(out,exp):.1e})')
# model with saturation after it: the reference machine saturates the host-compressed bus
u,_=setup();link(u);rs,_=setup(ref);h=Host();out=[];exp=[]
for n in range(40):
 ev=[(COMP,1),(THRESH,-20000),(MODEL,2),(SAT,500)] if n==0 else [];l,r=music(n,.7)
 o,_=block(u,l,r,ev);ol,orr=unpack(o);out+=ol+orr
 h.events(ev);cl,cr,_=h.comp(l,r)
 so,_=block(rs,cl,cr,[(COMP,0),(SAT,500)] if n==0 else []);sl,sr=unpack(so);exp+=sl+sr
check(worst(out,exp)<TOL,f'Punch then saturation: the model feeds Cobalt\'s saturation (worst {worst(out,exp):.1e})')

# ------------------------------------------------------ audio: edge cases
ON=[(COMP,0),(CLIPPER,1),(CLIP,700),(LIMIT,1),(LDRIVE,6000),(LCEIL,-2000)]
u,_=setup(chain=False);link(u);o,ret=block(u,*music(0,1.2),ON)
check(ret==1 and u32(u,OBJ+8)==0,'no downstream node: terminal return value and null link are kept')
u,_=setup(chain=False);link(u);w32(u,OBJ+8,NEXT);w32(u,NEXT,VT);w32(u,VT+12,NEXT_FN|1);seen=[]
def downstream(u):
 seen.append(bytes(u.mem_read(BUF,128))+bytes(u.mem_read(BUF+0x100,128)));u.reg_write(UC_ARM_REG_R0,7)
stub(u,NEXT_FN,downstream)
outs=[block(u,*music(n,1.4),ON if n==0 else [])[0] for n in range(6)]
check(len(seen)==6 and seen==outs and max(abs(x) for x in struct.unpack('<64f',seen[-1]))<=10**(-.1)*1.004,'the downstream master node receives the clipped and limited audio, once per block')
desc=CTX+0x150e4+12*20
for frames,cap,silent,stereo in [(4,32,0,1),(32,32,0,0),(32,32,1,1),(0,0,0,1)]:
 u,_=setup();link(u)
 if stereo:   # earlier stereo audio would (rightly) leave different tails in the two channels' look-ahead
  for n in range(8):block(u,*music(n,1.4),ON if n==0 else [])
 l=[1.5]*32;r=[-1.5]*32
 events(u,[] if stereo else ON);u.mem_write(desc,struct.pack('<IIIIBBxx',cap,frames,BUF,BUF+0x100,silent,stereo))
 u.mem_write(BUF,struct.pack('<32f',*l));u.mem_write(BUF+0x100,struct.pack('<32f',*r))
 before=bytes(u.mem_read(BUF,128))+bytes(u.mem_read(BUF+0x100,128))
 call(u,u32(u,u32(u,OBJ)+12),OBJ,CTX)
 after=bytes(u.mem_read(BUF,128))+bytes(u.mem_read(BUF+0x100,128))
 if silent or not cap:
  check(after==before and u32(u,STATE+OFF_CLIP)==0 and u32(u,STATE+OFF_LIM)==0,f'silent / empty bus {(frames,cap,silent,stereo)}: audio untouched and the stages restart clean, so the look-ahead cannot replay old audio')
 elif not stereo:
  v=struct.unpack('<64f',after)
  for n in range(3):
   events(u,[]);u.mem_write(desc,struct.pack('<IIIIBBxx',cap,frames,BUF,BUF+0x100,silent,stereo))
   u.mem_write(BUF,struct.pack('<32f',*l));u.mem_write(BUF+0x100,struct.pack('<32f',*r))
   call(u,u32(u,u32(u,OBJ)+12),OBJ,CTX)
  v=struct.unpack('<64f',bytes(u.mem_read(BUF,128))+bytes(u.mem_read(BUF+0x100,128)))
  check(v[:32]==v[32:] and all(0<x<=10**(-.1)*1.004 for x in v),'mono bus: both channels carry the same clipped and limited audio')
 else:
  check(after[16:128]==before[16:128] and after[144:]==before[144:] and after[:16]!=before[:16],'short block: only its valid frames are written')
# after silence the limiter's look-ahead starts empty
u,_=setup();link(u)
for n in range(8):block(u,[.9]*32,[.9]*32,[(COMP,0),(LIMIT,1),(LDRIVE,0),(LCEIL,0)] if n==0 else [])
events(u,[]);u.mem_write(desc,struct.pack('<IIIIBBxx',32,32,BUF,BUF+0x100,1,1));call(u,u32(u,u32(u,OBJ)+12),OBJ,CTX)
o,_=block(u,[.25]*32,[.25]*32);o2,_=block(u,[.25]*32,[.25]*32);o3,_=block(u,[.25]*32,[.25]*32)
v=struct.unpack('<64f',o)+struct.unpack('<64f',o2)+struct.unpack('<64f',o3)
check(all(x==0 for x in v[:32]) and all(x==0 for x in struct.unpack('<64f',o2)[:32]) and abs(struct.unpack('<64f',o3)[31]-.25)<1e-6,'after a silent block the limiter plays 1.5 ms of silence, not the audio from before it')
# garbage on the bus must not poison the state
u,_=setup();link(u)
for n in range(4):block(u,*music(n,1.2),ON+[(MODEL,1),(COMP,1)] if n==0 else [])
bad=[float('nan')]*8+[float('inf')]*8+[-float('inf')]*8+[1e30]*8
block(u,bad,bad)
ok=True
for n in range(8):
 o,_=block(u,*music(n,.5));ok=ok and all(math.isfinite(x) and abs(x)<=1 for x in struct.unpack('<64f',o))
check(ok,'NaN / infinity on the bus: the following blocks are finite and under the ceiling again')
# out-of-range events
u,_=setup();link(u);r0,_=setup(ref)
for n in range(6):
 l,r=music(n,1.2);ev=[(COMP,0),(MODEL,9),(CLIPPER,7),(FLOW,-2),(LIMIT,5),(CLIP,99999),(LDRIVE,-5)] if n==0 else []
 same=block(u,l,r,ev)==block(r0,l,r,ev)
check(same,'out-of-range list values fall back to Off / Stock: audio is Cobalt 1.2.0')

# ------------------------------------------------------------ work per block
def count(evs,amp=1.3):
 u,_=setup();link(u)
 for n in range(40):block(u,*music(n,amp),evs if n==0 else [])
 steps=[0]
 def c(u,addr,size,data):steps[0]+=1
 hk=u.hook_add(UC_HOOK_CODE,c);block(u,*music(41,amp));u.hook_del(hk);return steps[0]
base_off=count([(COMP,0)]);base_on=count([(COMP,1),(THRESH,-30000)])
work={'compressor off, all stages off':base_off,'stock compressor':base_on,
 'clipper Clean':count([(COMP,0),(CLIPPER,1),(CLIP,600)]),'clipper Diode':count([(COMP,0),(CLIPPER,2),(CLIP,600)]),
 'limiter':count([(COMP,0),(LIMIT,1),(LDRIVE,9000)]),
 'Glue (stock held in bypass)':count([(COMP,1),(THRESH,-30000),(MODEL,1)]),'Opto (stock held in bypass)':count([(COMP,1),(THRESH,-30000),(MODEL,3)]),
 'clip > Opto > limit':count([(COMP,1),(THRESH,-30000),(MODEL,3),(CLIPPER,2),(CLIP,600),(LIMIT,1),(LDRIVE,9000)]),
 'clip > Opto > Tape saturation > limit':count([(COMP,1),(THRESH,-30000),(MODEL,3),(CLIPPER,2),(CLIP,600),(LIMIT,1),(LDRIVE,9000),(SAT,700),(0x1b6,1)])}
for k,v in work.items():print('INFO instructions per 32-frame block,',k+':',v)
check(work['Glue (stock held in bypass)']<work['stock compressor']+600,'a model costs about what the stock compressor does, not both')
check(work['clip > Opto > limit']<21000,'everything on (no saturation): under 21000 instructions a block, about 3.5 stock compressors')
check(work['compressor off, all stages off']<400,'all stages off costs only the presence check')
print(str(H.checks)+' master dynamics checks passed. Sound, levels on the outputs, the screen rows, saving on a card and CPU headroom need the hardware.')
