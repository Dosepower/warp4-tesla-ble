"""Production snapshot assembly/execution. Approval is the external runtime pin.

The runtime payload may declare a snapshot_recipe; its snapshot digest is NOT
self-authorizing: the entire payload must already pass external approval. No
runtime/approval payload is included inside its own snapshot inventory.
"""
import hashlib
import os
from pathlib import Path, PurePosixPath
import sys
import time
import uuid
from installer_sdk_verify import require, hexpin, validate_inventory, loads
from installer_snapshot import Snapshot, verify_bootstrap, canonical

ROLE_LAYOUT = {
    "platform": "/snapshot/platformio/platforms/espressif32",
    "arduino": "/snapshot/platformio/packages/framework-arduinoespressif32",
    "toolchain": "/snapshot/platformio/packages/toolchain-xtensa-esp-elf",
    "esptool": "/snapshot/platformio/packages/tool-esptoolpy",
    "python_environment": "/snapshot/python",
    "firmware_libdeps": "/snapshot/libdeps",
    "web_dependencies": "/snapshot/project/web/node_modules",
    "sdk_builder_sources": "/snapshot/sdk_builder_sources",
    "sdk_idf_sources": "/snapshot/sdk_idf_sources",
    "platformio_core": "/snapshot/platformio",
    "node_environment": "/snapshot/node_environment",
    "system_tools": "/snapshot/system_tools",
    "loader_runtime": "/snapshot/loader_runtime",
}


def assemble_inputs(verified, software):
    software = Path(software)
    require(software.is_absolute(), "RECIPE_SOURCE_ROOT")
    roots = verified["runtime"]["roots"]
    require(type(roots) is list and len(roots) == len(ROLE_LAYOUT) and
            {r["role"] for r in roots} == set(ROLE_LAYOUT), "RECIPE_ROLES_INCOMPLETE")
    result = {}
    def add(target, path, pin, mode, role):
        item = {"path":str(path),"sha256":pin,"mode":mode,"role":role}
        require(target not in result, "RECIPE_TARGET_COLLISION: " + target)
        result[target] = item
    sources = verified["source_inventory"]
    require(type(sources) is dict and bool(sources), "RECIPE_SOURCE_INVENTORY_REQUIRED")
    for relative, pin in sorted(sources.items()):
        p = PurePosixPath(relative)
        require(str(p) == relative and not p.is_absolute() and ".." not in p.parts and hexpin(pin), "RECIPE_SOURCE_PATH")
        if relative.startswith("software/"):
            target = "/snapshot/project/" + relative[len("software/"):]
        else:
            target = "/snapshot/repository/" + relative
        path = software.parent / relative
        add(target,path,pin,0o555 if path.stat().st_mode & 0o111 else 0o444,"source")
    for root in roots:
        validate_inventory(root["inventory"])
        require(bool(root["inventory"]), "RECIPE_EMPTY_RUNTIME_ROLE")
        for relative, item in sorted(root["inventory"].items()):
            add(ROLE_LAYOUT[root["role"]]+"/"+relative,Path(root["path"])/relative,
                item["sha256"],0o555 if item["mode"] & 0o111 else 0o444,root["role"])
    sdk = verified["sdk_snapshot"]
    validate_inventory(sdk["inventory"])
    require(bool(sdk["inventory"]), "RECIPE_EMPTY_SDK")
    prefix="/snapshot/platformio/packages/framework-arduinoespressif32-libs/"
    for relative,item in sorted(sdk["inventory"].items()):
        add(prefix+relative,Path(sdk["path"])/relative,item["sha256"],
            0o555 if item["mode"] & 0o111 else 0o444,"sdk")
    require(type(verified["identity_snapshot"]) is dict and bool(verified["identity_snapshot"]), "RECIPE_IDENTITY_REQUIRED")
    for relative,item in verified["identity_snapshot"].items():
        p=PurePosixPath(relative)
        require(str(p)==relative and p.is_relative_to("signature") and ".." not in p.parts,
                "RECIPE_IDENTITY_PATH")
        target="/snapshot/project/"+relative
        if target in result:
            require(result[target]["sha256"] == item["sha256"], "RECIPE_IDENTITY_SOURCE_COLLISION")
            del result[target]
        add(target,item["path"],item["sha256"],0o444,"official_identity")
    return result


def bootstrap_actual(recipe):
    # This is a check made under the protected-launcher trust assumption, not a
    # retroactive proof of the initial interpreter/kernel. Loaded Python source
    # and mapped runtime libraries are included, not only three executable names.
    approved=recipe["bootstrap"]
    require(type(approved) is dict and "bwrap" in approved, "RECIPE_BOOTSTRAP_REQUIRED")
    require("strace" in approved,"RECIPE_TRACER_BOOTSTRAP_REQUIRED")
    actual={"python":str(Path(sys.executable).resolve()), "git":"/usr/bin/git",
            "bwrap":approved["bwrap"]["path"],"strace":approved["strace"]["path"]}
    for name,module in sorted(sys.modules.items()):
        path=getattr(module,"__file__",None)
        if path and Path(path).is_file():
            actual["module:"+name]=str(Path(path).resolve())
    for line in Path("/proc/self/maps").read_text().splitlines():
        parts=line.split(None,5)
        if len(parts)==6 and parts[5].startswith("/"):
            path=parts[5]
            require(not path.endswith(" (deleted)"), "BOOTSTRAP_DELETED_MAPPING")
            actual["mapping:"+path]=str(Path(path).resolve())
    return actual


