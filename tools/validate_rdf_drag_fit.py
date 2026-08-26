#!/usr/bin/env python3
"""Mirror of the exact Enforce NelderMeadDrag to validate the port numerically.
Matches RDF_RadarBallistics.NelderMeadDrag step-for-step so we confirm the
in-game algorithm (not a subtly different python one) yields ~8-12m impact
error on the WLR sampled+noised history."""
import sys, math, random
from pathlib import Path
TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
from quantify_wlr_realism import (
    vacuum_fit, integrate_rk2,
)


# --- replica of Enforce DragFitResidual / IntegrateDragFrom ---
_INTEG_MEM = {}
def integrate_drag_from(anchor, vel, dur, drag, dt=0.05):
    if dur <= 0.0:
        return anchor, vel
    # Memoize by rounded params: many Nelder-Mead iterations share the same
    # anchor + durations (only vel/drag change), so this is a large speedup.
    key = (tuple(round(x, 3) for x in anchor),
           tuple(round(x, 2) for x in vel),
           round(dur, 2), round(drag, 6))
    if key in _INTEG_MEM:
        return _INTEG_MEM[key]
    t = 0.0; p = list(anchor); v = list(vel)
    def accel(ve):
        sp = math.sqrt(ve[0]**2+ve[1]**2+ve[2]**2)
        return (-drag*sp*ve[0], -9.81-drag*sp*ve[1], -drag*sp*ve[2])
    while t + 1e-18 < dur:
        step = min(dt, dur - t)
        a = accel(v)
        vmid = (v[0]+a[0]*step*0.5, v[1]+a[1]*step*0.5, v[2]+a[2]*step*0.5)
        amid = accel(vmid)
        p = (p[0]+vmid[0]*step, p[1]+vmid[1]*step, p[2]+vmid[2]*step)
        v = (v[0]+amid[0]*step, v[1]+amid[1]*step, v[2]+amid[2]*step)
        t += step
    _INTEG_MEM[key] = (p, v)
    return _INTEG_MEM[key]


def dragfit_resid(positions, times, anchor, vel, drag):
    t0 = times[0]
    sum_sq = 0.0; used = 0
    for i in range(1, len(positions)):
        dur = times[i] - t0
        outP, _ = integrate_drag_from(anchor, vel, dur, drag)
        d = (outP[0]-positions[i][0], outP[1]-positions[i][1], outP[2]-positions[i][2])
        sum_sq += d[0]**2+d[1]**2+d[2]**2
        used += 1
    return 1e10 if used == 0 else sum_sq


def nelder_mead_drag(positions, times, anchor, vel_init, drag_lo, drag_hi, wind=None):
    # Mirror RDF_RadarBallistics.NelderMeadDrag (RDF 1.1.6): 5th vertex must
    # perturb ONLY the drag axis so the simplex spans drag (dart = dragInit
    # duplicated vertex 0 and pinned drag to the prior).
    init = list(vel_init)
    drag_init = 0.000615  # prefab prior (AIR_DRAG_SHELL_82MM_HE)
    simplex_vel = [tuple(init)]
    simplex_drag = [drag_init]
    simplex_cost = [dragfit_resid(positions,times,anchor,simplex_vel[0],simplex_drag[0])]
    pert = 5.0
    for axis in range(3):
        p = list(init)
        p[axis] += pert
        d = drag_init
        simplex_vel.append(tuple(p)); simplex_drag.append(d)
        simplex_cost.append(dragfit_resid(positions,times,anchor,p,d))
    drag_span = drag_hi - drag_lo
    if drag_span < 1.0e-6:
        drag_span = max(1.0e-3, abs(drag_init) * 0.3)
    dart = drag_init + 0.3 * drag_span
    if dart > drag_hi:
        dart = drag_hi
    if dart < drag_lo:
        dart = drag_lo
    simplex_vel.append(tuple(init)); simplex_drag.append(dart)
    simplex_cost.append(dragfit_resid(positions,times,anchor,init,dart))

    for _ in range(90):
        worst = 0
        for i in range(1,len(simplex_cost)):
            if simplex_cost[i] > simplex_cost[worst]: worst = i
        cx=[0.0,0.0,0.0]; cd=0.0
        cnt=0
        for i in range(len(simplex_cost)):
            if i==worst: continue
            for j in range(3): cx[j]+=simplex_vel[i][j]
            cd+=simplex_drag[i]; cnt+=1
        inv=1.0/cnt
        cx=[x*inv for x in cx]; cd*=inv
        wv=simplex_vel[worst]; wd=simplex_drag[worst]
        refl_vel=tuple(cx[j]+(cx[j]-wv[j]) for j in range(3))
        refl_drag=min(max(cd+(cd-wd),drag_lo),drag_hi)
        cost_ref= dragfit_resid(positions,times,anchor,refl_vel,refl_drag)
        best_cost=min(simplex_cost)
        if cost_ref < best_cost:
            exp_vel=tuple(cx[j]+(cx[j]-wv[j])*1.6 for j in range(3))
            exp_drag=min(max(cd+(cd-wd)*1.6,drag_lo),drag_hi)
            cost_exp= dragfit_resid(positions,times,anchor,exp_vel,exp_drag)
            if cost_exp<cost_ref:
                simplex_vel[worst]=exp_vel; simplex_drag[worst]=exp_drag; simplex_cost[worst]=cost_exp
            else:
                simplex_vel[worst]=refl_vel; simplex_drag[worst]=refl_drag; simplex_cost[worst]=cost_ref
        else:
            worst_cost=simplex_cost[worst]
            if cost_ref < worst_cost:
                simplex_vel[worst]=refl_vel; simplex_drag[worst]=refl_drag; simplex_cost[worst]=cost_ref
            else:
                best=0
                for i in range(1,len(simplex_cost)):
                    if simplex_cost[i]<simplex_cost[best]: best=i
                bv=simplex_vel[best]; bd=simplex_drag[best]
                for i in range(len(simplex_cost)):
                    if i==best: continue
                    nv=tuple(bv[j]+(simplex_vel[i][j]-bv[j])*0.5 for j in range(3))
                    nd=bd+(simplex_drag[i]-bd)*0.5
                    simplex_vel[i]=nv; simplex_drag[i]=nd
                    simplex_cost[i]=dragfit_resid(positions,times,anchor,nv,nd)
    bi=0
    for i in range(1,len(simplex_cost)):
        if simplex_cost[i]<simplex_cost[bi]: bi=i
    return simplex_vel[bi], min(max(simplex_drag[bi],drag_lo),drag_hi)


