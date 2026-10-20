"""Shared source-only host dependency locations; never use device evidence."""
import os
from pathlib import Path

software = Path(__file__).resolve().parents[2]
cache = Path(os.environ.get('TESLA_CLEAN_TEST_OUT', software / '.pio/tesla-host-secure'))
mbed = Path(os.environ.get('TESLA_BLE_HOST_MBEDTLS_SOURCE',
    software / '.pio/tesla-sdk/esp-idf-secure/components/mbedtls/mbedtls'))
