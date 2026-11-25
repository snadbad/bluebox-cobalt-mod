"""Peak readout on the track headers.

M7: the real patched call sites that publish meter levels (three in the mixer,
one in the output node for the master) run in the emulator; the native publish
functions are stubbed and recorded. M4: the real MIXER and TRACK pages are
built by their native constructors (gate_ui_fixture) and the overlay draws into
a framebuffer. NOT run: the native meter drawing, the display, real timing.
"""
import os
from gate_ui_fixture import *
import firmware_harness as H
C4=M['symbols']['cue4'];G7=M['symbols']['gate7']
REPORT=0x38800648;LINK=0x38800f90;STATE=0x20010000;LINK_MAGIC=0x4d444c31;PK_MAGIC=0x504b4431
HOLD_CALLS=2250;NONE=-128;CLIP=1
FONT=[0o75557,0o22222,0o71747,0o71717,0o55711,0o74717,0o74757,0o71111,0o75757,0o75717,0o00700]
def f2u(x):return struct.unpack('<I',struct.pack('<f',x))[0]
def published(q):
 magic,*v=struct.unpack('<I13b',q.mem_read(REPORT,17));return magic,v

# ------------------------------------------------------------------ M7 capture
def m7(linked=True):
 q=machine(patched,double=True);seen=[]
 for address,num in ((0x804bab0,1),(0x804bacc,2)):
  def take(q,num=num):seen.append((q.reg_read(UC_ARM_REG_R1),[q.reg_read(r) for r in (UC_ARM_REG_S0,UC_ARM_REG_S1)[:num]]))
  stub(q,address,take)
 if linked:
  # the block the delay constructor allocates: master dynamics state, then the hold
  import ctypes
  q.mem_write(LINK,struct.pack('<II',LINK_MAGIC,STATE));q.mem_write(STATE,bytes(4096))
 mix=0x24048000;q.mem_write(mix+0xe6c,struct.pack('<H',4));q.reg_write(UC_ARM_REG_R8,mix)
 return q,seen
def say(q,at,ch,l,r=None):
 q.reg_write(UC_ARM_REG_S0,f2u(l));q.reg_write(UC_ARM_REG_S1,f2u(l if r is None else r))
 q.reg_write(UC_ARM_REG_R8,0x24048000)
 call(q,at,0x2404a000,ch,end=at+4)
MONO,STL,STR,MASTER=0x804f8be,0x804f9c6,0x804f9e0,0x804e334
check(any(p['name']=='master meter peak capture' and p['address']==MASTER for p in M['patches']),'the output node\'s master meter call is patched')
q,seen=m7()
say(q,MONO,0,-12.3);say(q,STL,1,-20.,-6.2);say(q,STR,2,-33.,-40.);say(q,MASTER,12,-3.1,-9.)
magic,v=published(q)
check(magic==PK_MAGIC and v[0]==-25 and v[1]==-12 and v[2]==-66 and v[12]==-6 and all(x==NONE for x in v[3:12]),'mono, stereo and master levels are published in half dB, the louder side of a stereo pair, everything else empty')
check(seen==[(0,[f2u(-12.3)]),(1,[f2u(-20.),f2u(-6.2)]),(2,[f2u(-33.),f2u(-40.)]),(12,[f2u(-3.1),f2u(-9.)])],'the native meter publish calls still get exactly their channel and levels')
say(q,MONO,0,-30.);say(q,MONO,0,-45.)
check(published(q)[1][0]==-25,'a lower level does not replace the held peak')
say(q,MONO,0,-4.)
check(published(q)[1][0]==-8,'a higher level replaces it at once')
check(q.mem_read(REPORT+17,1)==b'\1','the status byte says the block is alive and levels arrive')
for _ in range(HOLD_CALLS-1):say(q,MONO,0,-30.)
check(published(q)[1][0]==-8,'the peak is still held one call short of 1.5 s (2250 blocks)')
say(q,MONO,0,-30.)
check(published(q)[1][0]==-60,'after 1.5 s without a higher level the readout falls to the current level')
check(published(q)[1][1]==-12,'other channels keep their own hold')
for level,want,what in ((0.,CLIP,'0 dB'),(3.,CLIP,'over 0 dB')):
 say(q,MONO,3,level);check(published(q)[1][3]==want,what+' is published as clipping')
