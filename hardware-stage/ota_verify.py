"""Offline OTA verification prototype. Never installs updates or writes partitions."""
import hashlib, json, pathlib, re, subprocess, tempfile, unittest
def verify(manifest, signature, trusted_key, payload, installed_version, kernel_abi):
    subprocess.run(["openssl","pkeyutl","-verify","-pubin","-inkey",str(trusted_key),
                    "-rawin","-in",str(manifest),"-sigfile",str(signature)],
                    check=True, capture_output=True)
    m=json.loads(manifest.read_text())
    if set(m)!={"board","version","kernel_abi","sha256","bytes"}: raise ValueError("manifest schema")
    if m["board"]!="j72s" or m["kernel_abi"]!=kernel_abi: raise ValueError("incompatible target")
    if type(m["version"]) is not int or m["version"]<=installed_version: raise ValueError("rollback")
    if type(m["bytes"]) is not int or not 0<m["bytes"]<=8*1024**3: raise ValueError("size")
    if not isinstance(m["sha256"],str) or not re.fullmatch("[0-9a-f]{64}",m["sha256"]): raise ValueError("digest")
    h=hashlib.sha256()
    with payload.open("rb") as f:
        for b in iter(lambda:f.read(1024*1024),b""): h.update(b)
    if payload.stat().st_size!=m["bytes"] or h.hexdigest()!=m["sha256"]: raise ValueError("payload integrity")
    return m
class Tests(unittest.TestCase):
    def setUp(self):
        self.t=tempfile.TemporaryDirectory(); self.addCleanup(self.t.cleanup)
        self.p=pathlib.Path(self.t.name)
        self.key=self.p/"test-only.pem"; self.pub=self.p/"public.pem"
        self.man=self.p/"manifest.json"; self.sig=self.p/"signature"; self.payload=self.p/"payload"
        self.payload.write_bytes(b"synthetic driver package")
        subprocess.run(["openssl","genpkey","-algorithm","ED25519","-out",str(self.key)],check=True,capture_output=True)
        subprocess.run(["openssl","pkey","-in",str(self.key),"-pubout","-out",str(self.pub)],check=True,capture_output=True)
        self.data={"board":"j72s","version":2,"kernel_abi":"test-abi","sha256":hashlib.sha256(self.payload.read_bytes()).hexdigest(),"bytes":self.payload.stat().st_size}
    def sign(self):
        self.man.write_text(json.dumps(self.data,sort_keys=True))
        subprocess.run(["openssl","pkeyutl","-sign","-inkey",str(self.key),"-rawin","-in",str(self.man),"-out",str(self.sig)],check=True,capture_output=True)
    def run_verify(self): return verify(self.man,self.sig,self.pub,self.payload,1,"test-abi")
    def test_valid(self):
        self.sign(); self.assertEqual(self.run_verify()["version"],2)
    def test_rollback(self):
        self.data["version"]=1; self.sign()
        with self.assertRaises(ValueError): self.run_verify()
    def test_wrong_board(self):
        self.data["board"]="j72t"; self.sign()
        with self.assertRaises(ValueError): self.run_verify()
    def test_wrong_abi(self):
        self.data["kernel_abi"]="other"; self.sign()
        with self.assertRaises(ValueError): self.run_verify()
    def test_payload_tamper(self):
        self.sign(); self.payload.write_bytes(b"bad")
        with self.assertRaises(ValueError): self.run_verify()
    def test_manifest_tamper(self):
        self.sign(); self.man.write_text("{}")
        with self.assertRaises(subprocess.CalledProcessError): self.run_verify()
if __name__=="__main__": unittest.main(verbosity=2)