def find_ground_intersection_py(pos, vel, drag, ground_y=0.0, max_t=90.0, dt=0.02, backward=False):
    p=list(pos); v=list(vel); t=0.0
    direction = -1.0 if backward else 1.0
    prev_p=list(p); prev_y=p[1]
    def accel(ve):
        sp=math.sqrt(ve[0]**2+ve[1]**2+ve[2]**2)
        return (-drag*sp*ve[0], -9.81-drag*sp*ve[1], -drag*sp*ve[2])
    while abs(t) < max_t:
        a=accel(v)
        vmid=(v[0]+a[0]*dt*0.5*direction, v[1]+a[1]*dt*0.5*direction, v[2]+a[2]*dt*0.5*direction)
        amid=accel(vmid)
        np=[p[0]+vmid[0]*dt*direction, p[1]+vmid[1]*dt*direction, p[2]+vmid[2]*dt*direction]
        v=[v[0]+amid[0]*dt*direction, v[1]+amid[1]*dt*direction, v[2]+amid[2]*dt*direction]
        if (prev_y-ground_y)>=0 and (np[1]-ground_y)<=0:
            frac=(prev_y-ground_y)/max(prev_y-np[1],1e-9)
            return (prev_p[0]+(np[0]-prev_p[0])*frac, ground_y, prev_p[2]+(np[2]-prev_p[2])*frac)
        prev_p=p=list(np); prev_y=np[1]; t+=dt*direction
    return None


def main():
    from simulate_wlr_tracker import US_CFG, build_hw, build_shell_trajectory, polar, RADAR_AGL_M
    radar=(0.0,RADAR_AGL_M,0.0)
    shell=build_shell_trajectory(1,80.0,radar,v0_ms=210.0,elev_deg=55.0)
    flight=shell.samples[-1][0]
    true_impact=shell.samples[-1][1]
    # sparse sample times like the Enforce window
    times=[]; t=0.0
    import math
    while t<=flight:
        fwd=45*math.sin(1.2*t)
        if abs(fwd)<=6 and (not times or t-times[-1]>=0.2):
            times.append(t)
        t+=0.05
    errs=[]
    for seed in range(40):
        rng=random.Random(seed)
        pts=[]
        for tt in times:
            it=None
            for s in shell.samples:
                if s[0]<=tt+1e-9: it=s
                else: break
            if not it: continue
            r,az,el,delta=polar(radar,it[1])
            # add modest range noise
            n=rng.gauss(0.0,8.0)
            pp=(it[1][0]+n, it[1][1]+n*0.5, it[1][2]+n)
            pts.append((tt,pp))
        if len(pts)<4: continue
        pos=[p for _,p in pts]; tms=[t for t,_ in pts]
        vf=vacuum_fit(pos,tms)
        if not vf: continue
        # Enforce path: NelderMeadDrag rooted at first window sample
        dv,dd=nelder_mead_drag(pos,tms,pos[0],vf["vel"],0.000615*0.35,0.000615*3.0)
        ground_h = shell.samples[0][1][1]   # launch height
        imp=find_ground_intersection_py(pos[0],dv,dd,ground_y=ground_h)
        if imp:
            errs.append(math.dist(imp,(true_impact[0],true_impact[1],true_impact[2])))
    errs.sort()
    med=errs[len(errs)//2] if errs else None
    print(f"Enforce-mirror NelderMeadDrag impact error median = {med:.1f} m over {len(errs)} trials")
    return 0

if __name__=="__main__":
    sys.exit(main())
