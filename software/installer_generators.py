"""Installer-only fixed Python/Node consumers; never uv/npm provisioning.

Snapshot approval does not prove generated outputs. The host-owned completed
generator parent and independent final link gate establish stage-bound evidence,
not obsolete exact per-read consumption.
"""
import hashlib
import os
from pathlib import PurePosixPath
import subprocess
from installer_sdk_verify import require
from installer_snapshot import read_build_context, _mounted_file, _parse_context

NODE_TOOLS={"tsc":"typescript/bin/tsc", "esbuild":"esbuild/bin/esbuild",
            "sass":"sass/sass.js", "postcss":"postcss-cli/index.js",
            "html-minifier-terser":"html-minifier-terser/cli.js"}


def _record(records,target,role):
    require(target in records and records[target]["role"]==role,"GENERATOR_UNBOUND_INPUT: "+target)


def manifest(guarded):
    fd,st=_mounted_file("/run/verified-context.json")
    try: value=_parse_context(os.pread(fd,st.st_size+1,0))
    finally: os.close(fd)
    require(value["snapshot_sha256"]==guarded["snapshot_sha256"],"GUARD_CONTEXT_MISMATCH")
    return value["manifest"]["files"]


def command(argv,cwd,environment,records):
    from installer_policy import ENVS
    require(environment in ENVS and type(argv) is list and bool(argv) and
            all(type(a) is str and "\0" not in a for a in argv),"GENERATOR_COMMAND_SCHEMA")
    p=PurePosixPath(cwd)
    require(p.is_absolute() and str(p)==cwd and p.is_relative_to("/work/project") and ".." not in p.parts,
            "GENERATOR_PRIVATE_CWD_REQUIRED")
    require(argv[:2]==["uv","run"] and len(argv)>=3,"GENERATOR_COMMAND_FORBIDDEN")
    args=argv[2:]
    if args[0]=="npx":
        require(len(args)>=2 and args[1] in NODE_TOOLS,"GENERATOR_NODE_TOOL_FORBIDDEN")
        node="/snapshot/node_environment/bin/node"
        target="/snapshot/project/web/node_modules/"+NODE_TOOLS[args[1]]
        _record(records,node,"node_environment")
        _record(records,target,"web_dependencies")
        return [node,target]+args[2:]
    name=args[0]
    require(PurePosixPath(name).name==name and name.endswith(".py"),"GENERATOR_COMMAND_FORBIDDEN")
    script=p/name
    target="/snapshot/project/"+script.relative_to("/work/project").as_posix()
    _record(records,target,"source")
    python="/snapshot/python/bin/python"
    entry="/snapshot/project/installer_python_entry.py"
    _record(records,python,"python_environment")
    _record(records,entry,"source")
    return [python,"-I","-B",entry,environment,str(script)]+args[1:]


def run(argv,environment,guarded,**kwargs):
    context=read_build_context(environment,guarded=guarded)
    require(set(kwargs)<= {"env","shell"} and kwargs.get("shell",False) is False,
            "GENERATOR_PROCESS_OPTIONS_FORBIDDEN")
    child_env=kwargs.get("env",dict(os.environ))
    require(type(child_env) is dict and set(child_env)<=set(os.environ)|{"PLATFORMIO_METADATA"} and
            all(child_env.get(k)==v for k,v in os.environ.items() if k!="PLATFORMIO_METADATA"),
            "GENERATOR_ENVIRONMENT_MISMATCH")
    cwd=os.getcwd()
    selected=command(argv,cwd,environment,manifest(context))
    # Native esbuild may be distributed directly instead of a JavaScript shim.
    if selected[1].endswith("/esbuild/bin/esbuild"):
        fd,_=_mounted_file(selected[1])
        try: native=os.pread(fd,4,0)==b"\x7fELF"
        finally: os.close(fd)
        if native: selected=selected[1:]
    return subprocess.check_call(selected,cwd=cwd,env=child_env,close_fds=True,stdin=subprocess.DEVNULL)


def verify_frozen_web(environment,guarded):
    context=read_build_context(environment,guarded=guarded)
    records=manifest(context)
    web={p:item for p,item in records.items() if item["role"]=="web_dependencies"}
    require(bool(web),"GENERATOR_WEB_DEPENDENCIES_REQUIRED")
    for target,item in web.items():
        require(target.startswith("/snapshot/project/web/node_modules/"),"GENERATOR_WEB_ROLE_LAYOUT")
        actual="/work/project/"+target[len("/snapshot/project/"):]
        fd,st=_mounted_file(actual)
        try:
            require(st.st_size==item["size"] and hashlib.sha256(os.pread(fd,st.st_size+1,0)).hexdigest()==item["sha256"],
                    "GENERATOR_WEB_DEPENDENCY_MISMATCH")
        finally: os.close(fd)
    return True


