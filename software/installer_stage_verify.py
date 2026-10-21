"""Independent stage evidence parser; host-pinned heads establish authority.

Self-consistency alone is NOT provenance. expected_heads must come from the
protected host broker, never from the same build-supplied JSON being checked.
Exposure is conservative; no requirement to have consumed all exposed files.
"""
import hashlib
from pathlib import PurePosixPath
from installer_sdk_verify import require, hexpin
from installer_snapshot import canonical


def output_names(names):
    require(type(names) in (list,dict) and bool(names) and len(names)<=20000,
            'STAGE_OUTPUT_SCHEMA')
    for name in names:
        require(type(name) is str, 'STAGE_OUTPUT_PATH')
        p=PurePosixPath(name)
        require(str(p)==name and not p.is_absolute() and '..' not in p.parts and
                name not in ('','.') and len(p.parts)<=32 and len(name)<=4095 and
                not any(c in name for c in '\0\n\r'), 'STAGE_OUTPUT_PATH')
        require(not any(str(parent) in names for parent in p.parents if str(parent)!='.'),
                'STAGE_OUTPUT_PATH_CONFLICT')


def verify_stage_records(records, snapshot_sha256, expected_heads):
    fields={'schema','contract','snapshot_sha256','argv','input_scope','execution',
            'parent_generation','parents','outputs','stdout_sha256'}
    require(type(records) is list and bool(records) and len(records)<=20000,
            'STAGE_RECORD_LIST')
    require(hexpin(snapshot_sha256), 'STAGE_SNAPSHOT_MISMATCH')
    require(type(expected_heads) is list and bool(expected_heads) and
            all(hexpin(h) for h in expected_heads), 'STAGE_UNTRUSTED_HEAD')
    known={}; children=set()
    for record in records:
        require(type(record) is dict and set(record)==fields and
                record['schema']=='warp4-stage-generation-v1' and
                record['contract']=='declared-action-boundary-not-actual-open-capture' and
                record['input_scope']=='conservative-exposure-not-observed-consumption',
                'STAGE_RECORD_SCHEMA')
        require(record['snapshot_sha256']==snapshot_sha256, 'STAGE_SNAPSHOT_MISMATCH')
        argv=record['argv']; execution=record['execution']
        require(type(argv) is list and bool(argv) and all(type(a) is str and '\0' not in a for a in argv),
                'STAGE_ARGV_SCHEMA')
        require(type(execution) is dict and {'cwd','elf_binding','environment','script','materialization'} <= set(execution) <= {'cwd','elf_binding','environment','script','materialization','context_sha256'}
                and execution['cwd'] in ('/output','/work/project') and execution['materialization'] in
                ('sealed-ro-bind-data','named-private-readonly'), 'STAGE_EXECUTION_SCHEMA')
        require('context_sha256' not in execution or hexpin(execution['context_sha256']),'STAGE_CONTEXT_DIGEST')
        binding=execution['elf_binding']
        require(type(binding) is dict and set(binding)=={'executables','interpreters','libraries'} and
                type(binding['executables']) is list and argv[0] in binding['executables'] and
                argv[0].startswith('/snapshot/'), 'STAGE_TOOL_SCHEMA')
        env=execution['environment']
        require(type(env) is dict and all(type(k) is str and type(v) is str and
                '\0' not in k+v and '=' not in k for k,v in env.items()), 'STAGE_ENV_SCHEMA')
        script=execution['script']
        if script is not None:
            require(type(script) is dict and set(script)=={'path','sha256','interpreter','arguments'} and
                    script['interpreter']==argv[0] and hexpin(script['sha256']) and
                    type(script['arguments']) is list and
                    argv[1:len(script['arguments'])+2]==script['arguments']+[script['path']],
                    'STAGE_SCRIPT_SCHEMA')
        parents=record['parents']
        require(type(parents) is list and all(hexpin(p) and p in known for p in parents),
                'STAGE_PARENT_NOT_SEALED')
        require(record['parent_generation'] is None or record['parent_generation'] in parents,
                'STAGE_PARENT_NOT_SEALED')
        children.update(parents)
        merged={}
        for parent in parents:
            for name,item in known[parent]['outputs'].items():
                require(name not in merged or merged[name]==item, 'STAGE_PARENT_CONFLICT')
                merged[name]=item
        if merged: output_names(merged)
        outputs=record['outputs']
        require(type(outputs) is dict, 'STAGE_OUTPUT_SCHEMA')
        output_names(outputs)
        total=0
        for item in outputs.values():
            require(type(item) is dict and set(item)=={'size','sha256'} and
                    type(item['size']) is int and 0<=item['size']<=64*1024*1024 and
                    hexpin(item['sha256']), 'STAGE_OUTPUT_SCHEMA')
            total+=item['size']
        require(total<=512*1024*1024 and hexpin(record['stdout_sha256']), 'STAGE_OUTPUT_LIMIT')
        digest=hashlib.sha256(canonical(record)).hexdigest()
        require(digest not in known, 'STAGE_DUPLICATE_RECORD')
        known[digest]=record
    require(set(expected_heads)==set(known)-children, 'STAGE_UNTRUSTED_HEAD')
    return True


