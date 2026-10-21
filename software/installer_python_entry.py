"""Isolated Python generator entry with one explicit immutable script root."""
import hashlib
import os
from pathlib import PurePosixPath
import runpy
import sys
sys.path.insert(0,"/snapshot/project")
from installer_snapshot import read_build_context, _mounted_file
from installer_generators import manifest
from installer_sdk_verify import require


def publish_source_package():
    """Preserve upstream's software package name at the fixed snapshot layout.

    create_parent_module normally walks to a directory literally named software.
    /work/project is intentionally not the mutable checkout path. Publish the
    same approved package from its readonly snapshot, not an ambient sys.path.
    """
    import importlib.util
    path='/work/project/__init__.py'
    fd,_=_mounted_file(path)
    os.close(fd)
    require('software' not in sys.modules, 'GENERATOR_SOURCE_PACKAGE_COLLISION')
    spec=importlib.util.spec_from_file_location('software',path,
                                               submodule_search_locations=['/work/project'])
    module=importlib.util.module_from_spec(spec)
    sys.modules['software']=module
    try: spec.loader.exec_module(module)
    except BaseException:
        del sys.modules['software']
        raise


def run(environment,script,arguments):
    context=read_build_context(environment)
    p=PurePosixPath(script)
    require(p.is_absolute() and str(p)==script and p.is_relative_to("/work/project") and
            ".." not in p.parts and p.suffix==".py","GENERATOR_SCRIPT_PATH")
    target="/snapshot/project/"+p.relative_to("/work/project").as_posix()
    records=manifest(context)
    require(target in records and records[target]["role"]=="source","GENERATOR_UNBOUND_INPUT")
    fd,st=_mounted_file(script)
    try:
        require(hashlib.sha256(os.pread(fd,st.st_size+1,0)).hexdigest()==records[target]["sha256"],
                "PREPARE_SCRIPT_DIGEST_MISMATCH")
    finally: os.close(fd)
    publish_source_package()
    sys.argv=[script]+arguments
    sys.path.insert(0,str(p.parent))
    runpy.run_path(script,run_name="__main__")


if __name__=="__main__":
    require(len(sys.argv)>=3,"GENERATOR_ARGUMENTS_REQUIRED")
    run(sys.argv[1],sys.argv[2],sys.argv[3:])
