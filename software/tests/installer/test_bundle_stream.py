import fcntl
import hashlib
import os
import struct
import unittest
from unittest.mock import patch


class BundleStreamTests(unittest.TestCase):
    def test_fd_records_stream_in_bounded_chunks_and_preserve_wire_bytes(self):
        import installer_snapshot as module
        self.assertTrue(hasattr(module, 'sealed_bundle'), 'streaming bundle writer missing')
        data = b'z' * (3 * 1024 * 1024 + 17)
        source = module.sealed(data, 'source')
        real = os.pread
        sizes = []
        def bounded(fd, count, offset):
            sizes.append(count)
            self.assertLessEqual(count, 1024 * 1024)
            return real(fd, count, offset)
        try:
            with patch.object(os, 'pread', bounded):
                fd, digest = module.sealed_bundle([('source', source, len(data), 0o444)], 'test')
            try:
                expected = b'W4B1' + struct.pack('<I', 1) + struct.pack('<IIQ', 6, 0o444, len(data)) + b'source' + data
                self.assertEqual(real(fd, len(expected), 0), expected)
                self.assertEqual(digest, hashlib.sha256(expected).hexdigest())
                self.assertEqual(fcntl.fcntl(fd, 1034) & 15, 15)
                self.assertEqual(os.lseek(fd, 0, os.SEEK_CUR), 0)
                self.assertGreater(len(sizes), 1)
            finally:
                os.close(fd)
        finally:
            os.close(source)
