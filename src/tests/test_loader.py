"""Loader protocol/lifecycle checks; no game discovery or injection."""
import json
import os
from pathlib import Path
import struct
import sys
import types
import unittest
import tempfile
import copy
import re

ROOT = Path(__file__).resolve().parents[1]
os.environ['BATTLE_BGM_EXTEND_TEST'] = '1'

class Manager:
    menu_items = []
    def get_addon_info(self, name):
        return {'settings': {'enable': True, '自动注入': False}}

class LoaderTests(unittest.TestCase):
    def setUp(self):
        self.modules_before = {name: sys.modules.get(name) for name in
            ('functions', 'functions.base', 'functions.base.il2cpp_exports')}
        functions = types.ModuleType('functions')
        base = types.ModuleType('functions.base')
        ix = types.ModuleType('functions.base.il2cpp_exports')
        ix.resolve = lambda **kw: {'ok': True, 'mapping': {}}
        base.il2cpp_exports = ix
        functions.base = base
        sys.modules.update({'functions': functions, 'functions.base': base,
                            'functions.base.il2cpp_exports': ix})
        self.scope = {'__file__': str(ROOT / 'scr.py'),
                      'ADDON_ARG': {'AddonManager': Manager(), 'AddonName': 'BattleBGMExtend'}}
        exec(compile((ROOT / 'scr.py').read_text(encoding='utf-8'),
                     str(ROOT / 'scr.py'), 'exec'), self.scope)
        self.session = self.scope['Session'](os.getpid(), 'C:/fake/GameAssembly.dll')
        self.scope['_session'] = self.session

    def tearDown(self):
        self.session.close()
        for name, original in self.modules_before.items():
            if original is None:
                sys.modules.pop(name, None)
            else:
                sys.modules[name] = original

    def test_protocol_and_alias_order(self):
        self.assertEqual(len(self.session.memory), 14976)
        self.assertEqual(struct.unpack_from('<III', self.session.memory),
                         (0x31454742, 2, 512))
        self.assertEqual(len(self.scope['API_NAMES']), 34)
        for i, name in enumerate(self.scope['API_NAMES']):
            alias = self.session.memory[512+i*64:512+(i+1)*64].split(b'\0')[0]
            self.assertEqual(alias.decode(), name)

    def test_disable_is_immediate(self):
        self.session.update(True)
        self.scope['pause']()
        self.assertEqual(struct.unpack_from('<I', self.session.memory, 12)[0], 0)
        self.assertTrue(self.scope['_paused'])

    def test_unload_disables_without_waiting_for_game(self):
        self.session.update(True)
        self.scope['on_unload']()
        self.assertTrue(self.scope['STOP'].is_set())
        self.assertEqual(struct.unpack_from('<I', self.session.memory, 12)[0], 0)
        self.assertIsNone(self.scope['_thread'])

    def test_reconnect_preserves_native_status(self):
        struct.pack_into('<I', self.session.memory, 20, 1)
        struct.pack_into('<II', self.session.memory, 36, 123, 456)
        other = self.scope['Session'](os.getpid(), 'C:/fake/GameAssembly.dll')
        try:
            self.assertTrue(other.status()['hooked'])
            self.assertEqual((other.status()['changes'], other.status()['blocked']), (123,456))
        finally:
            other.close()

    def test_manual_connect_requests_retry(self):
        self.scope['_paused'] = True
        self.scope['connect']()
        self.assertFalse(self.scope['_paused'])
        self.assertTrue(self.scope['REQUEST'].is_set())
        self.assertTrue(self.scope['RETRY'].is_set())

    def test_canonical_export_file_is_complete(self):
        names = (ROOT / 'il2cpp-api-names.txt').read_text().splitlines()
        self.assertEqual(len(names), 234)
        self.assertEqual(len(set(names)), 234)
        self.assertTrue(set(self.scope['API_NAMES']) <= set(names))

    def test_published_rule_uses_verified_identity_keyword_and_event(self):
        payload = self.scope['load_rules']()
        self.assertEqual(len(payload), 9 * 384)
        identity, priority, keyword, path, name, bank, kind, threshold, source = struct.unpack('<ii64s128s48s64sii64s', payload[:384])
        self.assertEqual((identity, priority), (11016, 10000))
        self.assertEqual(keyword.rstrip(b'\0'), b'TransformSinclair')
        self.assertEqual(path.rstrip(b'\0'), b'event:/BGM/Battle/Battle_Cp10_Boss_10')
        self.assertEqual(bank.rstrip(b'\0'), b'BGM_Default_S8_3')
        self.assertEqual(self.session.memory[2688:2688+len(payload)], payload)
        sequence = struct.unpack_from('<I', self.session.memory, 68)[0]
        self.assertGreater(sequence, 0)
        self.assertEqual(sequence % 2, 0)

    def write_rules(self, document):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'rules.json'
            path.write_text(json.dumps(document), encoding='utf-8')
            return self.scope['load_rules'](path)

    def test_rejects_invalid_rules_before_enabling(self):
        base = json.loads((ROOT / 'rules.json').read_text(encoding='utf-8'))
        mutations = [
            lambda d: d.update(schema_version=2),
            lambda d: d['rules'][0].update(personality_id=True),
            lambda d: d['rules'][0].update(priority=2**31),
            lambda d: d['rules'][0].update(priority='100'),
            lambda d: d['rules'][0].update(bgm=''),
            lambda d: d['rules'][0].update(bgm='event:/BGM/Battle/'),
            lambda d: d['rules'][0].update(bgm='event:/BGM/Lobby/Test'),
            lambda d: d['rules'][0].update(bank='../other'),
            lambda d: d['rules'][0]['condition'].update(type='unknown'),
            lambda d: d['rules'][0]['condition'].update(keyword='x'*64),
            lambda d: d['rules'].append(copy.deepcopy(d['rules'][0])),
            lambda d: d.update(rules=d['rules']*33),
        ]
        for change in mutations:
            with self.subTest(change=mutations.index(change)):
                doc = copy.deepcopy(base)
                change(doc)
                with self.assertRaises(ValueError):
                    self.write_rules(doc)

    def test_disabled_rules_and_full_paths(self):
        doc = json.loads((ROOT / 'rules.json').read_text(encoding='utf-8'))
        doc['rules'] = doc['rules'][:1]
        doc['rules'][0]['enabled'] = False
        self.assertEqual(self.write_rules(doc), b'')
        doc['rules'][0].update(enabled=True, bgm='event:/BGM/Battle/Battle_Cp10_Boss_10', priority=-1)
        self.assertEqual(struct.unpack_from('<i', self.write_rules(doc), 4)[0], -1)

    def test_all_nine_conditions_and_priorities(self):
        payload = self.scope['load_rules']()
        rows = [struct.unpack_from('<ii64s128s48s64sii64s', payload, i*384) for i in range(9)]
        self.assertEqual([r[1] for r in rows], [10000,9000,8800,8700,8600,8500,8000,7000,6000])
        self.assertEqual([r[0] for r in rows], [11016,10415,10115,10916,11115,10614,10314,10512,10915])
        self.assertEqual([r[6] for r in rows], [0,0,1,3,0,2,0,0,0])
        self.assertEqual(rows[2][7], 9)
        self.assertEqual(rows[3][8].rstrip(b'\0'), b'FutureEyeOnRodion')
        self.assertEqual(rows[6][2].rstrip(b'\0'), b'Blandishment_Shin')
        self.assertEqual(rows[2][3].rstrip(b'\0'), b'event:/BGM/Battle/Battle_Cp9_Boss_10')
        self.assertEqual(rows[7][3].rstrip(b'\0'), b'event:/BGM/Battle/Battle_Cp8_Boss_2')

    def test_timed_condition_validation(self):
        base = json.loads((ROOT / 'rules.json').read_text(encoding='utf-8'))
        for kind, extras in [('round_start_buff_stack_equals', {}),
                             ('round_start_buff_stack_equals', {'stack': True}),
                             ('round_start_buff_stack_equals', {'stack': -1}),
                             ('round_end_buff_transition', {}),
                             ('round_end_buff_transition', {'from_keyword': ''})]:
            doc = copy.deepcopy(base)
            doc['rules'][0]['condition'] = dict(type=kind, keyword='TransformSinclair', **extras)
            with self.subTest(condition=doc['rules'][0]['condition']):
                with self.assertRaises(ValueError): self.write_rules(doc)

    def test_method_requirements_match_extracted_game_schema(self):
        source = (ROOT / 'battlebgmextend.c').read_text(encoding='utf-8')
        schema = json.loads((ROOT / 'tests/game-schema.json').read_text())
        classes = re.findall(r'CLASS\((\w+),"([^"]*)","([^"]+)"\)', source)
        for var, ns, name in classes:
            self.assertEqual((schema[var]['namespace'], schema[var]['name']), (ns, name))
        requirements = re.findall(r'\b(METHOD|HOOK)\(([^;\n]+)\);', source)
        self.assertGreater(len(requirements), 30)
        constants = dict(I='System.Int32', B='System.Boolean', S='System.String',
                         F='System.Single', V='System.Void', EV='FMOD.Studio.EventInstance')
        def expand(s):
            return ''.join(constants.get(token, token[1:-1])
                           for token in re.findall(r'"[^"]*"|\w+', s))
        for kind, body in requirements:
            args = body.split(',')
            if kind == 'METHOD':
                _, klass, name, count, static, result, *params = args
            else:
                klass, name, count, static, result, detour, original, save, *params = args
            expected = dict(name=name.strip('"'), argc=int(count), static=static == 'true',
                            result=expand(result), params=list(map(expand, params)))
            with self.subTest(method=expected['name'], klass=klass):
                self.assertIn(expected, schema[klass]['methods'])

if __name__ == '__main__':
    unittest.main(verbosity=2)
