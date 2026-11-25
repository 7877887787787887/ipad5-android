import unittest
from audit import parse,audit,CORE
class AuditTests(unittest.TestCase):
    def test_observed(self):
        from pathlib import Path
        a=audit(parse(Path(__file__).with_name('observed-orchard.config').read_text()))
        self.assertFalse(a['core_subset_pass']); self.assertEqual(a['core_subset']['SECURITY_SELINUX'],'n')
    def test_missing(self): self.assertEqual(audit({})['core_subset']['CGROUPS'],'unknown')
    def test_subset(self):
        a=audit({'CONFIG_'+k:'y' for k in CORE}); self.assertTrue(a['core_subset_pass']); self.assertIn('UNVERIFIED',a['boot_readiness'])
    def test_parse(self): self.assertEqual(parse('# CONFIG_EXAMPLE is not set\nCONFIG_X=m'),{'CONFIG_EXAMPLE':'n','CONFIG_X':'m'})
