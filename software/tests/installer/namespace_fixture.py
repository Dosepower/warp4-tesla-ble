"""Local host Python runtime fixture; never a production approval/pin source."""
import hashlib
import io
from pathlib import Path
import subprocess

from elftools.elf.elffile import ELFFile


def python_inputs(root, project, *, native_tools=None, include_pio=False):
    inputs = {}
    executables = []
    interpreters = {}
    libraries = {}
    needed = []
    def add(target, data, role, executable=False):
        copy = root / ('input-' + str(len(inputs)))
        copy.write_bytes(data)
        inputs[target] = {'path': str(copy), 'sha256': hashlib.sha256(data).hexdigest(),
                          'role': role, 'mode': 0o555 if executable else 0o444}
        if data.startswith(b'\x7fELF'):
            elf = ELFFile(io.BytesIO(data))
            # Python's stdlib also contains relocatable build-support objects.
            # Inventory them as data, not as runnable ELF/loader entrypoints.
            if elf['e_type'] not in ('ET_EXEC', 'ET_DYN'):
                inputs[target]['mode'] = 0o444
                return
            if role != 'loader_runtime':
                executables.append(target)
            for segment in elf.iter_segments():
                if segment['p_type'] == 'PT_INTERP':
                    original = segment.get_interp_name()
                    name = Path(original).name
                    interpreters[original] = '/snapshot/loader_runtime/' + name
                    needed.append(name)
                if segment['p_type'] == 'PT_DYNAMIC':
                    needed.extend(tag.needed for tag in segment.iter_tags() if tag.entry.d_tag == 'DT_NEEDED')
    # Test only: observe the installed system runtime instead of endorsing it.
    observed = subprocess.check_output(['/usr/bin/python3', '-c', 'import sysconfig;print(sysconfig.get_path("stdlib"))'], text=True).strip()
    stdlib = Path(observed)
    version = stdlib.name
    executable = Path('/usr/bin/python3').resolve()
    add('/snapshot/python/bin/python', executable.read_bytes(), 'python', True)
    for path in sorted(stdlib.rglob('*')):
        if not path.is_file() or path.is_symlink() or '__pycache__' in path.parts or 'site-packages' in path.relative_to(stdlib).parts:
            continue
        data = path.read_bytes()
        add('/snapshot/python/lib/' + version + '/' + path.relative_to(stdlib).as_posix(), data, 'python', data.startswith(b'\x7fELF'))
    if include_pio:
        # Test input from the project venv, not a runtime endorsement. PlatformIO
        # and its dependencies are Python source; do not import ABI-incompatible
        # CPython-3.12 extensions into this explicit system-3.13 fixture.
        site = Path(__file__).resolve().parents[2] / ".venv/lib/python3.12/site-packages"
        selected={"platformio","bottle.py","click","colorama","marshmallow","elftools","serial","requests","semantic_version","tabulate","ajsonrpc","starlette","uvicorn","wsproto","anyio","typing_extensions.py","h11","urllib3","certifi","idna","charset_normalizer","packaging"}
        for path in sorted(site.rglob("*")):
            first=path.relative_to(site).parts[0]
            if first not in selected and not first.endswith(".dist-info"):
                continue
            if not path.is_file() or path.is_symlink() or "__pycache__" in path.parts:
                continue
            if path.suffix in (".so", ".pyc", ".pyo"):
                continue
            add("/snapshot/python/lib/python3/dist-packages/"+path.relative_to(site).as_posix(),
                path.read_bytes(), "python")
    for target, path in (native_tools or {}).items():
        add(target, Path(path).read_bytes(), "native_fixture_tool", True)
    while needed:
        name = needed.pop()
        if name in libraries:
            continue
        path = Path('/usr/lib/x86_64-linux-gnu') / name
        if not path.is_file():
            raise AssertionError('test host runtime library absent: ' + name)
        target = '/snapshot/loader_runtime/' + name
        libraries[name] = target
        add(target, path.read_bytes(), 'loader_runtime', True)
    for relative, data in project.items():
        add('/snapshot/project/' + relative, data, 'source')
    return inputs, {'executables': executables, 'interpreters': interpreters, 'libraries': libraries}
