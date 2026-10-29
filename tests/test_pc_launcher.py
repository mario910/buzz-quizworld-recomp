import copy
from dataclasses import asdict
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from launcher import model
from launcher.devices import Gamepad, State, Pads
import ctypes


def sfo(title='BCES00647'):
    key=b'TITLE_ID\0'
    value=title.encode()+b'\0'
    return b'\0PSF'+struct.pack('<IIII',0x101,36,36+len(key),1)+struct.pack('<HHIII',0,0x204,len(value),len(value),0)+key+value


class LauncherTests(unittest.TestCase):
    def test_english_defaults_and_legacy_labels_preserve_custom_input(self):
        settings = model.Settings()
        self.assertEqual(model.BUTTONS, ('Red', 'Yellow', 'Green', 'Orange', 'Blue'))
        self.assertEqual(settings.players[7].name, 'Player 8')
        legacy = asdict(settings)
        legacy['players'][0]['name'] = 'Gracz 1'
        legacy['players'][1]['name'] = 'Łukasz'
        expected_keys = copy.deepcopy(legacy['players'][0]['keys'])
        loaded = model.from_dict(legacy)
        self.assertEqual(loaded.players[0].name, 'Player 1')
        self.assertEqual(loaded.players[1].name, 'Łukasz')
        self.assertEqual(loaded.players[0].keys, expected_keys)
        self.assertEqual(legacy['players'][0]['name'], 'Gracz 1')
        with self.assertRaisesRegex(ValueError, 'Unknown key'):
            model.key_code('not-a-key')
        settings.places = 3
        with self.assertRaisesRegex(ValueError, 'player slots'):
            settings.validate()

    def test_keys(self):
        for code in range(1,256):
            self.assertEqual(model.key_code(model.key_label(code)),code)
        self.assertEqual(model.key_code('NONE'),0)
        self.assertEqual(model.key_code('4'),52)
        self.assertEqual(model.key_code('0x04'),4)
        for bad in ('', 'ZZ', '-1', 'F25', '0x100', 'NONE\0garbage'):
            with self.assertRaises(ValueError):model.key_code(bad)

    def test_serialization_and_isolation(self):
        settings=model.Settings()
        settings.players[0].name='Adam ąę'
        settings.pads[3].player=None
        settings.places=8
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            (root/'buzz-paths.json').write_text('{"unrelated":"keep me"}')
            model.save(settings,root)
            self.assertEqual(asdict(model.load(root)),asdict(settings))
            self.assertEqual(json.loads((root/'buzz-paths.json').read_text()),{'unrelated':'keep me'})
            self.assertFalse(list((root/'runtime/launcher').glob('*.tmp')))
            with patch('launcher.model.os.replace',side_effect=OSError('synthetic disk failure')):
                with self.assertRaises(OSError):model.save(model.Settings(),root)
            self.assertEqual(asdict(model.load(root)),asdict(settings))
            self.assertFalse(list((root/'runtime/launcher').glob('*.tmp')))

    def test_import_only_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            (root/'buzz-paths.json').write_text(json.dumps(dict(game_root='A',dev_flash='B',eboot_elf='C',_comment='preserve')))
            settings=model.load(root)
            self.assertEqual(settings.paths,dict(game_root='A',dev_flash='B',eboot_elf='C'))
            self.assertFalse((root/'runtime').exists())

    def test_bad_settings(self):
        for mutate in (
            lambda s:setattr(s,'version',9),lambda s:setattr(s,'places',5),
            lambda s:setattr(s,'places',True),lambda s:setattr(s,'pulse_ms',0),
            lambda s:setattr(s,'offline','false'),lambda s:s.players.pop(),
            lambda s:s.players[0].keys.__setitem__(1,'Space'),
            lambda s:s.pads[1].__setattr__('player',0),
            lambda s:s.pads[0].buttons.__setitem__(1,'START'),
            lambda s:s.players[0].__setattr__('name','Bad\nName')):
            settings=model.Settings();mutate(settings)
            with self.assertRaises(ValueError):settings.validate()
        with self.assertRaises(ValueError):model.from_dict({'version':1})
        with self.assertRaises(ValueError):model.from_dict([])

    def test_preview(self):
        s=model.Settings();s.places=8;s.gamepads=True
        s.pads[0].player=7
        masks=model.preview(s,{32},[0x4000,0,0,0])
        self.assertEqual(masks[0],1)
        self.assertEqual(masks[7],16)
        self.assertEqual(model.preview(s,set(),[0]*4),[0]*8)
        s.gamepads=False
        self.assertEqual(model.preview(s,set(),[0xffff]*4),[0]*8)

    def test_warnings(self):
        s=model.Settings();self.assertEqual(s.warnings(),[])
        s.players[1].keys[0]='Space'
        self.assertTrue(any('Keys shared' in text for text in s.warnings()))
        s.places=8
        self.assertTrue(any('Player 8' in text for text in s.warnings()))

    def test_sfo_bounds(self):
        self.assertEqual(model.title_id(sfo()),'BCES00647')
        for bad in (b'',b'\0PSF',sfo()[:30],sfo()[:-4],sfo().replace(b'TITLE_ID',b'BAD_ID__')):
            with self.assertRaises((ValueError,UnicodeError)):model.title_id(bad)

    def test_file_validation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            (root/'game/PS3_GAME/USRDIR').mkdir(parents=True)
            (root/'game/PS3_GAME/PARAM.SFO').write_bytes(sfo())
            (root/'flash/data/font').mkdir(parents=True)
            elf=root/'EBOOT.elf';elf.write_bytes(b'synthetic test ELF')
            (root/'build-windows').mkdir()
            (root/'build-windows/BuzzQuizWorldRecomp.exe').write_bytes(b'not an executable')
            settings=model.Settings(paths=dict(game_root=str(root/'game'),dev_flash=str(root/'flash'),eboot_elf=str(elf)))
            digest=hashlib.sha256(elf.read_bytes()).hexdigest()
            model.validate_files(settings,root,expected_hash=digest)
            with self.assertRaises(ValueError):model.validate_files(settings,root)
            (root/'game/PS3_GAME/PARAM.SFO').write_bytes(sfo('BCES00367'))
            with self.assertRaises(ValueError):model.validate_files(settings,root,expected_hash=digest)

    def test_launch_plan_literal_paths(self):
        with tempfile.TemporaryDirectory(prefix='Buzz ą & ! ') as temporary:
            root=Path(temporary)
            settings=model.Settings();settings.places=8;settings.pads[0].player=None
            settings.paths=dict(game_root='D:/Own game & more',dev_flash='D:/flash',eboot_elf='D:/update/EBOOT.elf')
            inherited={'SPURS_JOB_LIST_WORKERS':'4','BUZZ_KEYS':'1,2,3,4,5','BUZZ_SCRIPT':'12:1','BUZZ_PULSE_MS':'10'}
            original=copy.deepcopy(inherited)
            command,env=model.plan_launch(settings,root,inherited)
            self.assertEqual(inherited,original)
            self.assertEqual(env['SPURS_JOB_LIST_WORKERS'],'4')
            self.assertNotIn('BUZZ_SCRIPT',env);self.assertNotIn('BUZZ_KEYS',env);self.assertNotIn('BUZZ_PULSE_MS',env)
            self.assertEqual(env['BUZZ_OFFLINE'],'1')
            self.assertIn('-Background',command)
            paths=Path(command[command.index('-PathsConfig')+1])
            inputs=Path(command[command.index('-InputConfig')+1])
            self.assertEqual(json.loads(paths.read_text()),settings.paths)
            self.assertIn('gamepad.1=NONE',inputs.read_text())
            self.assertNotIn('-Command',command)
            self.assertFalse((root/'buzz-paths.json').exists())
            second,_=model.plan_launch(settings,root,{})
            self.assertNotEqual(second,command)

    def test_device_layout_and_no_poll_when_inactive(self):
        self.assertEqual(ctypes.sizeof(Gamepad),12)
        self.assertEqual(ctypes.sizeof(State),16)
        with patch.object(Pads,'__init__',lambda self:None):
            pads=Pads();pads.api=lambda *args:(_ for _ in ()).throw(AssertionError('polled inactive'))
            self.assertEqual(pads.poll(False),[0]*4)

    @unittest.skipUnless(os.name=='nt','Windows PowerShell bridge')
    def test_windows_powershell_bridge(self):
        import subprocess
        with tempfile.TemporaryDirectory(prefix='Buzz ą & ! ') as temporary:
            base=Path(temporary)
            paths=base/'paths.json';inputs=base/'input.ini'
            paths.write_text(json.dumps(dict(game_root='D:/Gra żółta & literalna!',dev_flash='D:/firmware',eboot_elf='D:/Eboot ąć & !.elf'),ensure_ascii=False),encoding='utf-8')
            inputs.write_text(model.Settings().input_ini(),encoding='utf-8')
            fixture=Path(__file__).resolve().with_name('launcher-bridge-test.ps1')
            result=subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',str(fixture),'-InputConfig',str(inputs),'-PathsConfig',str(paths)],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=20)
            self.assertEqual(result.returncode,0,result.stderr.decode('utf-8',errors='replace'))
            self.assertIn(b'no game launched',result.stdout)

    def test_runtime_ini_oracle(self):
        oracle=os.environ.get('BUZZ_CONFIG_ORACLE')
        if not oracle:self.skipTest('Set BUZZ_CONFIG_ORACLE via tests/scripts/test-pc-launcher.ps1 for native parser parity')
        import subprocess
        for places in (4,8):
            settings=model.Settings();settings.places=places;settings.pads[0].player=None
            settings.players[7].keys=['F1','F2','F3','F4','F5']
            result=subprocess.run([oracle],input=settings.input_ini(),text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            parsed=json.loads(result.stdout)
            self.assertEqual(parsed['players'],places)
            self.assertEqual(parsed['keys'],[[model.key_code(k) for k in p.keys] for p in settings.players])
            self.assertEqual(parsed['pad_players'],[-1,1,2,3])

    def test_native_adapter_unicode_config(self):
        adapter=os.environ.get('BUZZ_INPUT_ADAPTER_CHECK')
        if not adapter:self.skipTest('Set BUZZ_INPUT_ADAPTER_CHECK via tests/scripts/test-pc-launcher.ps1')
        import subprocess
        with tempfile.TemporaryDirectory(prefix='Buzz żółć 汉字 & ! ') as temporary:
            settings=model.Settings();settings.places=8;settings.pads[0].player=None
            config=Path(temporary)/'input.ini';config.write_text(settings.input_ini(),encoding='utf-8')
            env=dict(os.environ)
            for key in list(env):
                if key.upper().startswith('BUZZ_KEYS') or key.upper()=='BUZZ_PULSE_MS':del env[key]
            env['BUZZ_INPUT_CONFIG']=str(config)
            result=subprocess.run([adapter,'8'],env=env,capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=10)
            self.assertEqual(result.returncode,0,result.stderr.decode('utf-8',errors='replace'))
            self.assertIn(b'no window/game',result.stdout)


if __name__=='__main__':unittest.main()
