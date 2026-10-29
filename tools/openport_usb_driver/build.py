"""Build/test the separate reviewed USB lifecycle derivative. No adapter access.
Requires GCC/MinGW and an extracted official libusb 1.0.29 Windows archive.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,os,shutil
root=Path(__file__).resolve().parent
parser=argparse.ArgumentParser()
parser.add_argument('--libusb',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--cc',default='gcc')
parser.add_argument('--native-mocks',action='store_true',help='Execute fake-USB tests on this host; never the real DLL')
args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads((root/'source-revision.json').read_text())
for name,expected in manifest['source_sha256'].items():
    if sha(root/name)!=expected:raise SystemExit('Reviewed source hash mismatch: '+name)
dep=args.libusb.resolve()
lib=dep/'MinGW64/dll/libusb-1.0.dll'
if sha(lib)!=manifest['libusb_dll_sha256']:raise SystemExit('Unexpected libusb DLL')
include=dep/'include'
flags=['-std=gnu11','-O2','-Wall','-Wextra','-Wno-pointer-sign']
command=[args.cc]+flags+['-shared','-I'+str(include),str(root/'j2534/j2534.c'),str(root/'j2534.def'),str(dep/'MinGW64/static/libusb-1.0.dll.a'),'-Wl,--exclude-all-symbols,--no-insert-timestamp','-static-libgcc','-o',str(out/'j2534.dll')]
subprocess.run(command,check=True)
subprocess.run([args.cc,'-std=c11','-c',str(root/'tests/abi-x64.c'),'-o',str(out/'abi-x64.o')],check=True)
(out/'DRIVER_SHA256.txt').write_text(sha(out/'j2534.dll')+'\n')
shutil.copy2(lib,out/'libusb-1.0.dll');shutil.copy2(root/'LICENSE',out/'LICENSE-driver')
manifest['build_command']=command;manifest['compiler']=subprocess.check_output([args.cc,'--version'],text=True).splitlines()[0]
manifest['j2534_dll_sha256']=sha(out/'j2534.dll');manifest['physical_test_performed']=False
results=[]
if args.native_mocks:
    exe=out/('fake-usb-tests.exe' if os.name=='nt' else 'fake-usb-tests')
    subprocess.run([args.cc,'-std=gnu11','-O1','-ffunction-sections','-fdata-sections','-Wno-pointer-sign','-I'+str(include),str(root/'tests/mock_connection.c'),'-Wl,--gc-sections','-o',str(exe)],check=True)
    for case in ['null-error','copy-error','max-error','0','1','2','3','4','5','6','7','8','9','10','11','13','close-success','close-timeout','close-release','close-both','ownership','log-visible']:
        arguments=[str(exe),case] if case!='log-visible' else [str(exe),'2',str(out/'mock-failure.log')]
        if case=='log-visible':(out/'mock-failure.log').unlink(missing_ok=True)
        result=subprocess.run(arguments,capture_output=True,text=True,timeout=8)
        results.append({'case':case,'exit':result.returncode,'stdout':result.stdout,'stderr':result.stderr})
        if result.returncode:raise SystemExit(json.dumps(results[-1]))
    (out/'fake-usb-results.json').write_text(json.dumps(results,indent=2))
manifest['native_mock_cases_passed']=len(results)
(out/'build-manifest.json').write_text(json.dumps(manifest,indent=2))
print('Reviewed driver artifact SHA-256:',manifest['j2534_dll_sha256'])
