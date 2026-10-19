"""Run unmodified public HINATA Go protocol classes against our native C core.

Usage: python tools/test_public_client.py --dart PATH --source HINATA_GO_CHECKOUT
The upstream files are research inputs, never copied into the release package.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,os
ROOT=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser();ap.add_argument('--dart',required=True);ap.add_argument('--source',required=True)
ap.add_argument('--core',help='Optional native harness for before/after regression comparison')
args=ap.parse_args();up=Path(args.source).resolve()/'plugins/hinata_nfc.dart/lib/src'
files=['protocol/pn532.dart','protocol/sega_protocol.dart','protocol/base.dart','nfc/target.dart']
hashes={f:hashlib.sha256((up/f).read_bytes()).hexdigest() for f in files}
expected=json.loads((ROOT/'sources-lock.json').read_text())['public_client']
if hashes!=expected['sha256']:
    raise SystemExit('Upstream source differs from sources-lock.json; no compatibility result recorded')
script='''
import 'dart:async';
import 'dart:convert';
import 'dart:io';
import '__PN__';
import '__SG__';
void check(bool value,String message) { if(!value) throw StateError(message); }
Future<void> main(List<String> args) async {
  final process=await Process.start(args[0],['--serve']);
  final lines=StreamIterator(process.stdout.transform(utf8.decoder).transform(const LineSplitter()));
  process.stderr.transform(utf8.decoder).listen(stderr.write);
  Future<String> line() async {
    if(!await lines.moveNext().timeout(const Duration(seconds:3))) throw StateError('C harness ended');
    return lines.current;
  }
  Future<List<List<int>>> textExchange(String command) async {
    process.stdin.writeln(command); await process.stdin.flush();
    final count=int.parse(await line());
    final result=<List<int>>[];
    for(var i=0;i<count;i++) {final s=await line();result.add([for(var j=0;j<s.length;j+=2)int.parse(s.substring(j,j+2),radix:16)]);}
    return result;
  }
  Future<List<List<int>>> exchange(List<int> packet) => textExchange(packet.map((b)=>(b&255).toRadixString(16).padLeft(2,'0')).join());
  var queue=<List<int>>[];
  var acks=0;
  final pn=Pn532Api((p) async { queue=(await exchange([1,0xe2,...p])).map((r)=>r.sublist(2)).toList(); },
    ({timeout}) async {if(queue.isEmpty)throw TimeoutException('No more C response frames');final r=queue.removeAt(0);if(r[3]==0)acks++;return r;});
  final sg=SegaApi((p) async {queue=(await exchange([1,0xe0,...p])).map((r)=>r.sublist(1)).toList();},
    ({timeout})async=>queue.removeAt(0));
  try {
    final fw=await exchange([1,1]);
    check(fw.single[0]==3,'unique input report ID');
    check(ascii.decode(fw.single.sublist(1,11))=='2024090500','timestamp');
    check(await pn.getFirmwareVersion()==0x3201,'PN software version');
    final initial=pn.genFelicaPollInitialData(0xffff,1);
    var tags=await pn.inListPassiveTarget(1,1,initial);
    check(tags.length==1 && tags.single.systemCodes!.single==0x88b4,'public FeliCa target parser');
    final id=tags.single.id;
    final read=[16,6,...id,1,11,0,1,0x80,0];
    final response=await pn.inDataExchange(1,read[0],read.sublist(1));
    check(response.length==30 && response[0]==0 && response[1]==29 && response[29]==15,'public read response');
    final rc=[32,8,...id,1,9,0,1,0x80,0x80,...List<int>.generate(16,(i)=>i+1)];
    check((await pn.inDataExchange(1,rc[0],rc.sublist(1)))[0]==0,'RC write');
    final mac=[18,6,...id,1,11,0,2,0x80,0,0x80,0x91];
    final mr=await pn.inDataExchange(1,mac[0],mac.sublist(1));
    check(mr.length==46 && mr[30]==(1^0xa5),'dynamic MAC callback routing');
    await pn.inRelease(1);
    check((await pn.inDataExchange(1,mac[0],mac.sublist(1)))[0]==0x2b,'release invalidates session');
    check(await sg.startPoll()==0,'public E0 start');
    check((await sg.detectCard())!.pmm![1]==0xf1,'public E0 FeliCa card-type mapping');
    for(var i=0;i<25;i++) {
      await textExchange('remove');
      check((await pn.inListPassiveTarget(1,1,initial)).isEmpty,'remove');
      await textExchange('present');
      check((await pn.inListPassiveTarget(1,1,initial)).length==1,'present');
      check((await exchange([1,1])).single[1]==0x32,'same connection handshake');
    }
    // Reproduce the web application's complete empty-card round, including
    // the Type A fallback that previously threw instead of reporting noTarget.
    await textExchange('remove');
    check((await pn.inListPassiveTarget(1,1,initial)).isEmpty,'web empty FeliCa');
    await pn.inRelease(1);
    for(final profile in [[0x29,3,0x11],[0x49,11,0x0c],[0x59,15,0x3f],[0x69,15,0x2b]]) {
      await pn.setTypeARfPower(rfCfg:profile[0],cwGsNOn:profile[1],cwGsP:profile[2]);
      check((await pn.inListPassiveTarget(0,1,[])).isEmpty,'web empty Type A');
    }
    await pn.setTypeARfPower();
    await textExchange('classic');
    final classic=(await pn.inListPassiveTarget(0,1,[])).single;
    final auth=await pn.inDataExchange(1,0x61,[2,...ascii.encode('ABCDEF'),...classic.id]);
    check(auth.single==0,'public Classic auth');
    final block=await pn.mifareClassicReadBlock(1,2);
    check(block!=null && block.length==16 && block.first==0xa0 && block.last==0xaf,'public Classic read');
    await textExchange('remove');
    check(await pn.mifareClassicReadBlock(1,2)==null,'removed Classic invalidates read');
    var rejected=false;
    try {await pn.setTypeARfPower(rfCfg:0xff);} catch(_) {rejected=true;}
    check(rejected,'unknown PN532 analog settings must fail');
    stdout.writeln('PUBLIC DART CLIENT PASS: firmware, ACK/final, FeliCa poll/read, RC callback, E0, 25 remove/present cycles, complete web empty-card fallback, Classic auth/read, rejected unknown analog settings; ACKs=$acks');
  } finally {
    await process.stdin.close();await process.exitCode;await lines.cancel();
  }
}
'''
script=script.replace('__PN__',(up/'protocol/pn532.dart').as_uri()).replace('__SG__',(up/'protocol/sega_protocol.dart').as_uri())
target=ROOT/'build/public_client.dart';target.write_text(script,encoding='utf-8')
env=dict(os.environ,TEMP=str(ROOT/'.cache/tmp'),TMP=str(ROOT/'.cache/tmp'),DART_SUPPRESS_ANALYTICS='true')
core=Path(args.core).resolve() if args.core else ROOT/'build/test_hinata.exe'
result=subprocess.run([str(Path(args.dart).resolve()),str(target),str(core)],env=env,capture_output=True,text=True,timeout=60)
print(result.stdout);print(result.stderr)
(ROOT/'build/public-client-test.json').write_text(json.dumps(dict(upstream_commit='66ac7f6ab2f09272bbca0c5d9db6ad7a4808f0a1',source_sha256=hashes,core_sha256=hashlib.sha256(core.read_bytes()).hexdigest(),exit_code=result.returncode,stdout=result.stdout,stderr=result.stderr),indent=2))
raise SystemExit(result.returncode)
