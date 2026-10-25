"""Vertical production cleanup/evidence/repeated attempts; no device access."""
from pathlib import Path
import os,subprocess,sys
T=Path(__file__).parent
subprocess.run([sys.executable,str(T/'test_multi_tesla.py'),*(['sanitize']if 'sanitize'in sys.argv else []),'guided-evidence-adopt','guided-evidence-idle','guided-evidence-io','guided-delete-local','guided-delete-unpair','guided-delete-orphan','unpair-revokes'],check=True)
subprocess.run([sys.executable,str(T/'test_nimble_worker_timeout.py'),*(['sanitize']if 'sanitize'in sys.argv else [])],env={**os.environ,'TESLA_TIMEOUT_FLOW_MODES':'flow-repeat flow-repeat-cleanup'},check=True)