def source_metadata(environment,guarded):
    """Use explicit runtime-approved labels, never a live .git/cache in sandbox."""
    from installer_sdk_verify import hexpin
    context=read_build_context(environment,guarded=guarded)
    source=context.get('source_metadata')
    require(type(source) is dict and set(source)=={'is_release','is_dirty','git_url','git_commit_id','branch_name'} and
            type(source['is_release']) is bool and type(source['is_dirty']) is bool and
            all(type(source[k]) is str and bool(source[k]) and
                not any(c in source[k] for c in '\0\n\r') for k in ('git_url','git_commit_id','branch_name')) and
            hexpin(context.get('source_head'),40) and source['git_commit_id']==context['source_head'][:15],
            'GENERATOR_SOURCE_METADATA')
    return dict(source)


def generator_environment(environment, context):
    """Actual PIO option resolution + actual SCons, without platform provisioning.

    pio_hooks needs only these configuration methods; compiler/platform setup
    remains in the subsequent real PIO build with its unchanged first guard.
    """
    from platformio.project.config import ProjectConfig
    from SCons.Environment import Environment
    config=ProjectConfig.get_instance('/work/project/platformio.ini')
    section='env:'+environment
    env=Environment(tools=[])
    def get_option(name, *default):
        if config.has_option(section,name): return config.get(section,name)
        if default:return default[0]
        raise ValueError('GENERATOR_OPTION_MISSING: '+name)
    env.AddMethod(lambda self,name,*default:get_option(name,*default),'GetProjectOption')
    env.AddMethod(lambda self:False,'IsCleanTarget')
    env.Replace(PROJECT_DIR='/work/project',PROJECT_WORKSPACE_DIR='/tmp/workspace',
                BUILD_DIR='/output/build/'+environment,PIOENV=environment)
    env['WARP4_INSTALLER_VERIFIED']=context
    return env


def generate(environment):
    """Run the WHOLE actual hook and publish only newly produced private files."""
    import base64
    import json
    import runpy
    import shutil
    from pathlib import Path
    from installer_snapshot import canonical
    context=read_build_context(environment)
    require(context.get('stage_phase')=='generate','GENERATOR_STAGE_PHASE')
    import sys
    sys.path.insert(0,'/work/project')
    env=generator_environment(environment,context)
    Path(env.subst('$BUILD_DIR')).mkdir(parents=True)
    # Upstream enum/union specifications import software.util in this process.
    from installer_python_entry import publish_source_package
    publish_source_package()
    sys.path.insert(0,'/work/project')
    def imported(name):
        require(name=='env','GENERATOR_IMPORT')
        import inspect
        inspect.currentframe().f_back.f_globals['env']=env
    runpy.run_path('/snapshot/project/pio_hooks.py',init_globals={
        'Import':imported,'env':env,'COMMAND_LINE_TARGETS':[]})
    records=manifest(context)
    files={}
    for p in sorted(Path('/work/project').rglob('*')):
        if not p.is_file(): continue
        relative=p.relative_to('/work/project').as_posix()
        if '/snapshot/project/'+relative in records: continue
        require(not p.is_symlink(),'GENERATOR_OUTPUT_LINK')
        files[relative]=base64.b64encode(p.read_bytes()).decode('ascii')
    metadata=json.loads(Path(env.subst('$BUILD_DIR')+'/metadata.json').read_bytes())
    from installer_policy import effective_flags
    value={'schema':'warp4-generated-products-v1','snapshot_sha256':context['snapshot_sha256'],
           'build_id':context['build_id'],'environment':environment,'files':files,'metadata':metadata,
           'flags':effective_flags(env),'src_filter':list(env['SRC_FILTER'])}
    Path('/output/generated.json').write_bytes(canonical(value))
    shutil.rmtree('/output/build')


