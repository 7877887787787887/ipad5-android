#!/usr/bin/env python3
"""Audit the official reference GSI without mounting or executing its contents."""
import argparse
import hashlib
import json
import pathlib
import re
import shutil
import struct
import subprocess
import tempfile
import zipfile

EXPECTED = "f9b599ff32b5bb574575b8a33ee17ff1d3354c9482b72fb10e2d13c16c0e12bb"
MAX_IMAGE = 8 * 1024**3
def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024*1024), b""):
            h.update(chunk)
    return h.hexdigest()

def elf_info(path):
    with open(path,"rb") as f:
        head=f.read(64)
        if len(head)!=64 or head[:6]!=b"\x7fELF\x02\x01":
            raise ValueError("Not little-endian ELF64")
        machine=struct.unpack_from("<H",head,18)[0]
        off=struct.unpack_from("<Q",head,32)[0]
        size,count=struct.unpack_from("<HH",head,54)
        if size!=56 or count>4096 or off+size*count>path.stat().st_size:
            raise ValueError("Invalid ELF program header table")
        f.seek(off)
        loads=[]
        for _ in range(count):
            p=struct.unpack("<IIQQQQQQ",f.read(size))
            if p[0]==1:
                loads.append({"alignment":p[7],"offset":p[2],"vaddr":p[3]})
        return {"machine":machine,"aarch64":machine==183,"load_segments":len(loads),
                "aligned_16k":bool(loads) and all(p["alignment"]>=16384 and
                   (p["offset"]-p["vaddr"])%16384==0 for p in loads)}

def main():
    p=argparse.ArgumentParser()
    p.add_argument("archive",type=pathlib.Path)
    p.add_argument("report",type=pathlib.Path)
    args=p.parse_args()
    h=digest(args.archive)
    if h!=EXPECTED: raise SystemExit("Reference archive SHA-256 mismatch")
    report={"archive_sha256":h,"boot_tested":False,"hardware_tested":False,"elf":{}}
    with tempfile.TemporaryDirectory() as td:
        work=pathlib.Path(td)
        raw=work/"system.img"
        with zipfile.ZipFile(args.archive) as z:
            entries=[i for i in z.infolist() if pathlib.PurePosixPath(i.filename).name=="system.img"]
            if len(entries)!=1 or not 0<entries[0].file_size<=MAX_IMAGE:
                raise ValueError("Missing, ambiguous or oversized system.img")
            report["archive_members"]=[{"name":i.filename,"bytes":i.file_size} for i in z.infolist()]
            with z.open(entries[0]) as source, raw.open("wb") as target:
                shutil.copyfileobj(source,target)
        with raw.open("rb") as f:
            header=f.read(4096)
        if header[:4]==b"\x3a\xff\x26\xed":
            major,minor,fh,ch,block,blocks=struct.unpack_from("<HHHHII",header,4)
            if major!=1 or not 0<block*blocks<=MAX_IMAGE:
                raise ValueError("Sparse image exceeds supported size")
            expanded=work/"expanded.img"
            subprocess.run(["simg2img",str(raw),str(expanded)],check=True)
            raw=expanded
        with raw.open("rb") as f:
            f.seek(1080)
            if f.read(2)!=b"\x53\xef": raise ValueError("Expected EXT4 system image")
        report["filesystem"]="ext4"
        report["system_bytes"]=raw.stat().st_size
        for name,paths in {
            "init":["/init","/system/bin/init","/bin/init"],
            "app_process64":["/system/bin/app_process64","/bin/app_process64"],
            "linker64":["/system/bin/linker64","/bin/linker64"],
            "surfaceflinger":["/system/bin/surfaceflinger","/bin/surfaceflinger"],
        }.items():
            output=work/name
            result={"status":"not found or not a regular ELF"}
            for src in paths:
                output.unlink(missing_ok=True)
                # debugfs defaults to read-only; no mount, no write option.
                subprocess.run(["debugfs","-R",f"dump {src} {output}",str(raw)],
                               capture_output=True,text=True,check=False)
                if output.is_file() and output.stat().st_size:
                    try:
                        result={"path":src,**elf_info(output)}
                        break
                    except ValueError:
                        pass
            report["elf"][name]=result
        report["blockers"]=["GSI expects Android vendor interfaces and boot ramdisk",
           "J72s hardware HALs and SELinux policy are unimplemented",
           "No verified independent boot mechanism",
           "Full system image exceeds practical RAM-only storage on a 2 GB device"]
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(report,indent=2))
if __name__=="__main__":
    main()
