#!/usr/bin/env python3
"""Exact frontend entry/dependency AST + digest helper definitions; no package tools."""
import ast
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import tomllib
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'software/tools'))
import bench_confinement as confinement
from test_r10_confinement import state


def definitions():
    path=ROOT/'software/util.py'
    names={'get_digest_paths','check_digest','remove_digest','store_digest','get_env_metadata'}
    nodes=[n for n in ast.parse(path.read_text()).body if isinstance(n,ast.FunctionDef) and n.name in names]
    assert len(nodes)==5
    writes=[]
    def write(path,content):
        writes.append(str(path))
        if content is not None:Path(path).write_text(content)
    scope={'os':os,'hashlib':hashlib,'json':json,'__file__':str(path),'tfutil':SimpleNamespace(write_file_if_different=write),'write_generated_file':write}
    exec(compile(ast.Module(body=nodes,type_ignores=[]),str(path),'exec'),scope)
    return scope,writes


def entry_and_frontend(pair):
    path=ROOT/'software/pio_hooks.py'
    main=next(n for n in ast.parse(path.read_text()).body if isinstance(n,ast.FunctionDef) and n.name=='main')
    assert isinstance(main.body[0],ast.Assign) and isinstance(main.body[1],ast.Expr)
    entry=main.body[:2]
    if pair=='node_modules':
        start=next(i for i,n in enumerate(main.body) if isinstance(n,ast.Expr) and ast.unparse(n).startswith("util.log('Checking web interface dependencies')"))
        stop=next(i for i,n in enumerate(main.body) if isinstance(n,ast.Expr) and ast.unparse(n)=="util.log('Checking web interface')")
        block=main.body[start:stop]
    else:
        start=next(i for i,n in enumerate(main.body) if isinstance(n,ast.Assign) and any(isinstance(x,ast.Name) and x.id=='index_html_needs_update' for x in ast.walk(n.targets[0])))
        block=main.body[start:start+2]
    assert any(isinstance(n,ast.If) for n in block)
    return entry,block