for level,what in ((-60.,'-60 dB'),(-96.,'-96 dB'),(float('nan'),'NaN')):
 q2,_=m7();say(q2,MONO,4,level);check(published(q2)[1][4]==NONE,what+' publishes nothing to show')
say(q,MONO,5,-0.2);check(published(q)[1][5]==0,'-0.2 dB rounds to 0 without counting as clipping')
before=published(q);say(q,MASTER,13,-1.);say(q,STL,23,-1.)
check(published(q)==before,'channels other than the 12 tracks and the master are not recorded')
q,seen=m7(linked=False);say(q,MONO,0,-12.);say(q,MASTER,12,-3.)
check(bytes(q.mem_read(REPORT,20))==bytes(20) and len(seen)==2,'without the boot-time block nothing is published and the native meters are unaffected')
q,seen=m7(linked=False);q.mem_write(LINK,struct.pack('<II',LINK_MAGIC,0x08040000));say(q,MONO,0,-12.)
check(bytes(q.mem_read(REPORT,20))==bytes(20),'a link that does not point into RAM is refused')
for base in (0x20000040,0x24060000,0xc0100000):
 q,seen=m7(linked=False);q.mem_write(LINK,struct.pack('<II',LINK_MAGIC,base));q.mem_write(base,bytes(4096));say(q,MONO,0,-12.)
 check(published(q)[1][0]==-24,'a block in '+hex(base)[:4]+'.. RAM (DTCM on the device) is used')
# Gate view keeps Cobalt's behaviour: the bar shows the gate's detector, the readout the real level.
q,seen=m7();q.mem_write(0x24048000+0xe6c,struct.pack('<H',0x1ae));q.mem_write(0x38800400,struct.pack('<I',0x4e473937))
say(q,MONO,0,-12.)
check(published(q)[1][0]==-24 and seen[-1][1]!=[f2u(-12.)],'MIXER Gate view: the bar shows the gate detector as before, the number stays the channel level')
steps=[0]
def count(q,addr,size,data):steps[0]+=1
q,seen=m7();say(q,STL,1,-20.,-6.)
hk=q.hook_add(UC_HOOK_CODE,count);say(q,STL,1,-20.,-7.);q.hook_del(hk);steady=steps[0];steps[0]=0
hk=q.hook_add(UC_HOOK_CODE,count);say(q,STL,1,-20.,-3.);q.hook_del(hk)
print('INFO instructions for one stereo level through the hook (Cobalt gate shim + readout): steady',steady,', when the shown value changes',steps[0])
check(steady<150 and steps[0]<200,'the capture costs under 150 instructions a channel a block (13 channels: under a third of one stock compressor)')

# ------------------------------------------------------------------ M4 drawing
FAKEAPP=0x24070000;DESC=0x24071000;FB=0xc1000000
TILE0=0x14d0;STRIP0=0xbe0;STRIDE=0xd98;VT=0x814af64
BG=u32(u,0x814d230+2*4)&0xffffff;FG=u32(u,0x814d230+3*4)&0xffffff;RED=0xd02020;WHITE=0xffffff
PAINT=0x123456
w32(u,DESC,FB);u.mem_write(DESC+4,struct.pack('<HHBxH',320,240,1,4))
stub(u,0x810139a,lambda u:u.reg_write(UC_ARM_REG_R0,DESC))
presented=[];stub(u,C4['pc_present'],lambda u:presented.append(1))
ticks=[];stub(u,C4['gate_ui_tick'],lambda u:ticks.append(1))
w32(u,0x3000d9fc,FAKEAPP)
def show(page):w32(u,FAKEAPP+0x2b4,page)
def levels(v,magic=PK_MAGIC):u.mem_write(REPORT,struct.pack('<I13b3x',magic,*v))
def present(fill=PAINT):
 u.mem_write(FB,struct.pack('<I',0xff000000|fill)*(320*240));call(u,C4['cue_present'],0,1,0,0)
 return bytes(u.mem_read(FB,320*240*4))
