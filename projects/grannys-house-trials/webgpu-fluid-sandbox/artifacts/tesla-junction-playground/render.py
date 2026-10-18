import os
os.environ['MPLCONFIGDIR']='/tmp/mplconfig'
import json,numpy as np
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
out=Path(__file__).parent
z=np.array(json.loads((out/'terrain.json').read_text())['ElevationInches']).reshape(64,96)
s=json.loads((out/'junction-state.json').read_text())
m=np.array(s['Momentum']).reshape(64,96,2);h=np.array(s['Water']).reshape(64,96)
u=(m[:,:,0]+np.pad(m[:,:-1,0],((0,0),(1,0))))/2
v=(m[:,:,1]+np.pad(m[:-1,:,1],((1,0),(0,0))))/2
fig=plt.figure(figsize=(16,12),facecolor='white')
fig.text(.055,.955,'TESLA JUNCTION PLAYGROUND',fontsize=24,weight='bold')
fig.text(.055,.923,'Proposed terrain • two separated hook stages • 4-ft nominal passages • level floor',fontsize=13)
ax=fig.add_axes([.055,.39,.91,.50])
def grid(ax,xlim,ylim):
 ax.imshow(z,cmap='gray',vmin=0,vmax=60,extent=(0,96,64,0),interpolation='nearest');ax.set_aspect('equal');ax.set_xlim(*xlim);ax.set_ylim(*ylim)
 ax.set_xticks(np.arange(0,97,8));ax.set_yticks(np.arange(0,65,8));ax.set_xticks(np.arange(97),minor=True);ax.set_yticks(np.arange(65),minor=True)
 ax.grid(which='minor',color='white',lw=.3,alpha=.6);ax.grid(which='major',color='#73808b',lw=.6,alpha=.55);ax.tick_params(which='minor',length=0);ax.set_xlim(*xlim);ax.set_ylim(*ylim)
grid(ax,(0,96),(48,0))
for i,(cx,cy) in enumerate([(22,34),(66,16)],1):ax.text(cx,cy,str(i),ha='center',color='#075a94',fontsize=20,weight='bold')
ax.text(1,22,'A',color='#6dd5ff',weight='bold',va='center');ax.text(94,34,'B',color='#6dd5ff',weight='bold',va='center')
for xj,yj in [(22,24),(66,26)]:ax.plot(xj,yj,'o',mfc='none',mec='#e7a650',ms=11,mew=1.7)
ax.set_xlabel('1 square = 1 ft • 96 × 64 terrain; unused bottom rows cropped')
zoom=fig.add_axes([.055,.105,.38,.22]);grid(zoom,(14,30),(32,18))
y,x=np.mgrid[:64,:96];sel=(z==0)&(x>=14)&(x<30)&(y>=18)&(y<32)&(np.hypot(u,v)>.03)
zoom.quiver(x[sel]+.5,y[sel]+.5,u[sel],v[sel],color='#54caff',angles='xy',scale_units='xy',scale=1.1,width=.006,headwidth=3.5)
zoom.set_title('Actual MAC velocity • first junction, A → B',fontsize=12,pad=12)
fig.text(.49,.315,'GEOMETRY',fontsize=13,weight='bold',color='#075a94')
fig.text(.49,.287,'Return and shortcut meet at 146° in the ideal design.',fontsize=11)
fig.text(.49,.262,'Raster: two intact islands, 159 solid cells each.',fontsize=11)
fig.text(.49,.237,'Black bed: 0 in. Gray terrain: +48 in. No imposed slope.',fontsize=11)
fig.text(.49,.193,'EXPLORATORY FLOW CHECK',fontsize=13,weight='bold',color='#075a94')
fig.text(.49,.165,'Matched 18 / 12 in reservoir depths; advection enabled.',fontsize=11)
fig.text(.49,.14,'A → B: 3.962 ft³/s     B → A: 3.988 ft³/s',fontsize=11)
fig.text(.49,.115,'0.65% directional difference; strong rectification not established.',fontsize=11)
fig.text(.055,.052,'Junction circles mark intended interaction sites. Blue inset arrows are sampled solver velocities, not prescribed paths.',fontsize=11)
fig.text(.055,.028,'Design artifact only • application unchanged • results from the existing CPU MAC implementation, not GPU validation',fontsize=10)
fig.savefig(out/'map.png',dpi=170,facecolor='white')