def check_bootstrap(recipe):
    require(type(recipe) is dict and set(recipe)=={"schema","snapshot_sha256","elf_binding","bootstrap"} and
            recipe["schema"]=="warp4-snapshot-recipe-v1" and hexpin(recipe["snapshot_sha256"]), "RECIPE_SCHEMA")
    observed=bootstrap_actual(recipe)
    approved=recipe["bootstrap"]
    require(set(observed)<=set(approved) and
            all(approved[k]["path"]==v for k,v in observed.items()),"BOOTSTRAP_UNAPPROVED_LOADED_ROLE")
    # The external approval can predeclare later imports. Check ALL declared
    # bytes now, while every already loaded role must match its observed path.
    verify_bootstrap(approved,{k:v["path"] for k,v in approved.items()})


def execute(environment, verified, software):
    from installer_output import decode_output
    from installer_ota_verify import verify_container
    from installer_identity import verify_elf_key, verify_app_key
    from installer_ota import stage_ota, make_container
    from installer_generation import StageDAG
    from installer_policy import ENVS
    recipe=verified['snapshot_recipe']
    check_bootstrap(recipe)
    inputs=assemble_inputs(verified,software)
    pair=stage_inputs(inputs,verified['runtime'].get('stage_policy'))
    selected=pair[ENVS.index(environment)]
    with Snapshot(selected) as snap:
        # The B/full inventory pin also authenticates the policy-filtered A set.
        with Snapshot(inputs) as complete:
            require(complete.identity==recipe['snapshot_sha256'], 'RECIPE_SNAPSHOT_APPROVAL_MISMATCH')
        context={k:v for k,v in verified.items() if k not in
                 ('runtime','source_inventory','sdk_snapshot','identity_snapshot','snapshot_recipe','read_only_roots')}
        context.update(environment=environment,build_id=uuid.uuid4().hex,start_ns=time.time_ns(),stage_phase='build')
        package_names={'framework-arduinoespressif32':'arduino','toolchain-xtensa-esp-elf':'toolchain','tool-esptoolpy':'esptool'}
        context['package_roots']={name:ROLE_LAYOUT[role] for name,role in package_names.items()}
        context['package_roots']['framework-arduinoespressif32-libs']='/snapshot/platformio/packages/framework-arduinoespressif32-libs'
        context['vfs_sources']={'builder':ROLE_LAYOUT['sdk_builder_sources'],'idf':ROLE_LAYOUT['sdk_idf_sources']}
        python='/snapshot/python/bin/python'
        script='/snapshot/project/installer_runner.py'
        policy={script:{'interpreter':python,'arguments':['-I','-B']}}
        def run_context():
            return snap.context({'environment':environment,'elf_binding':recipe['elf_binding'],
                                 'private_workspace':True,'verified':context})
        with StageDAG(recipe['bootstrap']['bwrap']['path'],snap,recipe['elf_binding']) as dag:
            context['stage_phase']='generate'
            generator_script='/snapshot/project/installer_generator_entry.py'
            generator_policy={generator_script:{'interpreter':python,'arguments':['-I','-B']}}
            fd=run_context()
            try:
                generated=dag.run('generate',[python,'-I','-B',generator_script,environment],['generated.json'],
                                  named=True,workspace=True,script_policy=generator_policy,context_fd=fd)
            finally: os.close(fd)
            from installer_generators import decode_generated
            generated_data=generated.read('generated.json')
            generated_value,generated_files=decode_generated(generated_data,
                context | {'snapshot_sha256':snap.identity},snap.manifest['files'])
            context['generator_output_sha256']=hashlib.sha256(generated_data).hexdigest()
            context['generator_context_sha256']=generated.record['execution']['context_sha256']
            context['web_outputs']={p:hashlib.sha256(generated_files[p]).hexdigest() for p in
                ('src/index_html.embedded.cpp','src/index_html.embedded.h')}
            context['stage_phase']='build'
            fd=run_context()
            try:
                linked=dag.run('build',[python,'-I','-B',script,environment,'build'],['linked.json'],
                               parents=['generate'],named=True,workspace=True,script_policy=policy,context_fd=fd)
            finally: os.close(fd)
            # Promotion is after complete namespace teardown. Never read a ledger
            # from linked.json or any build directory; the broker creates it.
            context['build_context_sha256']=linked.record['execution']['context_sha256']
            ledger_fd=dag.seal_evidence()
            context['stage_ledger']=loads(os.pread(ledger_fd,os.fstat(ledger_fd).st_size,0))
            context['stage_authority']={'snapshot_sha256':snap.identity,'heads':list(context['stage_ledger']['heads'])}
            exposure_policy=verified['runtime']['stage_policy']
            context['stage_closure']={'schema':'warp4-stage-closure-v1','common':dict(exposure_policy['common']),
                'tesla':{} if environment==ENVS[0] else dict(exposure_policy['tesla']),
                'manifest':snap.manifest,'records':context['stage_ledger']['records']}
            context['stage_phase']='finalize' 
            fd=run_context()
            try:
                result=dag.run('posthook',[python,'-I','-B',script,environment,'finalize'],['export.json'],
                               parents=['generate','build'],named=True,workspace=True,script_policy=policy,context_fd=fd)
            finally: os.close(fd)
            names=["firmware-UNSIGNED_ota.bin","firmware_info.bin","manifest.json","firmware.elf","firmware.map","metadata.json"]
            exported=decode_output(result.read("export.json").decode(),names,snap.identity,context["build_id"])
            ota=exported["firmware-UNSIGNED_ota.bin"]
            app=ota[0xf000:]
            verify_container(ota,app)
            require(exported["firmware_info.bin"]==ota[0xc000:0xd000],"EXPORTED_INFO_MISMATCH")
            address,key=verify_elf_key(exported["firmware.elf"],context["public_key_sha256"])
            verify_app_key(app,address,key,context["public_key_sha256"])
            manifest=loads(exported["manifest.json"])
            provenance=manifest["provenance"]
            expected=context | {"snapshot_sha256":snap.identity}
            require(all(canonical(provenance.get(k))==canonical(v) for k,v in expected.items()),"EXPORTED_PROVENANCE_MISMATCH")
            require(provenance["map_sha256"]==hashlib.sha256(exported["firmware.map"]).hexdigest() and
                    provenance["elf_sha256"]==hashlib.sha256(exported["firmware.elf"]).hexdigest(), "EXPORTED_LINK_EVIDENCE_MISMATCH")
            metadata=loads(exported["metadata.json"])
            require(make_container(app,metadata)==ota,"EXPORTED_METADATA_MISMATCH")
            from installer_stage_verify import verify_production_stage
            from installer_output import LinkedProducts
            from installer_link_verify import verify_linkgraph
            full_context=context | {'snapshot_sha256':snap.identity}
            verify_production_stage(full_context,linked.read('linked.json'))
            view=LinkedProducts(linked.read('linked.json'),full_context,snap.manifest,
                lambda p: os.pread(snap.files[p],snap.manifest['files'][p]['size'],0),generated_data=generated_data)
            program=view.output+'/warp4-ota-candidate-'+context['build_id']
            require(exported['firmware.elf']==view.read(program+'.elf') and
                    exported['firmware.map']==view.read(program+'.map') and
                    exported['metadata.json']==view.read(view.output+'/metadata.json') and
                    app==view.read(program+'.bin'), 'EXPORTED_STAGE_PARENT_MISMATCH')
            require(metadata==generated_value['metadata'],'EXPORTED_GENERATOR_METADATA_MISMATCH')
            verify_linkgraph(environment,exported['firmware.map'].decode(),view.closure,sealed_view=view)
            from installer_policy import validate_effective
            validate_effective(view.flags,False,approved=context['effective_flags'][environment])
            evidence={k:exported[k] for k in ('firmware.elf','firmware.map','metadata.json')}
            evidence.update({'host-stage-dag.json':canonical(dag.evidence()),'linked.json':linked.read('linked.json'),
                             'build.stdout':linked.stdout.encode(),'build.stderr':linked.stderr.encode(),
                             'posthook.stdout':result.stdout.encode(),'posthook.stderr':result.stderr.encode(),
                             'generated.json':generated_data,'generate.stdout':generated.stdout.encode(),'generate.stderr':generated.stderr.encode()})
            destination=Path(software)/"build/installer"/environment/context["build_id"]
            stage_ota(None,destination,metadata,context["start_ns"],provenance,app_bytes=app,evidence=evidence)
            return 0