class DigestTests(unittest.TestCase):
    def setUp(self):
        temp=tempfile.TemporaryDirectory(prefix='r12-digest-');self.addCleanup(temp.cleanup)
        self.root=Path(temp.name);self.project=self.root/'software';self.build=self.project/'.pio/build/bench'
        self.build.mkdir(parents=True);(self.project/'web').mkdir();(self.project/'src').mkdir()
        self.victim=self.root/'victim';self.victim.write_bytes(b'UNRELATED PRODUCTION BYTES\n')
        (self.project/'web/package-lock.json').write_text('{"fixture":true}')
        (self.project/'pyproject.toml').write_text('[tool.pio-hooks]\nrequires-node="fixture"\nrequires-npm="fixture"\n')
        self.paths=[self.build/'web,node_modules.digest',self.project/'web/node_modules.digest',self.build/'src,index_html.digest',self.project/'src/index_html.digest']
        for p in self.paths:p.write_text('old stale digest')
        project,build=self.project,self.build
        self.env=SimpleNamespace(subst=lambda s:{'$PROJECT_DIR':str(project),'$BUILD_DIR':str(build)}[s])
        self.oldcwd=Path.cwd();os.chdir(self.project);self.addCleanup(os.chdir,self.oldcwd)
        self.helpers,self.writes=definitions()

    def unsafe(self,path,kind):
        path.unlink()
        if kind=='symlink':path.symlink_to(self.victim)
        elif kind=='dangling':path.symlink_to(self.root/'missing')
        elif kind=='hardlink':os.link(self.victim,path)
        else:path.mkdir()

    def execute(self,pair='node_modules',output_plan=None):
        calls=[]
        class StopBeforeTools(Exception):pass
        def remove(*a,**kw):
            calls.append(a)
            self.helpers['remove_digest'](*a,**kw)
            raise StopBeforeTools()
        util=SimpleNamespace(log=lambda _:None,check_digest=self.helpers['check_digest'],get_digest_paths=self.helpers['get_digest_paths'],remove_digest=remove)
        scope={'pathlib':__import__('pathlib'),'output_plan':output_plan or confinement.output_plan,'env':self.env,'tomllib':tomllib,'util':util,'os':os,'index_html_src_paths':[],'index_html_src_datas':[]}
        entry,block=entry_and_frontend(pair)
        exec(compile(ast.Module(body=entry,type_ignores=[]),'exact-pio-entry','exec'),scope)
        with self.assertRaises(StopBeforeTools):exec(compile(ast.Module(body=block,type_ignores=[]),'exact-pio-frontend','exec'),scope)
        self.assertEqual(calls,[(('web' if pair=='node_modules' else 'src'),pair)])

    def test_all_four_leaves_reject_before_any_entry_or_dependency_mutation(self):
        for index in range(4):
            for kind in ('symlink','dangling','hardlink','directory'):
                with self.subTest(leaf=index,kind=kind):
                    p=self.paths[index];self.unsafe(p,kind);before=state(self.root)
                    with patch('os.remove',wraps=os.remove) as remove:
                        with self.assertRaises(ValueError):self.execute()
                        remove.assert_not_called()
                    self.assertEqual(state(self.root),before)
                    if p.is_dir() and not p.is_symlink():p.rmdir()
                    else:p.unlink()
                    p.write_text('old stale digest')

    def test_ordinary_node_modules_actual_update_removes_only_lexical_pair(self):
        self.execute();self.assertFalse(self.paths[0].exists());self.assertFalse(self.paths[1].exists())
        self.assertTrue(all(p.exists() for p in self.paths[2:]));self.assertEqual(self.victim.read_bytes(),b'UNRELATED PRODUCTION BYTES\n')

    def test_equivalent_index_html_actual_update_control(self):
        self.execute('index_html');self.assertTrue(all(p.exists() for p in self.paths[:2]));self.assertFalse(any(p.exists() for p in self.paths[2:]))
        self.assertEqual(self.victim.read_bytes(),b'UNRELATED PRODUCTION BYTES\n')

    def test_entry_identity_substitution_before_first_mutation(self):
        for index in range(4):
            with self.subTest(leaf=index):
                p=self.paths[index];captured=[]
                def injected(*a,**kw):
                    plan=confinement.output_plan(*a,**kw)
                    p.rename(p.with_name(p.name+'.saved'));p.symlink_to(self.victim);captured.append(state(self.root));return plan
                with self.assertRaises(ValueError):self.execute(output_plan=injected)
                self.assertEqual(state(self.root),captured[0]);p.unlink();p.with_name(p.name+'.saved').rename(p)

    def test_unsafe_ancestors_reject_before_mutation(self):
        for parent in (self.project/'web',self.build):
            with self.subTest(parent=parent):
                saved=parent.with_name(parent.name+'-saved');parent.rename(saved);parent.symlink_to(saved,target_is_directory=True)
                before=state(self.root)
                with self.assertRaises(ValueError):self.execute()
                self.assertEqual(state(self.root),before);parent.unlink();saved.rename(parent)

    def test_actual_helpers_preflight_complete_pair_before_remove_or_store(self):
        for pair,dst,indices in [('node_modules','web',(0,1)),('index_html','src',(2,3))]:
            for index in indices:
                for kind in ('symlink','hardlink','directory'):
                    for action in ('remove_digest','store_digest'):
                        with self.subTest(pair=pair,index=index,kind=kind,action=action):
                            p=self.paths[index];self.unsafe(p,kind);before=state(self.root);self.writes.clear()
                            with patch('os.remove',wraps=os.remove) as remove:
                                with self.assertRaises(ValueError):
                                    args=(dst,pair) if action=='remove_digest' else ('new-digest',dst,pair)
                                    self.helpers[action](*args,env=self.env)
                                remove.assert_not_called()
                            self.assertEqual(self.writes,[]);self.assertEqual(state(self.root),before)
                            if p.is_dir() and not p.is_symlink():p.rmdir()
                            else:p.unlink()
                            p.write_text('old stale digest')

    def test_helper_ordinary_store_and_read_control(self):
        for dst,pair,indices in [('web','node_modules',(0,1)),('src','index_html',(2,3))]:
            paths=self.helpers['get_digest_paths'](dst,pair,env=self.env)
            self.assertEqual(list(map(Path,paths)),[self.paths[i] for i in indices])
            self.helpers['store_digest']('new-digest',dst,pair,env=self.env)
            self.assertTrue(all(self.paths[i].read_text()=='new-digest' for i in indices))
            self.helpers['store_digest'](None,dst,pair,env=self.env)
        self.assertEqual(self.victim.read_bytes(),b'UNRELATED PRODUCTION BYTES\n')

    def test_helper_rejects_lexical_traversal_without_normalizing(self):
        for dst,var in [('web/../web','node_modules'),('web','../node_modules')]:
            with self.subTest(dst=dst,var=var):
                before=state(self.root)
                with self.assertRaises(ValueError):self.helpers['remove_digest'](dst,var,env=self.env)
                self.assertEqual(state(self.root),before)

    def test_direct_helper_captured_identity_substitution_rejects_before_unlink(self):
        target=self.paths[1];original=confinement.preflight;injected=[]
        def preflight(*a,**kw):
            plan=original(*a,**kw)
            if target in [Path(p) for p in kw.get('files',())] and not injected:
                target.rename(target.with_name('saved.digest'));target.symlink_to(self.victim);injected.append(state(self.root))
            return plan
        with patch.object(confinement,'preflight',side_effect=preflight),patch('os.remove',wraps=os.remove) as remove:
            with self.assertRaises(ValueError):self.helpers['remove_digest']('web','node_modules',env=self.env)
            remove.assert_not_called()
        self.assertEqual(state(self.root),injected[0])

    def test_metadata_environment_uses_same_paired_output_semantics(self):
        metadata=json.dumps({'project_dir':str(self.project),'build_dir':str(self.build)})
        with patch.dict(os.environ,PLATFORMIO_METADATA=metadata):
            self.assertEqual(list(map(Path,self.helpers['get_digest_paths']('web','node_modules'))),self.paths[:2])
            self.unsafe(self.paths[1],'symlink');before=state(self.root)
            with self.assertRaises(ValueError):self.helpers['store_digest']('new','web','node_modules')
            self.assertEqual(state(self.root),before)


if __name__=='__main__':unittest.main()