def decode_generated(data, context, records):
    """Validate ordinary generator bytes against host-owned run identity.

    Authority is the broker-sealed generation/context, NOT a digest in this JSON.
    Generated project files may be mutable only inside the next private action.
    """
    import base64
    from installer_sdk_verify import loads
    from installer_stage_verify import output_names
    value=loads(data)
    require(type(value) is dict and set(value)=={'schema','snapshot_sha256','build_id','environment',
            'files','metadata','flags','src_filter'} and value['schema']=='warp4-generated-products-v1',
            'GENERATOR_OUTPUT_SCHEMA')
    require(all(value[k]==context[k] for k in ('snapshot_sha256','build_id','environment')),
            'GENERATOR_OUTPUT_IDENTITY')
    require(type(value['files']) is dict,'GENERATOR_OUTPUT_SCHEMA')
    output_names(value['files'])
    files={};total=0
    for name,encoded in value['files'].items():
        require('/snapshot/project/'+name not in records,'GENERATOR_SOURCE_COLLISION')
        require(type(encoded) is str and len(encoded)<=90*1024*1024,'GENERATOR_OUTPUT_SIZE')
        try: blob=base64.b64decode(encoded,validate=True)
        except (ValueError,TypeError) as exc: raise ValueError('GENERATOR_OUTPUT_ENCODING') from exc
        total+=len(blob)
        require(len(blob)<=64*1024*1024 and total<=512*1024*1024,'GENERATOR_OUTPUT_SIZE')
        files[name]=blob
    require({'src/index_html.embedded.cpp','src/index_html.embedded.h','src/build.h','src/build.cpp'}<=set(files),
            'GENERATOR_REQUIRED_OUTPUT')
    metadata=value['metadata']
    require(type(metadata) is dict and metadata.get('project_dir')=='/work/project' and
            metadata.get('build_dir')=='/output/build/'+context['environment'] and
            metadata.get('installer_public_key_sha256')==context['public_key_sha256'] and
            metadata.get('product_id')==metadata.get('firmware_info_product_id')=='warp4' and
            metadata.get('firmware_info_product_name')=='WARP4 Charger' and
            metadata.get('signature_preset')=='' and metadata.get('secure_boot')=='',
            'GENERATOR_METADATA_IDENTITY')
    from installer_policy import validate_effective
    approved={k:(context['effective_flags'][context['environment']][k] if k=='BUILD_FLAGS' else [])
              for k in ('BUILD_FLAGS','CPPDEFINES','CCFLAGS','CFLAGS','CXXFLAGS','LINKFLAGS')}
    validate_effective(value['flags'],False,approved=approved)
    require(type(value['src_filter']) is list and bool(value['src_filter']) and
            all(type(v) is str and '\0' not in v for v in value['src_filter']),'GENERATOR_SOURCE_FILTER')
    return value,files


def consume(env):
    """Import only the completed host-bound generator into fresh private scratch.

    The mounted /generation bundle is readonly; intra-action mutable generated
    scratch is an explicitly approved stage-sealed limit, not per-read capture.
    """
    from pathlib import Path
    from installer_snapshot import canonical
    environment=env['PIOENV']
    context=read_build_context(environment,guarded=env['WARP4_INSTALLER_VERIFIED'])
    require(context.get('stage_phase')=='build','GENERATOR_STAGE_PHASE')
    fd,st=_mounted_file('/generation/generated.json')
    try: data=os.pread(fd,st.st_size+1,0)
    finally: os.close(fd)
    require(hashlib.sha256(data).hexdigest()==context.get('generator_output_sha256'),
            'GENERATOR_PARENT_MISMATCH')
    value,files=decode_generated(data,context,manifest(context))
    for name,blob in files.items():
        target=Path('/work/project')/name
        target.parent.mkdir(parents=True,exist_ok=True)
        if target.exists():
            require(not target.is_symlink() and target.is_file() and target.read_bytes()==blob,
                    'GENERATOR_PRIVATE_OUTPUT_CHANGED')
        else:target.write_bytes(blob)
    build=Path(env.subst('$BUILD_DIR'))
    require(str(build)==value['metadata']['build_dir'],'GENERATOR_BUILD_DIR')
    build.mkdir(parents=True,exist_ok=True)
    (build/'metadata.json').write_bytes(canonical(value['metadata']))
    env.Replace(BUILD_FLAGS=value['flags']['BUILD_FLAGS'],SRC_FILTER=value['src_filter'])
    from installer_policy import validate_effective
    validate_effective(env,False)
    env['WARP4_INSTALLER_NOT_FOR_DISTRIBUTION']=False
    return value
