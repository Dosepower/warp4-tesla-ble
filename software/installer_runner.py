"""Protected namespace supervisor: actual PIO child, then private output export.

This does not claim per-read tracing. The OTA posthook fails closed if the
host-owned generator/build stage chain or independent link evidence is absent. stdout is reserved for one envelope;
all ordinary PlatformIO/generator/compiler output goes to stderr.
"""
import os
from pathlib import Path
import subprocess
import sys
# -I script execution deliberately excludes project paths from Python startup.
sys.path.insert(0, "/snapshot/project")
from installer_snapshot import read_build_context
from installer_sdk_verify import require
from installer_safeio import read_bytes, directory_fd
from installer_output import encode_output
from installer_policy import ENVS


def verify_python_runtime(context):
    # A venv symlink or pyvenv.cfg naming an ambient host home is not a
    # materialized runtime. Reject relocation failure BEFORE PlatformIO exec.
    require(sys.executable=="/snapshot/python/bin/python" and
            sys.prefix=="/snapshot/python" and sys.exec_prefix=="/snapshot/python",
            "PYTHON_RUNTIME_PREFIX")
    from installer_snapshot import _mounted_file, _parse_context
    import hashlib
    fd,st=_mounted_file("/run/verified-context.json")
    try: value=_parse_context(os.pread(fd,st.st_size+1,0))
    finally: os.close(fd)
    require(value["snapshot_sha256"]==context["snapshot_sha256"],"GUARD_CONTEXT_MISMATCH")
    records=value["manifest"]["files"]
    def role(path):
        p=Path(path)
        require(p.is_absolute() and ".." not in p.parts,"PYTHON_RUNTIME_PATH")
        if p.is_relative_to("/snapshot/python"):
            return "python_environment"
        require(p.is_relative_to("/snapshot/project"),"PYTHON_RUNTIME_FOREIGN_MODULE")
        return "source"
    for path in sys.path:
        role(path)
    paths={sys.executable}
    for module in list(sys.modules.values()):
        path=getattr(module,"__file__",None)
        if not path: continue
        spec=getattr(module,"__spec__",None)
        if spec is not None and spec.origin=="frozen": continue
        paths.add(path)
    for path in sorted(paths):
        expected_role=role(path)
        require(path in records and records[path]["role"]==expected_role,"PYTHON_RUNTIME_UNBOUND_MODULE: "+path)
        fd,st=_mounted_file(path)
        try:
            require(st.st_size==records[path]["size"] and
                    hashlib.sha256(os.pread(fd,st.st_size+1,0)).hexdigest()==records[path]["sha256"],
                    "PYTHON_RUNTIME_MODULE_DIGEST")
        finally: os.close(fd)
    return True


def collect_output(context):
    name=context["environment"]
    build_id=context["build_id"]
    destination=Path("/output/candidates")/name/build_id
    parent=directory_fd(destination)
    try:
        build=Path("/output/build")/name
        program="warp4-ota-candidate-"+build_id
        for target,source in (("firmware.elf",build/(program+".elf")),
                              ("firmware.map",build/(program+".map")),
                              ("metadata.json",build/"metadata.json")):
            data=read_bytes(source)
            fd=os.open(target,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW|os.O_CLOEXEC,0o600,dir_fd=parent)
            with os.fdopen(fd,"wb") as stream: stream.write(data)
    finally: os.close(parent)
    return encode_output(destination,["firmware-UNSIGNED_ota.bin","firmware_info.bin","manifest.json",
                                      "firmware.elf","firmware.map","metadata.json"],
                         context["snapshot_sha256"],build_id)


def run(environment):
    require(environment in ENVS,"OFFLINE_DEFAULT_ONLY")
    context=read_build_context(environment)
    verify_python_runtime(context)
    subprocess.run(["/snapshot/python/bin/python","-I","-B","-m","platformio","run","-e",environment],
                   cwd="/work/project",env=dict(os.environ),stdin=subprocess.DEVNULL,
                   stdout=sys.stderr,stderr=sys.stderr,close_fds=True,check=True)
    read_build_context(environment,guarded=context)
    return collect_output(context)


def capture_link_command(source, target, env):
    import json, shlex
    build=Path(env.subst('$BUILD_DIR'))
    build.mkdir(parents=True,exist_ok=True)
    command=shlex.split(env.subst('$LINKCOM',target=target,source=source))
    (build/'installer-link-command.json').write_text(json.dumps(command))


