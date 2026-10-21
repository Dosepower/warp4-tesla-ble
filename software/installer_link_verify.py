"""Strict synthetic link-input validation; real A/B graph qualification remains required."""
from installer_policy import ENVS

def verify_tesla_presence(name, text):
    if name not in ENVS or not text.strip():
        raise ValueError("missing or unknown A/B link graph")
    for token in ("BENCH", "--wrap=", "RADIO_SPIKE", "r13_psram_policy", "NONINSTALLABLE", "iram_diagnostics", "bluetooth_master_bench"):
        if token.lower() in text.lower():
            raise ValueError("nonproduction link input: " + token)
    lower = text.lower()
    if name == ENVS[0]:
        if any(t in lower for t in ("teslavehicle", "tesla_vehicle", "teslable", "tesla_ble")):
            raise ValueError("Tesla input/symbol in negative A link graph")
    else:
        import re
        retained = lower.split("linker script and memory map\n", 1)[-1] if "linker script and memory map\n" in lower else ""
        records = re.findall(r"^\s*(?:\.\S+\s+)?0x[0-9a-f]+\s+(0x[0-9a-f]+)\s+([^\n]+)$", retained, re.M)
        paths = [path for size, path in records if int(size, 16) > 0]
        if not all(any(token in path for path in paths) for token in ("tesla_vehicle", "teslableclean")):
            raise ValueError("missing retained positive B Tesla module/library sections")
    return True


