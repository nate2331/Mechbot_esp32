from PIL import Image, ImageDraw, ImageFont
from pathlib import Path

out=Path(__file__).parent
im=Image.new('RGB',(1500,1170),'#f6f8fc'); d=ImageDraw.Draw(im)
fontdir=Path('C:/Windows/Fonts')
def f(n,b=False): return ImageFont.truetype(str(fontdir/('arialbd.ttf' if b else 'arial.ttf')),n)
def text(x,y,s,n=19,fill='#172b45',b=False): d.text((x,y),s,font=f(n,b),fill=fill)
cols={c:240+i*35+(70 if i>=5 else 0) for i,c in enumerate('ABCDEFGHIJ')}
def pt(h): return cols[h[0]],140+(int(h[1:])-1)*44
colors={'3V3':'#d76400','GND':'#425268','TX':'#b73099','RX':'#087dad','5V':'#c72d3a','RST':'#7750b5','BASE':'#287d4a','CTL':'#a06520'}
text(55,30,'LVC + optional reset | ElectroCookie 17-row board',32,b=True)
text(55,76,'COMPONENT SIDE • Row 1 at top • Placement map; add the jumper list at right',21)
d.rounded_rectangle((212,110,730,915),22,fill='#ffe9de',outline='#b65641',width=3)
netsL={3:'3V3',4:'TX',**{r:'GND' for r in range(5,13)},13:'3V3',14:'GND',15:'RX',16:'CTL',17:'5V'}
netsR={1:'TX',2:'RST',3:'3V3',4:'GND',5:'RX',13:'5V',14:'GND',15:'BASE',16:'RST'}
for c,x in cols.items(): text(x-7,112,c,16,b=True)
for r in range(1,18):
 y=pt('A'+str(r))[1]; text(186,y-11,str(r),18,b=True)
 for side,cs,nets in [('L','ABCDE',netsL),('R','FGHIJ',netsR)]:
  color=colors.get(nets.get(r),'#b1b5bc')
  d.line([pt(cs[0]+str(r)),pt(cs[-1]+str(r))],fill=color,width=9)
  for c in cs:
   x,_=pt(c+str(r));d.ellipse((x-7,y-7,x+7,y+7),fill='#fff',outline=color,width=2)
# Socket and IC leads: E3-E12, F3-F12.
for r in range(3,13):
 y=pt('E'+str(r))[1]
 d.line((380,y,414,y),fill='#788494',width=6)
 d.line((465,y,485,y),fill='#788494',width=6)
d.rounded_rectangle((404,208,486,643),9,fill='#233346',outline='#101c2b',width=3)
d.arc((431,195,459,224),0,180,fill='#fff',width=3)
text(414,290,'LVC',22,fill='white',b=True)
text(413,319,'245',22,fill='white',b=True)
for r in range(3,13):
 y=pt('E'+str(r))[1]
 text(408,y-9,str(r-2),14,fill='white')
 text(462,y-9,str(23-r),14,fill='white')
text(362,172,'NOTCH UP',17,b=True)
def terminal(h,label,left=False):
 x,y=pt(h); color=colors.get(label.split()[0],'#172b45')
 d.rectangle((x-10,y-10,x+10,y+10),outline=color,width=3)
 if left: text(20,y-11,h+'  '+label,18,b=True)
 else: text(740,y-10,h+' '+label,16,b=True)
for h,s in [('A13','3V3'),('A14','GND'),('A15','RX GPIO21'),('A16','CTL GPIO25'),('A17','5V')]:terminal(h,s,True)
for h,s in [('J1','TX from IMU'),('J2','RST to IMU'),('J13','5V to IMU'),('J14','GND to IMU')]:terminal(h,s)
text(20,639,'MAKER 1×5',18,b=True)
# Bus joins all grounded A-side IC rows, shown as proposed wire.
d.line([pt('A5'),pt('A12')],fill='#253448',width=5)
for r in range(5,13):
 x,y=pt('A'+str(r));d.ellipse((x-4,y-4,x+4,y+4),fill='#253448')
text(20,414,'Ground bus',20,b=True)
text(20,442,'Solder A5–A12',17)
text(20,467,'at every row.',17)
# Capacitor and resistors.
def component(a,b,label,color):
 p,q=pt(a),pt(b); d.line([p,q],fill=color,width=6)
 mx,my=(p[0]+q[0])/2,(p[1]+q[1])/2
 d.rounded_rectangle((mx-17,my-10,mx+17,my+10),3,fill=color)
 text(mx+22,my-11,label,16,fill=color,b=True)
component('H3','H4','C1 100nF','#086c91')
component('D16','F15','R1 1k','#935509')
component('F14','I15','R2 10k','#935509')
for h,s in [('H14','E'),('H15','B'),('H16','C')]:
 x,y=pt(h); d.ellipse((x-10,y-10,x+10,y+10),fill='#1b7153');text(x-6,y-9,s,15,fill='white',b=True)
text(562,873,'Q1: H14 E / H15 B / H16 C',15,b=True)
# Reference wire list on right.
text(995,136,'ADD INSULATED JUMPERS',21,b=True)
lines=[('C13 → C3','3.3V → DIR'),('D3 → G3','3.3V → VCC (around top)'),('B14 → B12','Maker ground → pin 10'),('A5 → A12','Bus: solder EVERY row'),('C12 → J4','Ground → /OE'),('G14 → I4','Emitter + IMU ground'),('B17 → G13','5V → IMU power'),('B15 → G5','Buffer output → GPIO21'),('G1 → D4','IMU TX → buffer input'),('I2 → I16','IMU reset → collector')]
for i,(a,b) in enumerate(lines):
 y=180+i*59;text(995,y,a,21,b=True);text(995,y+27,b,17)
text(995,800,'R1: D16 → F15     1 kΩ',20,b=True)
text(995,834,'R2: F14 → I15     10 kΩ',20,b=True)
text(995,868,'C1: H3 → H4       100 nF',20,b=True)
text(55,950,'Q1 labels specify electrical destinations, NOT the physical leg order.',24,b=True)
text(55,990,'Confirm the actual transistor pinout before soldering. No flat-face orientation is assumed.',21)
text(55,1030,'Keep P0 tied to IMU VDC. LVC VCC is 3.3V. Unused outputs F6–F12 stay disconnected.',21)
text(55,1070,'Only each five-hole strip is preconnected. Colored strips show nets; they do not join different rows.',20)
text(55,1110,'Read PERFBOARD_LAYOUT.md for complete assembly and continuity checks. Current firmware does not drive reset.',18)
im.save(out/'perfboard-layout.png')
