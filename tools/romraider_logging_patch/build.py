"""Build a separate RomRaider candidate; never edit the supplied release/install.
Only the exact pinned JAR and normalized source inputs are accepted.
"""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,os,zipfile
p=argparse.ArgumentParser();p.add_argument('--romraider',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--javac',default='javac');p.add_argument('--java',default='java');a=p.parse_args()
root=Path(__file__).resolve().parent;source=a.romraider.resolve();out=a.output.resolve()
if out==source or source in out.parents:raise SystemExit('Candidate output must be outside the original release')
m=json.loads((root/'source-manifest.json').read_text());sha=lambda b:hashlib.sha256(b).hexdigest()
if sha((source/'RomRaider.jar').read_bytes())!=m['original_jar_sha256']:raise SystemExit('RomRaider JAR is not the reviewed release')
patch=(root/'romraider-1.1.0-logging.patch').read_text().splitlines(True);i=0;changed=[]
while i<len(patch):
    if not patch[i].startswith('--- a/'):raise SystemExit('Unexpected patch format')
    name=patch[i][6:].strip();i+=2
    original=(source/'src/main/java'/name).read_text()
    if sha(original.encode())!=m['normalized_source_sha256'][name]:raise SystemExit('Source mismatch: '+name)
    lines=original.splitlines(True);result=[];pos=0
    while i<len(patch) and patch[i].startswith('@@ '):
        h=re.match(r'@@ -(\d+)(?:,\d+)? \+\d+(?:,\d+)? @@',patch[i]);start=int(h[1])-1
        result+=lines[pos:start];pos=start;i+=1
        while i<len(patch) and not patch[i].startswith(('@@ ','--- a/')):
            line=patch[i];i+=1
            if line[0] in ' -':
                if pos>=len(lines) or lines[pos]!=line[1:]:raise SystemExit('Patch context mismatch: '+name)
                pos+=1
            if line[0] in ' +':result.append(line[1:])
    result+=lines[pos:];target=out/'src'/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(''.join(result));changed.append(target)
classes=out/'classes';classes.mkdir(parents=True,exist_ok=True)
classpath=os.pathsep.join([str(source/'RomRaider.jar')]+[str(x) for x in sorted((source/'lib/common').glob('*.jar'))])
subprocess.run([a.javac,'--release','8','-encoding','UTF-8','-cp',classpath,'-d',str(classes)]+[str(x) for x in changed],check=True)
tests=out/'test-classes';tests.mkdir(exist_ok=True)
subprocess.run([a.javac,'--release','8','-encoding','UTF-8','-cp',str(classes)+os.pathsep+classpath,'-d',str(tests),str(root/'LoggingIntegrityTest.java')],check=True)
result=subprocess.run([a.java,'-ea','-cp',os.pathsep.join([str(tests),str(classes),classpath]),'LoggingIntegrityTest'],capture_output=True,text=True,timeout=30)
(out/'java-test-results.txt').write_text(result.stdout+result.stderr)
if result.returncode:raise SystemExit(result.stdout+result.stderr)
print(result.stdout)
# Retain every unmodified release entry; overlay only the patched compiled classes.
jar=out/'RomRaider-logging-integrity-candidate.jar'
with zipfile.ZipFile(source/'RomRaider.jar') as original,zipfile.ZipFile(jar,'w',zipfile.ZIP_DEFLATED) as target:
    updates={x.relative_to(classes).as_posix():x for x in classes.rglob('*.class')}
    for entry in original.infolist():
        if entry.filename not in updates:target.writestr(entry,original.read(entry.filename))
    for name,path in updates.items():target.write(path,name)
m['patched_jar_sha256']=sha(jar.read_bytes());m['patch_sha256']=sha((root/'romraider-1.1.0-logging.patch').read_bytes());m['patched_source_sha256']={str(x.relative_to(out/'src')):sha(x.read_bytes()) for x in changed};m['physical_test_performed']=False
(out/'build-manifest.json').write_text(json.dumps(m,indent=2)+'\n');print('Candidate SHA-256:',m['patched_jar_sha256'])