def px(fb,x,y):return struct.unpack_from('<I',fb,(y*320+x)*4)[0]&0xffffff
def changed(fb):return {(i//4%320,i//4//320) for i in range(0,len(fb),4) if struct.unpack_from('<I',fb,i)[0]&0xffffff!=PAINT}
def tiles(page):
 out={}
 for ch in range(13):
  t=page+TILE0+ch*STRIDE
  if u32(u,t)!=VT:continue
  x,b,w,h=struct.unpack('<4i',u.mem_read(t+4,16))
  if w>0:out[ch]=(x,240-b-h,w,h)
 return out
def box(tile,text,clip=False):
 """Expected pixels: an opaque box in the tile's bottom-right corner."""
 x,top,w,h=tile;glyphs=[10 if c=='-' else int(c) for c in text];bw=len(glyphs)*4+1;x0=x+w-1-bw;y0=top+h-8;out={}
 for r in range(7):
  for c in range(bw):
   k,col,row=(c-1)//4,(c-1)%4,r-1
   on=c>=1 and col<3 and 0<=row<5 and FONT[glyphs[k]]>>((4-row)*3+(2-col))&1
   out[(x0+c,y0+r)]=(WHITE if clip else FG) if on else (RED if clip else BG)
 return out
def expect(fb,want):
 got=changed(fb)
 return got==set(want) and all(px(fb,x,y)==c for (x,y),c in want.items())
masks_off=lambda:u.mem_write(0x38800660,bytes(28))
for name,page in (('MIXER',P),('TRACK',T)):
 tl=tiles(page);show(page);masks_off()
 for ch in range(13):u.mem_write(page+STRIP0+ch*STRIDE+0x30,bytes([0 if ch<8 or ch==12 else 1]))
 levels([NONE]*13);check(not changed(present()) and presented,name+': nothing to show, nothing drawn, the present chain continues')
 levels([NONE]*13,magic=0);check(not changed(present()),name+': no report from the audio core, nothing drawn')
 v=[NONE]*13;v[0]=-24;v[3]=-7;v[5]=CLIP;v[7]=-119;v[12]=-1
 levels(v);fb=present();want={}
 texts={0:'12',3:'4',5:'0',7:'60',12:'1'}
 for ch,text in texts.items():
  wide=tl[ch][2]>=30 and ch!=5
  want.update(box(tl[ch],('-' if wide and text!='0' else '')+text,clip=ch==5))
 check(expect(fb,want),name+': -12, -3.5, clip, -59.5 and -0.5 dB appear as 12, 4, 0 on red, 60 and 1 in their own tiles\' bottom-right corners, and nothing else is touched')
 check(all(x>=tl[ch][0] and x<tl[ch][0]+tl[ch][2] and y>=tl[ch][1] and y<tl[ch][1]+tl[ch][3] for ch in texts for (x,y) in box(tl[ch],'-88')),name+': even the widest box stays inside its tile')
 v=[-20]*13;levels(v);fb=present()
 check(all(not any(tl[ch][0]<=x<tl[ch][0]+tl[ch][2] for (x,y) in changed(fb)) for ch in range(8,12) if ch in tl) ,name+': hidden strips (tracks 9-12 in 8-track mode) get no number')
 # with a cue icon and a greyed track the number is drawn on top, readable
 u.mem_write(0x38800660,struct.pack('<IiIIIII',0x43554534,0,0,0,1,0xffe,1));levels([-24]+[NONE]*12);fb=present()
 check(all(px(fb,x,y)==c for (x,y),c in box(tl[0],('-' if tl[0][2]>=30 else '')+'12').items()),name+': on a cued, greyed tile the number box is drawn last and stays whole')
 masks_off()
 if name=='MIXER':
  preview=(page,dict(tl))

# Redraw request: only when a shown number changes.
PAGES=0x24072000;VIEW=0x388001e0
def tick():
 u.mem_write(PAGES+0x38,b'\0\0');call(u,C4['cue_ui_tick'],PAGES,0);return bytes(u.mem_read(PAGES+0x38,2))
u.mem_write(VIEW,bytes(16));masks_off();levels([NONE]*13);tick()
check(tick()==b'\0\0','steady: no redraw is requested')
levels([-24]+[NONE]*12);check(tick()==b'\1\1' and tick()==b'\0\0','a new number requests one redraw of both pages')
levels([-23]+[NONE]*12);check(tick()==b'\0\0','half a dB that does not change the shown number requests nothing')
levels([-22]+[NONE]*12);check(tick()==b'\1\1','the next whole dB does')
levels([NONE]*13);check(tick()==b'\1\1' and tick()==b'\0\0','a number going away requests one redraw, so the box disappears')
check(len(ticks)>=6,'the Gate UI tick still runs after it')
# No font index can be out of range, whatever byte the audio core left there.
show(P)
for raw in range(-128,128):
 levels([raw]*13);present()
check(True,'every possible published byte draws without a fault')

# ------------------------------------------------------------ CPU % on TOOLS
# The real Tools page from its native constructor; the report is what the M7
# profiler hook publishes for the header bar (0x38800f00, tenths of a per cent).
FONT+=[0o11244,0o51245,0]
t=machine(patched);theap=[0xc0010000]
def tallocate(q):a=theap[0];theap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
stub(t,0x8103128,tallocate);call(t,0x8134cb8,0);TOOLS=0xc0140000;call(t,0x813140c,TOOLS)
check(u32(t,TOOLS)==0x814ba0c and struct.unpack('<4i',t.mem_read(TOOLS+0xd4+4,16))==(106,182,106,38) and bytes(t.mem_read(TOOLS+0xd4+0x65,8))==b'Cblt+Dyn','the native Tools page has the class and middle field the readout looks for')
w32(t,DESC,FB);t.mem_write(DESC+4,struct.pack('<HHBxH',320,240,1,4))
stub(t,0x810139a,lambda q:q.reg_write(UC_ARM_REG_R0,DESC));stub(t,C4['pc_present'],lambda q:None);stub(t,C4['gate_ui_tick'],lambda q:None)
w32(t,0x3000d9fc,FAKEAPP)
def cpu(avg,peak,magic=0x44504355):t.mem_write(0x38800f00,struct.pack('<IHHI',magic,avg,peak,1))
def tpresent():
 t.mem_write(FB,struct.pack('<I',0xff000000|PAINT)*(320*240));call(t,C4['cue_present'],0,1,0,0)
 fb=bytes(t.mem_read(FB,320*240*4))
 return {(i//4%320,i//4//320):struct.unpack_from('<I',fb,i)[0]&0xffffff for i in range(0,len(fb),4) if struct.unpack_from('<I',fb,i)[0]&0xffffff!=PAINT}
def cpu_box(text,ink):
 g=[{'/':11,'%':12,' ':13}.get(c) if c in '/% ' else int(c) for c in text];bw=len(g)*4+1;x0=106+(106-bw)//2;y0=240-182-2-7;out={}
 for r in range(7):
  for c in range(bw):
   k,col,row=(c-1)//4,(c-1)%4,r-1
   out[(x0+c,y0+r)]=ink if c>=1 and col<3 and 0<=row<5 and FONT[g[k]]>>((4-row)*3+(2-col))&1 else BG
 return out
w32(t,FAKEAPP+0x2b4,TOOLS)
def status(v):t.mem_write(REPORT+17,bytes([v]))
for avg,peak,text,ink,what,st in ((423,608,'42/61% 0',FG,'text colour',0),(655,744,'66/74% 3',0xffb000,'amber over 70 %',3),(880,1000,'88/100% 7',0xff2828,'red over 90 %',7),(4,9,'0/1% 1',FG,'text colour',1),(5000,9999,'100/100% 7',0xff2828,'clamped',0xff)):
 cpu(avg,peak);status(st);got=tpresent()
 check(got==cpu_box(text,ink),f'TOOLS shows {text} ({what}) centred along the bottom of the middle field, and nothing else is drawn')
check(all(106<=x<212 and 20+24<=y<58 for (x,y) in cpu_box('100/100% 7',FG)),'the widest readout stays inside the field, below the middle of it where the native text sits')
cpu(423,608,magic=0);check(not tpresent(),'no report from the audio core: nothing drawn')
cpu(423,608);t.mem_write(TOOLS+0xd4+0x30,b'\1');check(not tpresent(),'field hidden: nothing drawn');t.mem_write(TOOLS+0xd4+0x30,b'\0')
w32(t,FAKEAPP+0x2b4,0x24073000);t.mem_write(0x24073000,bytes(0x400));check(not tpresent(),'any other page: nothing drawn')
def ttick():
 t.mem_write(PAGES+0x38,b'\0\0');call(t,C4['cue_ui_tick'],PAGES,0);return bytes(t.mem_read(PAGES+0x38,2))
t.mem_write(0x388001e0,bytes(16));ttick()
check(ttick()==b'\0\0','away from TOOLS the CPU numbers never request a redraw');cpu(500,700);check(ttick()==b'\0\0','(even when they change)')
w32(t,FAKEAPP+0x2b4,TOOLS);check(ttick()==b'\1\1' and ttick()==b'\0\0','opening TOOLS requests one redraw')
cpu(502,703);check(ttick()==b'\0\0','a change under one per cent requests nothing');status(3);check(ttick()==b'\1\1','a new status digit requests one redraw');cpu(520,703);check(ttick()==b'\1\1','a new whole per cent requests one redraw')
# the track pages are unaffected by a CPU report
show(P);masks_off();levels([NONE]*13);u.mem_write(0x38800f00,struct.pack('<IHHI',0x44504355,423,608,1));check(not changed(present()),'MIXER: the CPU numbers are not drawn there')

# A picture of the placement, for looking at before it is on hardware.
out=os.environ.get('PEAK_PREVIEW')
if out:
 from PIL import Image
 page,tl=preview;show(page);masks_off()
 levels([-24,-12,-37,-6,NONE,CLIP,-90,-2,NONE,NONE,NONE,NONE,-9])
 base=Image.new('RGB',(320,240),(0x14,0x14,0x14))
 for ch,(x,top,w,h) in tl.items():
  if ch<8 or ch==12:
   for yy in range(top,top+h):
    for xx in range(x+1,x+w-1):base.putpixel((xx,yy),(0x56,0x56,0x5a))
 raw=b''.join(struct.pack('<I',0xff000000|(r<<16)|(g<<8)|b) for (r,g,b) in base.get_flattened_data())
 u.mem_write(FB,raw);call(u,C4['cue_present'],0,1,0,0);fb=bytes(u.mem_read(FB,320*240*4))
 img=Image.new('RGB',(320,240));img.putdata([((p>>16)&255,(p>>8)&255,p&255) for p in struct.unpack('<76800I',fb)])
 bottom=min(240,max(top+h for (x,top,w,h) in tl.values())+4)
 img.crop((0,0,320,bottom)).resize((1280,bottom*4),Image.NEAREST).save(out)
print(str(H.checks)+' peak readout checks passed. Where the number sits against the native track number, the hold time and readability need the hardware.')
