"""Read-only HID smoke/stress probe. Never changes game configuration or updates firmware.

No card IDs, keys, blocks, device serials or device paths are logged. Host handles
stay open for the full run, including while the user browses/selects cards.
"""
from __future__ import annotations
import argparse,json,time
VID,PID=0xf822,0x0148

def frame(command:int,payload:bytes=b'')->bytes:
    body=bytes([0xd4,command])+payload
    if len(body)>55:raise ValueError('PN532 request exceeds single HID report')
    return b'\0\0\xff'+bytes([len(body),(-len(body))&255])+body+bytes([(-sum(body))&255,0])

def parse_frame(packet:bytes,command:int)->bytes|None:
    if len(packet)<8 or packet[:3]!=b'\0\0\xff':raise ValueError('PN532 preamble')
    if packet[3:6]==b'\0\xff\0':return None
    n=packet[3]
    if (n+packet[4])&255 or n+7>len(packet):raise ValueError('PN532 length')
    if sum(packet[5:6+n])&255 or packet[6+n]:raise ValueError('PN532 checksum')
    if n==1 and packet[5]==0x7f:raise ValueError('Unsupported PN532 command/parameter')
    if n<2 or packet[5]!=0xd5 or packet[6]!=command+1:raise ValueError('Unexpected PN532 response')
    return packet[7:5+n]

class Reader:
    def __init__(self):
        import hid
        devices=[x for x in hid.enumerate(VID,PID)
                 if x.get('product_string') == 'HINATA USB experimental']
        reads=[x for x in devices if x['usage_page']==1]
        writes=[x for x in devices if x['usage_page']==6]
        if len(reads)!=1 or len(writes)!=1:
            raise RuntimeError(f'Expected one Flipper reader with pages 1/6; found {len(reads)}/{len(writes)}. Close other clients and connect only one.')
        self.rx=hid.device();self.tx=hid.device()
        try:
            self.rx.open_path(reads[0]['path']);self.tx.open_path(writes[0]['path'])
        except Exception:
            self.close();raise
        self.transactions=0
        # Drain only at initial open; no implicit handle reopening during tests.
        start=time.monotonic()
        while self.rx.read(64,50):
            if time.monotonic()-start>1:raise RuntimeError('Reader is busy with another host')
    def close(self):
        self.rx.close();self.tx.close()
    def request(self,command:int,payload:bytes=b'',pn_command:int|None=None)->bytes:
        wire=bytes([1,command])+payload
        if len(wire)>64:raise ValueError('HID request too large')
        if self.tx.write(wire.ljust(64,b'\0'))!=64:raise RuntimeError('Short HID write')
        deadline=time.monotonic()+1.5
        header=0x32 if command==1 else command
        while time.monotonic()<deadline:
            res=bytes(self.rx.read(64,200))
            if not res:continue
            if len(res)<2 or res[0]!=3 or res[1]!=header:raise RuntimeError('Unmatched HID response; stop and reopen after clearing state')
            if pn_command is not None:
                value=parse_frame(res[2:],pn_command)
                if value is None:continue
            else:value=res[1:]
            self.transactions+=1;return value
        raise TimeoutError('HID response timeout; handles were not reopened')
    def pn(self,command:int,payload:bytes=b'')->bytes:
        return self.request(0xe2,frame(command,payload),command)

def run(seconds:float,poll:bool):
    reader=Reader();durations=[];present=absent=0
    try:
        version=reader.request(1)[:10].decode('ascii')
        if version!='2024090500':raise RuntimeError('Unexpected compatibility level')
        fw=reader.pn(2)
        if fw!=bytes([0x32,1,0,5]):raise RuntimeError('Unexpected software bridge identity')
        print('HID handshake and ACK/final response: PASS',flush=True)
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            start=time.monotonic()
            reader.request(1)
            if poll:
                target=reader.pn(0x4a,bytes([1,1,0,255,255,1,0]))
                if target==b'\0':absent+=1
                elif len(target)==22 and target[:4]==bytes([1,1,20,1]):present+=1
                else:raise ValueError('Malformed FeliCa target response')
            durations.append((time.monotonic()-start)*1000)
            time.sleep(.15)
        print(json.dumps({'result':'PASS','handle_reopens':0,'transactions':reader.transactions,
            'poll_present':present,'poll_absent':absent,'max_round_ms':round(max(durations,default=0),1)}))
    finally:reader.close()

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--seconds',type=float,default=10,help='Keep the same handles open (default 10)')
    ap.add_argument('--poll',action='store_true',help='Also perform FeliCa 212 discovery (no card writes)')
    args=ap.parse_args()
    if not 0<=args.seconds<=3600:ap.error('seconds must be in 0..3600')
    run(args.seconds,args.poll)
