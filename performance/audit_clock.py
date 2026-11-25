#!/usr/bin/env python3
"""Read-only device-tree operating-point inventory and runtime tuning planner."""
import argparse
import json
import pathlib
import re
import subprocess
import unittest

def call(dtb, *args):
    p=subprocess.run(["fdtget",*args[:1],str(dtb),*args[1:]],capture_output=True,text=True)
    return p.stdout.strip() if p.returncode==0 else None

def words(dtb,node,prop):
    p=subprocess.run(["fdtget","-t","x",str(dtb),node,prop],capture_output=True,text=True)
    if p.returncode: return None
    return [int(x,16) for x in p.stdout.split()]

def strings(dtb,node,prop):
    p=subprocess.run(["fdtget","-t","s",str(dtb),node,prop],capture_output=True,text=True)
    return p.stdout.strip() if p.returncode==0 else None

def hz(cells):
    if not cells or len(cells)!=2: return None
    return (cells[0]<<32)|cells[1]

def inventory(dtb):
    nodes={}
    stack=["/"]
    while stack:
        path=stack.pop()
        props=(call(dtb,"-p",path) or "").splitlines()
        nodes[path]=set(props)
        children=(call(dtb,"-l",path) or "").splitlines()
        stack.extend(path.rstrip("/")+"/"+child for child in children)
    board=strings(dtb,"/","compatible") or ""
    if "apple,j72s" not in board.split(): raise ValueError("Not J72s")
    handles={}
    for path,props in nodes.items():
        for key in ("phandle","linux,phandle"):
            if key in props:
                v=words(dtb,path,key)
                if v: handles[v[0]]=path
    tables={}
    for path,props in nodes.items():
        if "opp-hz" not in props: continue
        parent=path.rsplit("/",1)[0] or "/"
        state={"node":path,"hz":hz(words(dtb,path,"opp-hz")),
               "status":strings(dtb,path,"status") or "okay",
               "turbo":"turbo-mode" in props,"suspend":"opp-suspend" in props,
               "voltage_uv":words(dtb,path,"opp-microvolt") if "opp-microvolt" in props else None,
               "hardware_mask":words(dtb,path,"opp-supported-hw") if "opp-supported-hw" in props else None}
        tables.setdefault(parent,[]).append(state)
    consumers=[]
    for path,props in nodes.items():
        if "operating-points-v2" in props:
            refs=words(dtb,path,"operating-points-v2") or []
            consumers.append({"node":path,"compatible":strings(dtb,path,"compatible"),
                "status":strings(dtb,path,"status") or "okay",
                "tables":[handles.get(x,hex(x)) for x in refs]})
        if "operating-points" in props:
            cells=words(dtb,path,"operating-points") or []
            consumers.append({"node":path,"legacy_khz_uv":list(zip(cells[::2],cells[1::2]))})
    return {"board":board,"opp_tables":tables,"consumers":consumers,
            "thermal_nodes":[p for p in nodes if p.startswith("/thermal-zones/")],
            "limitations":["Device tree declarations are not runtime-validated clocks.",
              "Hardware masks, firmware and driver limits may restrict states.",
              "No voltage or frequency changes made. No experimental OPP enabled."]}

def choose(available, ceiling, profile, thermal_ok):
    """Return a candidate ceiling only; never write sysfs."""
    if profile not in ("balanced","performance"): raise ValueError("unknown profile")
    if not thermal_ok: return None
    valid=sorted(set(x for x in available if type(x) is int and 0<x<=ceiling))
    if not valid: return None
    return valid[-1] if profile=="performance" else valid[(len(valid)-1)//2]

class Tests(unittest.TestCase):
    def test_hz(self): self.assertEqual(hz([0,1844000000]),1844000000)
    def test_invalid_cells(self): self.assertIsNone(hz([1]))
    def test_ceiling(self): self.assertEqual(choose([1200,1800,1844,2000],1800,"performance",True),1800)
    def test_thermal(self): self.assertIsNone(choose([1200,1800],1800,"performance",False))
    def test_empty(self): self.assertIsNone(choose([],1800,"balanced",True))
    def test_balance(self): self.assertEqual(choose([1200,1512,1800],1800,"balanced",True),1512)
    def test_unknown(self):
        with self.assertRaises(ValueError): choose([1800],1800,"experimental",True)

if __name__=="__main__":
    p=argparse.ArgumentParser()
    p.add_argument("--self-test",action="store_true")
    p.add_argument("--dtb",type=pathlib.Path)
    p.add_argument("--out",type=pathlib.Path)
    a=p.parse_args()
    if a.self_test:
        unittest.main(argv=["audit_clock"],verbosity=2)
    else:
        if not a.dtb or not a.out: p.error("--dtb and --out required")
        result=inventory(a.dtb)
        a.out.write_text(json.dumps(result,indent=2)+"\n")
        print(json.dumps(result,indent=2))
