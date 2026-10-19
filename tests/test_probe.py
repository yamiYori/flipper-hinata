import unittest,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from probe_hinata import frame,parse_frame
class ProbeTests(unittest.TestCase):
    def test_nxp_firmware_request(self):
        self.assertEqual(frame(2),bytes.fromhex('0000ff02fed4022a00'))
    def test_nxp_final_and_padded_ack(self):
        self.assertEqual(parse_frame(bytes.fromhex('0000ff06fad50332010607e800'),2),bytes.fromhex('32010607'))
        self.assertIsNone(parse_frame(bytes.fromhex('0000ff00ff00')+bytes(56),2))
    def test_errors(self):
        for data in ['0000ff01ff7f8100','0000ff06fad50332010607e900','0000ff06fad50332010607e801','0000ff06fad50532010607e600']:
            with self.assertRaises(ValueError):parse_frame(bytes.fromhex(data),2)
    def test_single_report_bound(self):
        self.assertEqual(len(frame(0,bytes(53))),62)
        with self.assertRaises(ValueError):frame(0,bytes(54))
