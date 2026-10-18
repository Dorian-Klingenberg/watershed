import os
os.environ['MPLCONFIGDIR']='/tmp/mplconfig'
import numpy as np
from scipy.ndimage import label,distance_transform_edt
from pathlib import Path
import json
out=Path(__file__).parent

def cubic(a,b,c,d,n=100):
 t=np.linspace(0,1,n)[:,None];u=1-t
 return u**3*np.array(a)+3*u*u*t*np.array(b)+3*u*t*t*np.array(c)+t**3*np.array(d)

def loop():
 return np.vstack([cubic((10,32),(10,38),(14,40),(18,40)),cubic((18,40),(28,40),(32,36),(32,32))[1:],cubic((32,32),(32,28),(32,24),(30,24))[1:],np.array([(22,24)])])
loops=[];cuts=[]
for dx,py,sgn in [(0,32,1),(44,18,-1)]:
 def transform(p):
  p=np.array(p,dtype=float).copy();p[:,0]+=dx;p[:,1]=py+sgn*(p[:,1]-32);return p
 loops.append(transform(loop()));cuts.append(transform([(10,32),(22,24)]))
connectors=[cubic((0,22),(6,22),(10,24),(10,32)),np.vstack([[(22,24),(22,16)],cubic((22,16),(22,8),(42,10),(44,26))[1:],cubic((44,26),(48,32),(54,26),(54,18))[1:]]),np.vstack([[(66,26),(66,34)],cubic((66,34),(66,42),(88,34),(96,34))[1:]])]
y,x=np.mgrid[:64,:96];points=np.stack([x+.5,y+.5],axis=-1)
def dist(line):
 d=np.full((64,96),1e6)
 for a,b in zip(line[:-1],line[1:]):
  v=b-a
  if np.dot(v,v)<1e-14:continue
  t=np.clip(np.sum((points-a)*v,axis=-1)/np.dot(v,v),0,1)
  d=np.minimum(d,np.linalg.norm(points-a-t[...,None]*v,axis=-1))
 return d
wet=np.minimum.reduce([dist(p) for p in loops+cuts+connectors])<=2
lab,n=label(~wet);islands=[]
for i in range(1,n+1):
 yy,xx=np.where(lab==i)
 if not (np.any(xx==0)|np.any(xx==95)|np.any(yy==0)|np.any(yy==63)):islands.append({'cells':len(xx),'centroid':[float(xx.mean()+.5),float(yy.mean()+.5)]})
metrics={'fluid_components':label(wet)[1],'islands':islands,'max_cell_centre_clearance_diameter':2*distance_transform_edt(wet).max(),'fluid_cells':int(wet.sum()),'ideal_arrival_angle_degrees':float(np.degrees(np.arccos(-12/np.sqrt(12**2+8**2))))}
print(json.dumps(metrics,indent=2))
(out/'geometry-checks.json').write_text(json.dumps(metrics,indent=2))
z=np.where(wet,0,48)
(out/'terrain.json').write_text(json.dumps({'Version':1,'Name':'tesla-junction-playground-proposal','Width':96,'Height':64,'ElevationInches':z.ravel().tolist()}))
np.save(out/'wet.npy',wet)