import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import urllib.error

spec = importlib.util.spec_from_file_location('bootstrap', Path(__file__).resolve().parents[1] / 'tools/bootstrap.py')
bootstrap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bootstrap)

class BootstrapDownloadTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.payload = b'synthetic SDK archive'
        self.spec = dict(filename='sdk.zip', url='https://example.invalid/sdk.zip',
                         api_url='https://example.invalid/alternate', size=len(self.payload),
                         sha256=hashlib.sha256(self.payload).hexdigest())

    def test_public_download_and_verified_cache(self):
        with patch.object(bootstrap, 'ROOT', self.root), patch.object(bootstrap.urllib.request, 'urlopen', return_value=io.BytesIO(self.payload)) as fetch:
            dest = bootstrap.download(self.spec)
            self.assertEqual(dest.read_bytes(), self.payload)
            self.assertEqual(fetch.call_args.args[0].full_url, self.spec['url'])
            self.assertEqual(bootstrap.download(self.spec), dest)
            self.assertEqual(fetch.call_count, 1)

    def test_network_failure_uses_locked_alternate(self):
        error = urllib.error.HTTPError(self.spec['url'], 503, 'Unavailable', {}, None)
        with patch.object(bootstrap, 'ROOT', self.root), patch.object(bootstrap.urllib.request, 'urlopen', side_effect=[error, io.BytesIO(self.payload)]) as fetch:
            self.assertEqual(bootstrap.download(self.spec).read_bytes(), self.payload)
            self.assertEqual([c.args[0].full_url for c in fetch.call_args_list], [self.spec['url'], self.spec['api_url']])

    def test_bad_checksum_never_installed_or_retried(self):
        bad = b'x' * len(self.payload)
        with patch.object(bootstrap, 'ROOT', self.root), patch.object(bootstrap.urllib.request, 'urlopen', return_value=io.BytesIO(bad)) as fetch:
            with self.assertRaisesRegex(RuntimeError, 'SHA256 mismatch'):
                bootstrap.download(self.spec)
            self.assertFalse((self.root / '.research/sdk.zip').exists())
            self.assertEqual(fetch.call_count, 1)

if __name__ == '__main__':
    unittest.main()
