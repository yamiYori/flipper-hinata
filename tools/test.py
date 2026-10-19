"""Build and execute the actual portable C core with UBSan. No USB device IO."""
from pathlib import Path
import argparse,os,subprocess,sys,json,hashlib,atexit
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument("--firmware",choices=["unleashed","official"],default="unleashed")
args=parser.parse_args()
os.chdir(ROOT)
for d in ['build','.cache/tmp','.cache/zig']:(ROOT/d).mkdir(parents=True,exist_ok=True)
env=dict(os.environ,TEMP=str(ROOT/'.cache/tmp'),TMP=str(ROOT/'.cache/tmp'),ZIG_GLOBAL_CACHE_DIR=str(ROOT/'.cache/zig'))
python=ROOT/'.venv/Scripts/python.exe'
results=[]
def run(command, timeout=120):
    print('Running: ' + ' '.join(str(x).replace(str(ROOT), 'PROJECT') for x in command), flush=True)
    result=subprocess.run(command,env=env,capture_output=True,text=True,timeout=timeout)
    results.append(dict(command=[str(x).replace(str(ROOT),'PROJECT') for x in command],exit_code=result.returncode,stdout=result.stdout,stderr=result.stderr))
    print(result.stdout,end='');print(result.stderr,end='')
    result.check_returncode()
def save_results():
    hashes={str(p.relative_to(ROOT)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest()
            for folder in ['src','tests','tools'] for p in sorted((ROOT/folder).rglob('*'))
            if p.is_file() and p.suffix in ['.c','.h','.py']}
    (ROOT/'build/test-results.json').write_text(json.dumps(dict(hardware_verified=False,numeric_mac_verified=False,
        compiler='ziglang 0.16.0 cc, UBSan, -Wall -Wextra -Werror',source_sha256=hashes,results=results),indent=2)+'\n')
atexit.register(save_results)
# A cold Windows Zig cache builds its C runtime before compiling our fixtures.
# Allow that compilation up to 10 minutes; test execution keeps the 120-second limit.
base=[str(python),'-m','ziglang','cc','-std=c11','-Wall','-Wextra','-Werror','-g','-O1','-fsanitize=undefined','-fno-sanitize-recover=all']
for name,extra in [('hinata',['src/core/hinata.c']),('virtual',[])]:
    out=ROOT/f'build/test_{name}.exe'
    run(base+['src/core/sg_frame.c','src/core/sg_reader.c','src/core/sg_virtual.c']+extra+[f'tests/test_{name}.c','-o',str(out)], timeout=600)
    run([str(out)])
sdk=ROOT/('.ufbt-official' if args.firmware=='official' else '.ufbt')/'current/sdk_headers/f7_sdk'
out=ROOT/'build/test_transport.exe'
run(base+['-DSTM32WB55xx','-Itests/stubs','-I'+str(sdk/'targets/furi_hal_include'),'-I'+str(sdk/'lib/libusb_stm32/inc'),
    'src/usb_transport.c','tests/test_transport.c','-o',str(out)], timeout=600)
run([str(out)])
run([str(python),'-m','unittest','discover','-s','tests','-p','test_*.py','-v'])
