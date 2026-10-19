import unittest,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def arrays():
    s=(ROOT/'src/usb_descriptors.h').read_text()
    def parse(name,report=None):
        raw=re.search(name+r'\[\] = \{(.*?)\};',s,re.S)[1]
        raw=raw.replace('sizeof(hinata_report_descriptor)',str(len(report or [])))
        return bytes(int(x.strip(),0) for x in raw.split(',') if x.strip())
    rpt=parse('hinata_report_descriptor')
    return rpt,parse('hinata_config_descriptor',rpt)
class DescriptorTests(unittest.TestCase):
    def test_dll_discovery_identity(self):
        # Local DLL SHA ab75d94... explicitly filters F822:0148 / F822:0147.
        source=(ROOT/'src/usb_descriptors.h').read_text()
        self.assertRegex(source,r'#define HINATA_VID 0xf822')
        self.assertRegex(source,r'#define HINATA_PID 0x0148')
        self.assertIn('HINATA USB experimental',(ROOT/'src/usb_transport.c').read_text())

    def test_configuration_and_endpoints(self):
        rpt,cfg=arrays(); self.assertEqual(int.from_bytes(cfg[2:4],'little'),len(cfg))
        offset=0;interfaces=[];endpoints=[];hid=None
        while offset<len(cfg):
            n,t=cfg[offset:offset+2]; self.assertGreaterEqual(n,2)
            item=cfg[offset:offset+n]; self.assertEqual(len(item),n)
            if t==4:interfaces.append((item[2],item[4],item[5]))
            if t==5:endpoints.append((item[2],item[3],int.from_bytes(item[4:6],'little')))
            if t==0x21:hid=item;self.assertEqual(offset,84)
            offset+=n
        self.assertEqual(offset,len(cfg)); self.assertEqual(interfaces,[(0,1,2),(1,2,10),(2,2,3)])
        self.assertEqual(set(endpoints),{(0x83,3,8),(2,2,64),(0x82,2,64),(0x81,3,64),(1,3,64)})
        self.assertEqual(int.from_bytes(hid[7:9],'little'),len(rpt))
    def test_report_ids_do_not_cross_collections(self):
        data,_=arrays(); i=0;depth=0;collection=-1;rid=0;owners={}
        while i<len(data):
            tag=data[i];i+=1;n=(0,1,2,4)[tag&3]
            value=int.from_bytes(data[i:i+n],'little');i+=n;item=tag&0xfc
            if item==0xa0:
                if depth==0:collection+=1
                depth+=1
            elif item==0xc0:depth-=1
            elif item==0x84:
                self.assertNotEqual(value,0);rid=value
            elif item in (0x80,0x90,0xb0):
                self.assertGreater(depth,0)
                self.assertNotEqual(rid,0)
                self.assertEqual(owners.setdefault(rid,collection),collection,
                                 f'Report ID {rid} crosses top-level collections')

    def test_hid_collections_and_report_lengths(self):
        data,_=arrays(); i=0;page=rid=width=count=0;top=None;depth=0;totals={};pages=[]
        while i<len(data):
            tag=data[i];i+=1;n=(0,1,2,4)[tag&3];v=int.from_bytes(data[i:i+n],'little');i+=n
            item=tag&0xfc
            if item==0x04:page=v
            if item==0x84:rid=v
            if item==0x74:width=v
            if item==0x94:count=v
            if item==0xa0:
                if depth==0:top=page;pages.append(page)
                depth+=1
            if item==0xc0:depth-=1
            if item in (0x80,0x90):
                k=(top,rid,item);totals[k]=totals.get(k,0)+width*count
        self.assertEqual(depth,0);self.assertEqual(pages,[1,6,0xffca])
        self.assertEqual(totals,{(1,3,0x80):504,(6,1,0x90):504,(0xffca,2,0x80):64})
        self.assertNotIn(7,pages) # keyboard usage page must never be emitted
if __name__=='__main__':unittest.main()
