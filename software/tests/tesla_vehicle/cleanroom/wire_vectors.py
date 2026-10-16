# SPDX-License-Identifier: Apache-2.0
"""Independent wire/KAT fixtures from pinned official Tesla proto field numbers."""
from pathlib import Path
import hashlib, hmac
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import serialization

def varint(n):
    out=bytearray()
    while n>127: out.append((n&127)|128); n>>=7
    out.append(n); return bytes(out)
def block(field,data): return varint(field*8+2)+varint(len(data))+data
def integer(field,n): return varint(field*8)+varint(n)
def fixed(field,n): return varint(field*8+5)+n.to_bytes(4,'little')
def tlv(tag,data): return bytes([tag,len(data)])+data
local=ec.derive_private_key(1,ec.SECP256R1()); remote=ec.derive_private_key(2,ec.SECP256R1())
public=local.public_key().public_bytes(serialization.Encoding.X962,serialization.PublicFormat.UncompressedPoint)
key=hashlib.sha1(local.exchange(ec.ECDH(),remote.public_key())).digest()[:16]
vin=b'5YJ3E1EA7KF000001'; epoch=bytes(range(16)); nonce=bytes(range(12)); uuid=bytes(range(16)); routing=bytes(range(16,32))
query=block(2,block(1,block(2,b'')))
metadata=tlv(0,b'\x05')+tlv(1,b'\x03')+tlv(2,vin)+tlv(3,epoch)+tlv(4,(1239).to_bytes(4,'big'))+tlv(5,(8).to_bytes(4,'big'))+tlv(7,(1).to_bytes(4,'big'))+b'\xff'
aad=hashlib.sha256(metadata).digest(); sealed=AESGCM(key).encrypt(nonce,query,aad)
signature=block(1,block(1,public))+block(5,block(1,epoch)+block(2,nonce)+integer(3,8)+fixed(4,1239)+block(5,sealed[-16:]))
outgoing=block(6,integer(1,3))+block(7,block(2,routing))+block(10,sealed[:-16])+block(13,signature)+block(51,uuid)+integer(52,1)
identifier=b'\x05'+sealed[-16:]
charge=block(1,b'')+block(2,block(3,integer(114,72)))
response_meta=tlv(0,b'\x09')+tlv(1,b'\x03')+tlv(2,vin)+tlv(5,(1).to_bytes(4,'big'))+tlv(7,(1).to_bytes(4,'big'))+tlv(8,identifier)+tlv(9,bytes(4))+b'\xff'
reply=AESGCM(key).encrypt(nonce,charge,hashlib.sha256(response_meta).digest())
response=block(6,block(2,routing))+block(7,integer(1,3))+block(10,reply[:-16])+block(13,block(9,block(1,nonce)+integer(2,1)+block(3,reply[-16:])))+block(50,uuid)+integer(52,1)
pair_unsigned=block(16,block(5,block(1,block(1,public))+integer(4,6))+block(6,integer(1,9)))
pair=block(1,block(2,pair_unsigned)+integer(3,2))
values=dict(public_point=public,key=key,epoch=epoch,nonce=nonce,uuid=uuid,routing=routing,query=query,aad=aad,identifier=identifier,outgoing=outgoing,charge=charge,response=response,pair=pair)
text='// SPDX-License-Identifier: Apache-2.0\n// Independently generated PUBLIC fixtures; wire_vectors.py.\n#pragma once\nnamespace wire_vectors {\n'
for name,value in values.items(): text+='inline constexpr unsigned char '+name+'[]={'+','.join(f'0x{x:02x}' for x in value)+'};\n'
text+='}\n'
p=Path(__file__).with_name('wire_vectors.hpp')
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools'))
from bench_confinement import preflight
preflight(files=(p,)).revalidate()
if p.exists(): assert p.read_text()==text,'wire fixture drift'
else: p.write_text(text)
print('Independent official-schema outgoing/response/charging-manager pairing fixtures generated/verified')
