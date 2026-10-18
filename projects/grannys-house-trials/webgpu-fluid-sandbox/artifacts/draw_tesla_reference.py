import os
os.environ['MPLCONFIGDIR']='/tmp/mplconfig'
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.path import Path
from matplotlib.patches import PathPatch
from PIL import Image
from pathlib import Path as FS
out=FS(__file__).resolve().parent
import urllib.request
source=out/'tesla-source-figure.png'
if not source.exists():
    urllib.request.urlretrieve('https://media.springernature.com/full/springer-static/image/art%3A10.1038%2Fs41467-021-23009-y/MediaObjects/41467_2021_23009_Fig1_HTML.png',source)
fig=plt.figure(figsize=(16,9),facecolor='#f7f8fa')
fig.text(.055,.95,'TESLA VALVE — TOP-DOWN GEOMETRY',fontsize=23,weight='bold',color='#172b3a')
fig.text(.055,.91,'Repeated asymmetric hooked passages and solid islands — a level-floor shape reference',fontsize=12,color='#435562')
# Draw an idealized tracing of the patent's alternating, hooked perimeter and islands.
def chain(lips,sign):
 v=[(0,2*sign)]; c=[Path.MOVETO]
 for a in lips:
  v.extend([(a,0),(a-3,0.3*sign),(a-3,4*sign),(a,4*sign)])
  c.extend([Path.LINETO,Path.CURVE4,Path.CURVE4,Path.CURVE4])
 v.append((120,2*sign));c.append(Path.LINETO)
 return v,c

def panel(rect,reverse=False):
 ax=fig.add_axes(rect);ax.set_xlim(-4,124);ax.set_ylim(-8,8);ax.set_aspect('equal');ax.axis('off')
 ax.fill_between([0,120],[-5.5,-5.5],[5.5,5.5],color='#647381')
 top,tc=chain([15,35,55,75,95],1);bot,bc=chain([5,25,45,65,85,105],-1)
 # Compound shape: outline is sampled from cubic paths and then joined in reverse.
 def sample(v,c):
  coords=[v[0]]; i=1
  while i<len(v):
   if c[i]==Path.CURVE4:
    a=coords[-1]; b,d,e=v[i:i+3]
    for j in range(1,41):
     t=j/40; u=1-t
     coords.append(tuple(u**3*a[k]+3*u*u*t*b[k]+3*u*t*t*d[k]+t**3*e[k] for k in (0,1)))
    i+=3
   else: coords.append(v[i]); i+=1
  return coords
 boundary=sample(top,tc)+list(reversed(sample(bot,bc)))
 ax.fill(*zip(*boundary),facecolor='#e2f1f6',edgecolor='#172b3a',linewidth=1.4)
 for sign,lips in [(1,[15,35,55,75,95]),(-1,[5,25,45,65,85,105])]:
  for a in lips:
   v=[(a,3*sign),(a+9,1.5*sign),(a+6,.95*sign),(a,2*sign),(a-1.4,2.2*sign),(a-1.4,3.2*sign),(a,3*sign),(a,3*sign)]
   codes=[Path.MOVETO,Path.LINETO,Path.LINETO,Path.LINETO,Path.CURVE4,Path.CURVE4,Path.CURVE4,Path.CLOSEPOLY]
   ax.add_patch(PathPatch(Path(v,codes),facecolor='#647381',edgecolor='#172b3a',lw=1.1))
 color='#bb4138' if reverse else '#087ca7'
 for x in [0,40,80,118]:
  start,end=((x+5,-.1),(x-1,-.1)) if reverse else ((x-1,-.1),(x+5,-.1))
  ax.annotate('',xy=end,xytext=start,arrowprops=dict(arrowstyle='-|>',color=color,lw=2,mutation_scale=16))
 return ax
ax=panel([.055,.57,.89,.25])
fig.text(.055,.835,'LOWER-RESISTANCE DIRECTION  →',color='#087ca7',fontsize=14,weight='bold')
fig.text(.055,.575,'The central corridor offers comparatively direct passage. The solid islands and hooked wall mouths stay in place.',fontsize=11,color='#435562')
ax2=panel([.055,.285,.89,.25],True)
fig.text(.055,.54,'HIGHER-RESISTANCE DIRECTION  ←',color='#bb4138',fontsize=14,weight='bold')
fig.text(.055,.28,'Reversing the drive changes the flow: repeated diversion, turning, separation and mixing increase losses.',fontsize=11,color='#435562')
fig.text(.055,.25,'Arrows indicate imposed direction only; they are not simulated streamlines. Reverse flow is restricted, not stopped.',fontsize=10,color='#435562')
# Include the published patent drawing so the new idealized schematic is independently checkable.
im=Image.open(source);crop=im.crop((0,0,im.width,126))
ref=fig.add_axes([.055,.105,.89,.12]);ref.imshow(crop);ref.axis('off')
fig.text(.055,.075,'Original patent planform for comparison: US1329559A (1920), reproduced in Nguyen et al., Nature Communications (2021), Fig. 1a (CC BY 4.0).',fontsize=9,color='#435562')
fig.text(.055,.05,'New schematic above is idealized, not a dimensioned fabrication drawing. Keep the floor level for a directional-resistance test.',fontsize=9,color='#435562')
fig.savefig(out/'tesla-valve-reference.png',dpi=170,facecolor=fig.get_facecolor())
print(out/'tesla-valve-reference.png')
