"""Local review-only linker adaptation, never edit the verified SDK package."""

def readonly_sections(text):
    old = ".flash.rodata : ALIGN(0x10)"
    if text.count(old) != 1 or ".flash.rodata (READONLY)" in text:
        raise ValueError("expected exactly one unchanged SDK flash rodata declaration")
    return text.replace(old, ".flash.rodata (READONLY) : ALIGN(0x10)")

def configure(env, package, targets):
    from pathlib import Path
    import hashlib, json
    if (env["PIOENV"] not in ("warp4_tesla_ble_verifier_review", "warp4_tesla_ble_classic_review", "warp4_tesla_ble_nimble_review") or
            list(targets) != ["tesla-review"] or
            env.GetProjectOption("custom_signature_preset") != "" or
            env.GetProjectOption("custom_signature_verify_preset") != "review:official-public-v1"):
        raise ValueError("explicit unsigned public-verifier ELF review only")
    flags = list(env["LINKFLAGS"])
    if flags.count("sections.ld") != 1:
        raise ValueError("expected exactly one SDK sections.ld linker input")
    index = flags.index("sections.ld")
    if index == 0 or flags[index - 1] != "-T":
        raise ValueError("expected explicit SDK -T sections.ld")
    source = Path(package) / "esp32/ld/sections.ld"
    original = source.read_text()
    adapted = readonly_sections(original)
    target = Path(env.subst("$BUILD_DIR")) / "review-readonly-sections.ld"
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists() or target.read_text() != adapted:
        target.write_text(adapted)
    derivation = {"disposition":"LOCAL NONINSTALLABLE REVIEW ONLY", "source":str(source),
                  "source_sha256":hashlib.sha256(original.encode()).hexdigest(),
                  "derived_sha256":hashlib.sha256(adapted.encode()).hexdigest(),
                  "change":"Only .flash.rodata output declaration gains READONLY; no placement, input, SDK or security change."}
    (target.parent/"review-link-derivation.json").write_text(json.dumps(derivation,indent=2)+"\n")
    flags[index] = str(target)
    env.Replace(LINKFLAGS=flags)
    env.Depends("$BUILD_DIR/${PROGNAME}.elf", str(target))

if "Import" in globals():
    import os
    from SCons.Script import COMMAND_LINE_TARGETS
    Import("env")
    configure(env, os.environ["TESLA_BLE_FRAMEWORK_LIBS"], COMMAND_LINE_TARGETS)
