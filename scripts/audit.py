"""Read-only Kconfig evidence checker."""
import argparse, json, re
from pathlib import Path

def parse(text):
    data = {}
    for line in text.splitlines():
        m = re.fullmatch(r'(CONFIG_[A-Z0-9_]+)=(.*)', line.strip())
        if m:
            data[m[1]] = m[2]
        else:
            m = re.fullmatch(r'# (CONFIG_[A-Z0-9_]+) is not set', line.strip())
            if m: data[m[1]] = 'n'
    return data

CORE = ['ARM64_16K_PAGES','ANDROID_BINDER_IPC','SECURITY_SELINUX','CGROUPS','MEMCG','TMPFS','BLK_DEV_INITRD','SECCOMP_FILTER','BPF_SYSCALL']
CANDIDATE = ['ANDROID_BINDERFS','CGROUP_BPF','EROFS_FS','DM_VERITY','F2FS_FS',
             'DMABUF_HEAPS','DMABUF_HEAPS_SYSTEM']

def audit(cfg):
    def state(items): return {k: cfg.get('CONFIG_'+k, 'unknown') for k in items}
    core=state(CORE)
    return {'core_subset':core,'additional_image_dependent_options':state(CANDIDATE), 'core_subset_pass':all(v=='y' for v in core.values()), 'boot_readiness':'UNVERIFIED — this is a configuration subset, not a boot test'}

if __name__ == '__main__':
    p=argparse.ArgumentParser(); p.add_argument('config',type=Path); a=p.parse_args(); result=audit(parse(a.config.read_text())); print(json.dumps(result,indent=2)); raise SystemExit(0 if result['core_subset_pass'] else 1)
