import unittest
from installer_ota import make_container
from installer_ota_verify import verify_container
from zlib import crc32

class MetadataBindingTests(unittest.TestCase):
    def test_rejects_changed_version_even_with_valid_crc(self):
        # Build a minimal valid ESP app; bind actual metadata independently.
        import struct,hashlib
        app=bytearray(24);app[0]=0xe9;app[1]=1;app[23]=1
        app+=struct.pack('<II',0x3ffb0000,4)+b'abcd'
        checksum=0xef
        for b in b'abcd':checksum^=b
        app+=bytes((-len(app)-1)%16)+bytes([checksum]);app+=hashlib.sha256(app).digest()
        metadata={'firmware_info_product_id':'warp4','firmware_info_product_name':'WARP4 Charger','version':[2,13,5,255],'build_timestamp':123456}
        ota=make_container(bytes(app),metadata)
        self.assertIsInstance(verify_container(ota,bytes(app),expected_metadata=metadata),dict)
        for offset in (0xc000+69,0xc000+72,0xc000+76):
            bad=bytearray(ota);bad[offset]^=1
            bad[0xc000+4092:0xd000]=crc32(bad[0xc000:0xc000+4092]).to_bytes(4,'little')
            with self.assertRaisesRegex(ValueError,'metadata'):
                verify_container(bytes(bad),bytes(app),expected_metadata=metadata)
