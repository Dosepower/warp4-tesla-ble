"""SDK header aliases must resolve, not be ignored or trusted by name."""
import unittest
from installer_sdk_verify import verify_config_header

class HeaderTests(unittest.TestCase):
    def test_idf_generated_aliases_match_real_config_values(self):
        verify_config_header({'CONFIG_NEW':'y','CONFIG_OLD':'y','CONFIG_COUNT':'3'},
                             '#define CONFIG_NEW 1\n#define CONFIG_OLD CONFIG_NEW\n#define CONFIG_COUNT 3\n')
    def test_pinned_builder_metadata_is_not_a_config_override(self):
        verify_config_header({'CONFIG_NEW':'y'}, '#define CONFIG_NEW 1\n#define CONFIG_ARDUINO_IDF_BRANCH ""\n#define CONFIG_ARDUINO_IDF_COMMIT "3ea97e7591"\n')
    def test_previous_sdk_identity_is_rejected(self):
        # Old ESP-IDF 5.5.5-era custom pin must not satisfy the new header gate.
        with self.assertRaises(ValueError):
            verify_config_header({'CONFIG_NEW':'y'}, '#define CONFIG_NEW 1\n#define CONFIG_ARDUINO_IDF_BRANCH ""\n#define CONFIG_ARDUINO_IDF_COMMIT "d2a01b4ca9"\n')
    def test_mutations_fail_closed(self):
        cfg={'CONFIG_NEW':'y','CONFIG_OLD':'y'}
        good='#define CONFIG_NEW 1\n#define CONFIG_OLD CONFIG_NEW\n'
        for bad in (good.replace('CONFIG_OLD CONFIG_NEW','CONFIG_OLD 0'),
                    good.replace('CONFIG_OLD CONFIG_NEW','CONFIG_OLD CONFIG_MISSING'),
                    '#define CONFIG_NEW CONFIG_OLD\n#define CONFIG_OLD CONFIG_NEW\n',
                    good+'#define CONFIG_FOREIGN 1\n',
                    good+'#define CONFIG_ARDUINO_IDF_COMMIT "0000000000"\n#define CONFIG_ARDUINO_IDF_BRANCH ""\n',
                    good+'#define CONFIG_ARDUINO_IDF_COMMIT "3ea97e7591"\n',
                    good+'#define CONFIG_OLD 1\n'):
            with self.subTest(header=bad),self.assertRaises(ValueError):verify_config_header(cfg,bad)
    def test_alias_must_also_match_its_own_config_value(self):
        with self.assertRaises(ValueError):
            verify_config_header({'CONFIG_NEW':'3','CONFIG_OLD':'4'},
                                 '#define CONFIG_NEW 3\n#define CONFIG_OLD CONFIG_NEW\n')
if __name__=='__main__':unittest.main()