def collect_linked(env, context):
    import base64, json
    from installer_snapshot import canonical
    build=Path(env.subst('$BUILD_DIR'))
    files={}
    for path in sorted(build.rglob('*')):
        if path.is_file() and path.suffix in ('.o','.d','.a','.elf','.map','.bin','.json','.ld','.rsp'):
            files[str(path)]=base64.b64encode(read_bytes(path)).decode('ascii')
    from installer_policy import effective_flags
    flags=effective_flags(env)
    value={'schema':'warp4-linked-products-v1','build_id':context['build_id'],
           'snapshot_sha256':context['snapshot_sha256'],'files':files,'flags':flags,
           'link_command':json.loads(read_bytes(build/'installer-link-command.json'))}
    Path('/output/linked.json').write_bytes(canonical(value))


def finalize(environment):
    import json, runpy
    from installer_snapshot import _mounted_file, _parse_context
    from installer_stage_verify import verify_production_stage
    from installer_output import LinkedProducts
    context=read_build_context(environment)
    require(context.get('stage_phase')=='finalize','PRODUCTION_STAGE_PHASE')
    fd,st=_mounted_file('/generation/linked.json')
    try: data=os.pread(fd,st.st_size+1,0)
    finally: os.close(fd)
    verify_production_stage(context,data)
    fd,st=_mounted_file('/run/verified-context.json')
    try: manifest=_parse_context(os.pread(fd,st.st_size+1,0))['manifest']
    finally: os.close(fd)
    def snapshot_read(path):
        fd,st=_mounted_file(path)
        try: return os.pread(fd,st.st_size+1,0)
        finally: os.close(fd)
    fd,st=_mounted_file('/generation/generated.json')
    try: generated_data=os.pread(fd,st.st_size+1,0)
    finally: os.close(fd)
    view=LinkedProducts(data,context,manifest,snapshot_read,generated_data=generated_data)
    # Fresh private output, populated ONLY from the sealed completed parent.
    for path,blob in view.files.items():
        p=Path(path);p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(blob)
    scons=list(Path('/snapshot/platformio/packages/tool-scons').glob('scons-local-*'))
    require(len(scons)==1,'SCONS_RUNTIME_CLOSURE')
    sys.path.insert(0,str(scons[0]))
    from SCons.Environment import Environment
    env=Environment(tools=[])
    env.Replace(**view.flags)
    env.Replace(BUILD_DIR=view.output,PROGNAME='warp4-ota-candidate-'+context['build_id'])
    env['WARP4_INSTALLER_VERIFIED']=context
    env['WARP4_INSTALLER_NOT_FOR_DISTRIBUTION']=False
    env['WARP4_INSTALLER_STAGE_VIEW']=view
    def imported(name):
        require(name=='env','POSTHOOK_IMPORT')
    # This executes the unmodified production posthook entry, not a fixture copy
    # of its validation/serialization algorithm.
    module=runpy.run_path('/snapshot/project/warp4_ota_only_hook.py',init_globals={'Import':imported,'env':env})
    module['emit_ota']([],[],env)
    return collect_output(context)


if __name__ == '__main__':
    require(len(sys.argv) in (2,3),'OFFLINE_DEFAULT_ONLY')
    if len(sys.argv)==2:
        print(run(sys.argv[1]))
    elif sys.argv[2]=='build':
        context=read_build_context(sys.argv[1])
        require(context.get('stage_phase')=='build','PRODUCTION_STAGE_PHASE')
        verify_python_runtime(context)
        subprocess.run(['/snapshot/python/bin/python','-I','-B','-m','platformio','run','-e',sys.argv[1]],
                       cwd='/work/project',env=dict(os.environ),stdin=subprocess.DEVNULL,
                       stdout=sys.stderr,stderr=sys.stderr,close_fds=True,check=True)
        read_build_context(sys.argv[1],guarded=context)
        require(Path('/output/linked.json').is_file(),'PRODUCTION_STAGE_OUTPUT_MISSING')
        # Compiler/build directories are private scratch; only linked.json is
        # promoted. The broker itself performs namespace teardown before sealing.
        import shutil
        shutil.rmtree('/output/build')
    elif sys.argv[2]=='finalize':
        Path('/output/export.json').write_text(finalize(sys.argv[1]))
        import shutil
        shutil.rmtree('/output/build');shutil.rmtree('/output/candidates')
    else: raise ValueError('OFFLINE_DEFAULT_ONLY')