def verify_link_inputs(text, closure, *, sealed_view=None):
    from pathlib import Path
    import re
    from installer_sdk_verify import require, inventory
    from installer_safeio import read_bytes
    required={"cwd","immutable_roots","output_root","link_command","dependency_files"}
    require(type(closure) is dict and required<=set(closure)<=required|{"archive_dependencies"},
            "LINK_CLOSURE_EVIDENCE_REQUIRED")
    require("Memory Configuration\n" in text and "Linker script and memory map\n" in text,
            "malformed link map; retained section structure required")
    cwd, output = Path(closure["cwd"]), Path(closure["output_root"])
    require(cwd.is_absolute() and output.is_absolute() and (cwd.is_relative_to(output) or sealed_view is not None and str(cwd)=="/work/project"), "explicit link working/output roots required")
    roots = closure["immutable_roots"]
    require(type(roots) is dict and bool(roots), "immutable link inputs required")
    for root, expected in roots.items():
        require(Path(root).is_absolute() and not output.is_relative_to(root) and not Path(root).is_relative_to(output), "overlapping link roots")
        require((inventory(root) if sealed_view is None else sealed_view.inventory(root)) == expected, "immutable link inventory mismatch")

    from installer_graph import LinkSemantics, depfile, archive_members
    script_paths = set()
    linked_objects = set()
    generated_archives=set()
    archive_dependencies=closure.get("archive_dependencies",{})
    require(type(archive_dependencies) is dict,"ARCHIVE_MEMBER_DEPENDENCY_SCHEMA")

    def bound(token, generated=True):
        archive_match = re.fullmatch(r"(.+\.a)\(([^()]+)\)", token)
        if archive_match:
            archive, member = archive_match.groups()
            data = bound(archive, generated)
            require(member in archive_members(data), "MAP_ARCHIVE_MEMBER_MISSING: " + member)
            return data
        path = Path(token)
        path = path if path.is_absolute() else cwd / path
        require(".." not in path.parts, "unnormalized link path")
        expected_entry = None
        for root, expected in roots.items():
            if path.is_relative_to(root):
                relative = path.relative_to(root).as_posix()
                require(relative in expected, "unapproved immutable link input")
                expected_entry = expected[relative]
                break
        if expected_entry is None:
            require(generated and path.is_relative_to(output), "foreign link input: " + str(path))
            require(path.suffix in (".o", ".a", ".ld", ".rsp", ".d", ".h", ".cpp", ".c", ".S"), "unknown project output input")
        # Confinement precedes even a verifier read; no foreign host-file open.
        data = read_bytes(path) if sealed_view is None else sealed_view.read(str(path))
        if expected_entry is not None:
            import hashlib
            require(hashlib.sha256(data).hexdigest() == expected_entry["sha256"], "changed link input")
        if path.suffix == ".a":
            members=archive_members(data)
            if path.is_relative_to(output):
                import hashlib
                generated_archives.add(str(path))
                mapping=archive_dependencies.get(str(path))
                require(type(mapping) is dict and set(mapping)==set(members),"ARCHIVE_MEMBER_DEPENDENCY_REQUIRED")
                for member,obj in mapping.items():
                    require(type(obj) is str and Path(obj).is_absolute() and
                            Path(obj).is_relative_to(output) and Path(obj).suffix==".o",
                            "ARCHIVE_MEMBER_DEPENDENCY_OBJECT")
                    require(hashlib.sha256(bound(obj)).hexdigest()==members[member],"ARCHIVE_MEMBER_OBJECT_DIGEST")
        if path.suffix == ".ld": script_paths.add(str(path))
        if path.suffix == ".o" and path.is_relative_to(output): linked_objects.add(str(path))
        return data

    loads = re.findall(r"^LOAD (.+)$", text, re.M)
    require(bool(loads), "link map has no LOAD inputs")
    for token in loads:
        require(not token.startswith("["), "unresolved map input")
        bound(token.strip())
    for line in text.splitlines():
        match = re.search(r"(?:^|\s)([^\s]+\.(?:a(?:\([^()]+\))?|o|ld))\s*$", line)
        if match:
            bound(match[1])

    semantics = LinkSemantics(bound, str(cwd))
    def arguments(args):
        args = semantics.expand(args)
        i = 0
        while i < len(args):
            token = args[i]; i += 1
            if token.startswith("-Wl,"):
                arguments(token[4:].split(","))
            elif token in ("-T", "--script", "-L", "-I", "-isystem", "--sysroot"):
                require(i < len(args), "missing link path argument")
                value = args[i]; i += 1
                if token in ("-T", "--script"):
                    bound(value)
                else:
                    require(Path(value).is_absolute() and any(Path(value).is_relative_to(root) for root in roots), "unapproved link search root")
            elif token.startswith('--dynconfig='):
                bound(token.split('=',1)[1],generated=False)
            elif token.startswith('-L') and len(token)>2:
                path=Path(token[2:])
                require(path.is_absolute() and '..' not in path.parts and
                        (any(path.is_relative_to(root) for root in roots) or
                         sealed_view is not None and path.is_relative_to(output)), 'unapproved link search root')
            elif token.startswith(("--script=", "-T")):
                bound(token.split("=", 1)[1] if "=" in token else token[2:])
            elif token in ("-o", "-Map", "--Map"):
                require(i < len(args) and Path(args[i]).is_absolute() and Path(args[i]).is_relative_to(output), "link output escape")
                i += 1
            elif token in ("--start-group", "--end-group", "--gc-sections", "--cref", "--no-warn-rwx-segments", "-nostdlib", "-static"):
                pass
            elif token.startswith("-"):
                raise ValueError("unqualified linker option/search fallback: " + token)
            else:
                bound(token)
    command = closure["link_command"]
    require(type(command) is list and len(command) > 1 and all(type(v) is str for v in command), "captured link command required")
    bound(command[0], generated=False)
    arguments(command[1:])
    dependencies = closure["dependency_files"]
    require(type(dependencies) is list and bool(dependencies), "compiler dependency evidence required")
    dependency_targets = set()
    for dep in dependencies:
        rules = depfile(bound(dep))
        for target, inputs in rules.items():
            target_path = Path(target) if Path(target).is_absolute() else cwd / target
            require(target_path.is_relative_to(output) or not inputs, "foreign dependency target")
            if inputs:
                require(str(target_path) not in dependency_targets, "duplicate dependency coverage")
                dependency_targets.add(str(target_path))
                bound(str(target_path))
            for token in inputs: bound(token)
    require(set(archive_dependencies)==generated_archives,"ARCHIVE_MEMBER_DEPENDENCY_UNUSED_OR_MISSING")
    require(bool(linked_objects) and linked_objects <= dependency_targets,
            "INCOMPLETE_COMPILER_DEPENDENCY_COVERAGE")
    semantics = LinkSemantics(bound, str(cwd))
    parsed = set()
    while script_paths - parsed:
        script = sorted(script_paths - parsed)[0]
        semantics.script(script)
        parsed.add(script)
    return True


def verify_linkgraph(name, text, closure=None, *, sealed_view=None):
    verify_link_inputs(text, closure, sealed_view=sealed_view)
    return verify_tesla_presence(name, text)


def verify_pair(a, b, approved_tesla=None, *, stage_authorities=None):
    from installer_sdk_verify import hexpin, require
    from installer_graph import verify_pair_closures
    for manifest, name in ((a, ENVS[0]), (b, ENVS[1])):
        if manifest.get("verification", {}).get("product_id") != "warp4" or manifest.get("provenance", {}).get("environment") != name:
            raise ValueError("A/B product/environment mismatch")
    for key in ("sdk_manifest_sha256", "sdk_inventory_sha256", "runtime_manifest_sha256", "public_key_sha256"):
        av, bv = a["provenance"].get(key), b["provenance"].get(key)
        if not hexpin(av) or av != bv:
            raise ValueError("A/B base differs: " + key)
    require(type(approved_tesla) is dict, "EXPLICIT_APPROVED_TESLA_DELTA_REQUIRED")
    if 'stage_closure' in a or 'stage_closure' in b:
        from installer_stage_verify import verify_stage_pair
        verify_stage_pair(a.get('stage_closure'), b.get('stage_closure'), approved_tesla, stage_authorities)
    else:
        verify_pair_closures(a.get("action_closure"), b.get("action_closure"), approved_tesla)
    return True
