"""Public verification only. No signing, key creation, or secret access."""
import configparser
import hashlib
from pathlib import Path
from installer_safeio import read_bytes
from installer_sdk_verify import loads, hexpin, require, sha, fields


def verify_identity(project):
    project = Path(project)
    lock = loads(read_bytes(project / "installer_identity.lock.json"))
    fields(lock, ("schema", "preset", "publisher", "public_key_sha256", "official_reference_sha256"), ("status",))
    require(lock.get("schema") == "warp4-official-verifier-v1", "official verifier schema mismatch")
    require(hexpin(lock.get("public_key_sha256")) and hexpin(lock.get("official_reference_sha256")),
            "OFFICIAL_WARP_PUBLIC_KEY_ANCHOR_MISSING")
    require(lock.get("preset") == "warp:v1" and lock.get("publisher") == "Tinkerforge GmbH", "official identity mismatch")
    config = configparser.ConfigParser()
    config.read_string(read_bytes(project / "signature/config.ini").decode())
    require(config.has_section("preset:warp:v1"), "official verifier preset missing")
    preset = dict(config["preset:warp:v1"])
    if "extends" in preset:
        preset = dict(config[preset["extends"]]) | preset
    require(preset.get("publisher") == lock["publisher"], "official publisher mismatch")
    relative = Path(preset["sodium_public_key_path"])
    require(not relative.is_absolute() and ".." not in relative.parts, "public-key path escape")
    path = project / "signature/keys" / relative
    require(not path.is_symlink(), "public-key symlink forbidden")
    key_json = loads(read_bytes(path))
    fields(key_json, ("sodium_public_key",))
    require(type(key_json["sodium_public_key"]) is str, "public key JSON type")
    key = bytes.fromhex(key_json["sodium_public_key"])
    require(len(key) == 32 and hashlib.sha256(key).hexdigest() == lock["public_key_sha256"], "official public-key fingerprint mismatch")
    # An independent known official signature must validate too, not just a name.
    from installer_ota_verify import verify_official_signature
    reference = project / ".pio/installer-evidence/official-warp4-ota.bin"
    reference_bytes = read_bytes(reference)
    require(hashlib.sha256(reference_bytes).hexdigest() == lock["official_reference_sha256"], "official reference digest mismatch")
    verify_official_signature(reference_bytes, key, lock["publisher"])
    return {"public_key_sha256": lock["public_key_sha256"], "preset": lock["preset"],
            "identity_snapshot": {
                "signature/config.ini": {"path": str(project / "signature/config.ini"), "sha256": sha(project / "signature/config.ini")},
                "signature/keys/" + relative.as_posix(): {"path": str(path), "sha256": sha(path)},
                "signature/official-reference.bin": {"path": str(reference), "sha256": lock["official_reference_sha256"]}}}


def verify_elf_key(data, expected):
    """Read the linked, allocated readonly symbol, not the current key file."""
    import io
    from elftools.elf.elffile import ELFFile
    from elftools.common.exceptions import ELFError
    try:
        elf = ELFFile(io.BytesIO(data))
        require(elf.elfclass == 32 and elf.little_endian and elf["e_machine"] == "EM_XTENSA", "final Xtensa ELF required")
        symbols = elf.get_section_by_name(".symtab")
        require(symbols is not None, "final ELF symbol table required")
        found = symbols.get_symbol_by_name("signature_sodium_public_key_data")
        require(found is not None and len(found) == 1, "unique linked verifier key required")
        symbol = found[0]
        require(symbol["st_size"] == 32 and type(symbol["st_shndx"]) is int, "linked verifier key size/section")
        section = elf.get_section(symbol["st_shndx"])
        require(section["sh_flags"] & 2 and not section["sh_flags"] & 1 and section["sh_type"] != "SHT_NOBITS", "key must be allocated readonly bytes")
        offset = symbol["st_value"] - section["sh_addr"]
        require(offset >= 0 and offset + 32 <= section["sh_size"], "linked verifier key bounds")
        key = section.data()[offset:offset+32]
        require(hexpin(expected) and hashlib.sha256(key).hexdigest() == expected, "embedded public-key fingerprint mismatch")
        return symbol["st_value"], key
    except (ELFError, IndexError, KeyError) as exc:
        raise ValueError("invalid final ELF key evidence") from exc


def verify_app_key(app, address, key, expected):
    from installer_ota_verify import verify_app
    import struct
    verify_app(app)
    require(type(address) is int and type(key) is bytes and len(key) == 32 and hexpin(expected) and
            hashlib.sha256(key).hexdigest() == expected, "embedded public-key fingerprint mismatch")
    position = 24
    matches = []
    for _ in range(app[1]):
        start, size = struct.unpack_from("<II", app, position); position += 8
        if start <= address and address + 32 <= start + size:
            matches.append(app[position + address - start:position + address - start + 32])
        position += size
    require(matches == [key], "final app key does not match linked verifier symbol")
    return True