def verify_stage_pair(a,b,approved_tesla,authorities):
    """A/B equality over conservative exposure, with independent broker heads.

    This new schema deliberately does NOT assert each exposed file was opened.
    authorities are readonly host-context values, NOT manifest-supplied labels.
    Product identity, shared SDK and real final map/ELF/app checks remain separate.
    """
    require(type(authorities) is list and len(authorities)==2, 'STAGE_PAIR_AUTHORITY')
    require(type(approved_tesla) is dict, 'AB_TESLA_DELTA_DIFFERS')
    for value,authority in zip((a,b),authorities):
        require(type(value) is dict and set(value)=={'schema','common','tesla','manifest','records'} and
                value['schema']=='warp4-stage-closure-v1', 'STAGE_CLOSURE_SCHEMA')
        require(type(authority) is dict and set(authority)=={'snapshot_sha256','heads'} and
                hexpin(authority['snapshot_sha256']), 'STAGE_PAIR_AUTHORITY')
        for kind in ('common','tesla'):
            require(type(value[kind]) is dict and all(type(p) is str and p.startswith('/snapshot/') and
                    str(PurePosixPath(p))==p and '..' not in PurePosixPath(p).parts and hexpin(h)
                    for p,h in value[kind].items()), 'STAGE_EXPOSURE_SCHEMA')
        require(not set(value['common']) & set(value['tesla']), 'STAGE_EXPOSURE_OVERLAP')
        manifest=value['manifest']
        require(type(manifest) is dict and set(manifest)=={'schema','files'} and
                manifest['schema']=='warp4-sealed-closure-v1' and type(manifest['files']) is dict,
                'STAGE_SNAPSHOT_MANIFEST')
        require(hashlib.sha256(canonical(manifest)).hexdigest()==authority['snapshot_sha256'],
                'STAGE_SNAPSHOT_MISMATCH')
        exposed=value['common'] | value['tesla']
        require(set(exposed)==set(manifest['files']), 'STAGE_EXPOSURE_INCOMPLETE')
        for path,item in manifest['files'].items():
            require(type(item) is dict and set(item)=={'sha256','size','mode','role'} and
                    item['sha256']==exposed[path] and type(item['size']) is int and item['size']>=0 and
                    type(item['mode']) is int and item['mode'] in (0o444,0o555) and
                    type(item['role']) is str and bool(item['role']), 'STAGE_EXPOSURE_SCHEMA')
        verify_stage_records(value['records'],authority['snapshot_sha256'],authority['heads'])
        for record in value['records']:
            binding=record['execution']['elf_binding']
            for path in binding['executables']+list(binding['interpreters'].values())+list(binding['libraries'].values()):
                require(path in exposed and manifest['files'][path]['mode']==0o555, 'STAGE_TOOL_UNEXPOSED')
            script=record['execution']['script']
            if script is not None:
                require(exposed.get(script['path'])==script['sha256'], 'STAGE_SCRIPT_UNEXPOSED')
    require(bool(a['common']) and a['common']==b['common'], 'AB_COMMON_CLOSURE_DIFFERS')
    require(not a['tesla'] and b['tesla']==approved_tesla, 'AB_TESLA_DELTA_DIFFERS')
    return True


def verify_production_stage(context, linked):
    # Both authority and ledger must originate in the readonly host run context.
    # linked is ordinary generated data, and cannot supply either one.
    authority=context.get('stage_authority')
    ledger=context.get('stage_ledger')
    require(type(authority) is dict and set(authority)=={'snapshot_sha256','heads'} and
            type(ledger) is dict and set(ledger)=={'schema','snapshot_sha256','records','heads'} and
            ledger['schema']=='warp4-stage-dag-v1', 'PRODUCTION_STAGE_AUTHORITY_REQUIRED')
    require(authority['snapshot_sha256']==context.get('snapshot_sha256')==ledger['snapshot_sha256'] and
            authority['heads']==ledger['heads'], 'PRODUCTION_STAGE_CONTEXT_MISMATCH')
    verify_stage_records(ledger['records'],authority['snapshot_sha256'],authority['heads'])
    require(len(ledger['records'])==2, 'PRODUCTION_STAGE_BUILD_MISSING')
    generator,record=ledger['records']
    generator_identity=hashlib.sha256(canonical(generator)).hexdigest()
    require(generator['argv']==['/snapshot/python/bin/python','-I','-B',
            '/snapshot/project/installer_generator_entry.py',context['environment']] and
            generator['parents']==[] and set(generator['outputs'])=={'generated.json'},
            'PRODUCTION_GENERATOR_MISSING')
    require(hexpin(context.get('generator_context_sha256')) and
            generator['execution'].get('context_sha256')==context['generator_context_sha256'] and
            generator['outputs']['generated.json']['sha256']==context.get('generator_output_sha256'),
            'PRODUCTION_GENERATOR_CONTEXT_MISMATCH')
    require(record['argv']==['/snapshot/python/bin/python','-I','-B',
            '/snapshot/project/installer_runner.py',context['environment'],'build'] and
            record['parents']==[generator_identity] and set(record['outputs'])=={'linked.json'},
            'PRODUCTION_STAGE_BUILD_MISSING')
    require(hexpin(context.get('build_context_sha256')) and record['execution'].get('context_sha256')==context['build_context_sha256'], 'PRODUCTION_STAGE_CONTEXT_MISMATCH')
    item=record['outputs']['linked.json']
    require(type(linked) is bytes and len(linked)==item['size'] and
            hashlib.sha256(linked).hexdigest()==item['sha256'], 'PRODUCTION_STAGE_PARENT_MISMATCH')
    return True