def stage_inputs(inputs, policy):
    # Authority is the externally approved runtime payload, never a stage output.
    require(type(policy) is dict and set(policy)=={"schema","common","tesla"} and
            policy["schema"]=="warp4-production-stage-policy-v1", "STAGE_PRODUCER_POLICY")
    common,delta=policy["common"],policy["tesla"]
    require(type(common) is dict and bool(common) and type(delta) is dict and
            not set(common)&set(delta) and
            common|delta=={p:i["sha256"] for p,i in inputs.items()}, "STAGE_PRODUCER_EXPOSURE")
    require(any(i["role"]=="sdk" for i in inputs.values()) and
            any(i["role"]=="official_identity" for i in inputs.values()), "STAGE_PRODUCER_AUTHORITY")
    require(all(inputs[p]["role"] in ("source","firmware_libdeps") and
                any(t in p.lower() for t in ("tesla_vehicle","teslable","tesla_ble")) for p in delta),
            "STAGE_PRODUCER_DELTA_AUTHORITY")
    require(not any(any(t in p.lower() for t in ("tesla_vehicle","teslable","tesla_ble")) and
                    inputs[p]["role"] in ("source","firmware_libdeps") for p in common),
            "STAGE_PRODUCER_A_TESLA_EXPOSURE")
    return {p:inputs[p] for p in common},dict(inputs)
