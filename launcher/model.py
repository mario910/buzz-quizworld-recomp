"""UI-independent launcher settings, validation and launch planning."""
from __future__ import annotations

from dataclasses import asdict, dataclass, field
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
import uuid
from datetime import datetime

ROOT = Path(__file__).resolve().parents[1]
from launcher.paths import EXPECTED_ELF_SHA256 as ELF_SHA256
BUTTONS = ('Red', 'Yellow', 'Green', 'Orange', 'Blue')
PAD_BITS = {'START': 0x10, 'BACK': 0x20, 'A': 0x1000, 'B': 0x2000,
            'X': 0x4000, 'Y': 0x8000, 'LB': 0x100, 'RB': 0x200,
            'UP': 1, 'DOWN': 2, 'LEFT': 4, 'RIGHT': 8, 'LS': 0x40, 'RS': 0x80}
KEY_NAMES = {'SPACE': 32, 'ENTER': 13, 'TAB': 9, 'ESC': 27, 'BACKSPACE': 8,
             'UP': 38, 'DOWN': 40, 'LEFT': 37, 'RIGHT': 39,
             'LSHIFT': 160, 'RSHIFT': 161, 'LCTRL': 162, 'RCTRL': 163, 'NONE': 0}


def key_code(text: str) -> int:
    value = text.strip().upper()
    if value in KEY_NAMES:
        return KEY_NAMES[value]
    if len(value) == 1 and value.isascii() and value.isalnum():
        return ord(value)
    if value.startswith('F') and value[1:].isdigit() and 1 <= int(value[1:]) <= 24:
        return 111 + int(value[1:])
    try:
        number = int(value, 16) if value.startswith('0X') else int(value, 10)
    except ValueError:
        raise ValueError(f'Unknown key: {text!r}') from None
    if not 1 <= number <= 255:
        raise ValueError(f'Key code must be in the range 1–255: {text!r}')
    return number


def key_label(code: int) -> str:
    for name, number in KEY_NAMES.items():
        if number == code:
            return 'Space' if name == 'SPACE' else name
    if 48 <= code <= 57 or 65 <= code <= 90:
        return chr(code)
    if 112 <= code <= 135:
        return f'F{code - 111}'
    return f'0x{code:02X}'


@dataclass
class Player:
    name: str
    keys: list[str]


@dataclass
class Pad:
    player: int | None = None  # Zero-based logical player, not an XInput slot.
    buttons: list[str] = field(default_factory=lambda: ['RB', 'LEFT', 'DOWN', 'RIGHT', 'UP'])


@dataclass
class Settings:
    version: int = 1
    paths: dict[str, str] = field(default_factory=lambda: dict(game_root='', dev_flash='', eboot_elf=''))
    places: int = 4
    offline: bool = True
    hide_store: bool = True
    gamepads: bool = True
    background: bool = True
    pulse_ms: int = 250
    players: list[Player] = field(default_factory=lambda: [
        Player('Player 1', ['Space', '4', '3', '2', '1']),
        Player('Player 2', ['Q', 'W', 'E', 'R', 'T']),
        Player('Player 3', ['A', 'S', 'D', 'F', 'G']),
        Player('Player 4', ['Z', 'X', 'C', 'V', 'B']),
        *[Player(f'Player {n}', ['NONE'] * 5) for n in range(5, 9)]])
    pads: list[Pad] = field(default_factory=lambda: [Pad(n) for n in range(4)])

    def validate(self) -> None:
        if type(self.version) is not int or self.version != 1:
            raise ValueError('Unsupported launcher settings version.')
        if self.places not in (4, 8) or type(self.places) is not int:
            raise ValueError('The number of player slots must be 4 or 8.')
        if type(self.pulse_ms) is not int or not 10 <= self.pulse_ms <= 5000:
            raise ValueError('Pulse duration must be between 10 and 5000 ms.')
        if any(type(getattr(self, key)) is not bool for key in ('offline', 'hide_store', 'gamepads', 'background')):
            raise ValueError('Invalid toggle setting.')
        if set(self.paths) != {'game_root', 'dev_flash', 'eboot_elf'} or any(not isinstance(v, str) or '\0' in v for v in self.paths.values()):
            raise ValueError('Invalid game file paths.')
        if len(self.players) != 8 or len(self.pads) != 4:
            raise ValueError('Settings must contain 8 player slots and 4 controller slots.')
        for n, player in enumerate(self.players, 1):
            if not isinstance(player.name, str) or not 1 <= len(player.name.strip()) <= 32 or any(ord(c) < 32 for c in player.name):
                raise ValueError(f'Player {n}: name must contain 1–32 characters with no control characters.')
            if len(player.keys) != 5 or any(not isinstance(k, str) for k in player.keys):
                raise ValueError(f'Player {n}: five keys are required.')
            keys = [key_code(k) for k in player.keys]
            used = [k for k in keys if k]
            if len(used) != len(set(used)):
                raise ValueError(f'Player {n}: the same key is assigned to multiple buttons.')
        assigned = set()
        for n, pad in enumerate(self.pads, 1):
            if pad.player is not None:
                if type(pad.player) is not int or not 0 <= pad.player < 8:
                    raise ValueError(f'Controller {n}: invalid player number.')
                if pad.player in assigned:
                    raise ValueError('Assign at most one controller to each player in the launcher.')
                assigned.add(pad.player)
            if len(pad.buttons) != 5 or any(b not in PAD_BITS for b in pad.buttons) or len(set(pad.buttons)) != 5:
                raise ValueError(f'Controller {n}: choose five different buttons.')

    def warnings(self) -> list[str]:
        self.validate()
        messages, owners = [], {}
        for n, player in enumerate(self.players[:self.places]):
            for key in player.keys:
                code = key_code(key)
                if code:
                    owners.setdefault(code, set()).add(n + 1)
            has_pad = self.gamepads and any(p.player == n for p in self.pads)
            if not has_pad and any(not key_code(k) for k in player.keys):
                messages.append(f'{player.name}: some buttons have no input assigned.')
        shared = [key_label(k) for k, players in owners.items() if len(players) > 1]
        if shared:
            messages.append('Keys shared by multiple players: ' + ', '.join(shared))
        return messages

    def input_ini(self) -> str:
        self.validate()
        lines = ['# Generated by the PC launcher; red,yellow,green,orange,blue.',
                 f'players={self.places}', f'pulse_ms={self.pulse_ms}',
                 f'gamepads={int(self.gamepads)}', 'background_gamepads=0']
        # Canonical hex VK prevents ambiguity between a numeric code and a digit key.
        for n, player in enumerate(self.players, 1):
            codes = [key_code(k) for k in player.keys]
            lines.append(f'keyboard.{n}=' + ','.join(f'0x{k:02X}' if k else 'NONE' for k in codes))
        for n, pad in enumerate(self.pads, 1):
            value = 'NONE' if pad.player is None else ','.join([str(pad.player + 1), *pad.buttons])
            lines.append(f'gamepad.{n}={value}')
        return '\n'.join(lines) + '\n'


def from_dict(data: dict) -> Settings:
    if not isinstance(data, dict):
        raise ValueError('Settings must be a JSON object.')
    try:
        values = dict(data)
        values['players'] = [Player(**p) for p in values['players']]
        # Translate only the old auto-generated slot labels. Custom names and
        # mappings remain unchanged, and loading never rewrites the saved file.
        for n, player in enumerate(values['players'], 1):
            if player.name == f'Gracz {n}':
                player.name = f'Player {n}'
        values['pads'] = [Pad(**p) for p in values['pads']]
        settings = Settings(**values)
        settings.validate()
        return settings
    except (KeyError, TypeError, AttributeError) as exc:
        raise ValueError('Invalid launcher settings structure.') from exc


def load(root: Path = ROOT) -> Settings:
    saved = root / 'runtime/launcher/settings.json'
    if saved.exists():
        if saved.stat().st_size > 65536:
            raise ValueError('The launcher settings file is too large.')
        return from_dict(json.loads(saved.read_text(encoding='utf-8-sig')))
    result = Settings()
    existing = root / 'buzz-paths.json'
    if existing.exists():
        data = json.loads(existing.read_text(encoding='utf-8-sig'))
        result.paths = {key: data.get(key, '') for key in result.paths}
    result.validate()
    return result


def atomic_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name + '.', suffix='.tmp', dir=path.parent)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8', newline='\n') as stream:
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def save(settings: Settings, root: Path = ROOT) -> None:
    settings.validate()
    atomic_text(root / 'runtime/launcher/settings.json', json.dumps(asdict(settings), ensure_ascii=False, indent=2) + '\n')


def title_id(data: bytes) -> str:
    if len(data) < 20 or data[:4] != b'\0PSF':
        raise ValueError('Invalid PARAM.SFO.')
    keys, values, count = struct.unpack_from('<III', data, 8)
    if count > 256 or 20 + count * 16 > len(data) or keys >= len(data) or values >= len(data):
        raise ValueError('Corrupt PARAM.SFO table.')
    for n in range(count):
        key, fmt, length, capacity, offset = struct.unpack_from('<HHIII', data, 20 + 16 * n)
        end = data.find(b'\0', keys + key)
        if end < 0 or not keys <= keys + key < values or end >= values or length > capacity or values + offset + length > len(data):
            raise ValueError('Invalid PARAM.SFO field.')
        if data[keys + key:end] == b'TITLE_ID':
            if fmt != 0x204 or length > 32:
                raise ValueError('Invalid TITLE_ID.')
            return data[values + offset:values + offset + length].rstrip(b'\0').decode('ascii')
    raise ValueError('PARAM.SFO does not contain TITLE_ID.')


def validate_files(settings: Settings, root: Path = ROOT, expected_hash: str = ELF_SHA256) -> None:
    settings.validate()
    game, flash, elf = (Path(settings.paths[k]) for k in ('game_root', 'dev_flash', 'eboot_elf'))
    if any(not settings.paths[k].strip() or not Path(settings.paths[k]).is_absolute() for k in settings.paths):
        raise ValueError('Select absolute paths to the game, dev_flash and EBOOT.elf.')
    sfo = game / 'PS3_GAME/PARAM.SFO'
    if not (game / 'PS3_GAME/USRDIR').is_dir() or not sfo.is_file() or sfo.stat().st_size > 1024 * 1024:
        raise ValueError('The game folder must contain PS3_GAME/USRDIR and PARAM.SFO.')
    disc_title = title_id(sfo.read_bytes())
    if not (flash / 'data/font').is_dir():
        raise ValueError('Select a firmware dev_flash folder containing data/font.')
    if not elf.is_file() or elf.stat().st_size > 256 * 1024 * 1024:
        raise ValueError('Select a decrypted EBOOT.elf from a supported game version.')
    with elf.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    if expected_hash == ELF_SHA256:
        from .game_profiles import identify
        profile = identify(disc_title, digest)
        if not profile.buildable:
            raise ValueError(profile.note)
        from .active_build import matching_workspace
        selected_project = matching_workspace(root, digest, disc_title)
        if selected_project is None:
            raise ValueError(f'Detected {profile.title_id} {profile.version}, but no matching unchanged build is available. '
                             'Use “Import ISO / prepare…”. An executable for another version will not be launched.')
        return selected_project
    elif digest != expected_hash or disc_title != 'BCES00647':
        raise ValueError('Unsupported EBOOT.elf or game title ID (SHA-256).')
    if not (root / 'build-windows/BuzzQuizWorldRecomp.exe').is_file():
        raise ValueError('BuzzQuizWorldRecomp.exe has not been built. Use the preparation wizard to build it.')
    return root


def preview(settings: Settings, keys_down: set[int], pad_states: list[int]) -> list[int]:
    masks = [0] * 8
    for p, player in enumerate(settings.players[:settings.places]):
        for b, key in enumerate(player.keys):
            code = key_code(key)
            if code and code in keys_down:
                masks[p] |= 1 << b
    if settings.gamepads:
        for slot, pad in enumerate(settings.pads):
            if pad.player is not None and pad.player < settings.places:
                for b, button in enumerate(pad.buttons):
                    if pad_states[slot] & PAD_BITS[button]:
                        masks[pad.player] |= 1 << b
    return masks


def plan_launch(settings: Settings, root: Path = ROOT, environ: dict | None = None) -> tuple[list[str], dict]:
    """Create a unique config snapshot; no process launch and no game-file writes."""
    settings.validate()
    session = root / 'runtime/launcher/sessions' / (datetime.now().strftime('%Y%m%d-%H%M%S-') + uuid.uuid4().hex[:8])
    session.mkdir(parents=True, exist_ok=False)
    paths = session / 'paths.json'
    inputs = session / 'input.ini'
    atomic_text(paths, json.dumps(settings.paths, ensure_ascii=False, indent=2) + '\n')
    atomic_text(inputs, settings.input_ini())
    env = dict(os.environ if environ is None else environ)
    # The UI owns input/frontend settings. Performance/SPURS variables are preserved.
    for key in list(env):
        if key.upper() in {'BUZZ_KEYS', 'BUZZ_KEYS_2', 'BUZZ_KEYS_3', 'BUZZ_KEYS_4', 'BUZZ_PULSE_MS',
                           'BUZZ_SCRIPT', 'BUZZ_INPUT_CONFIG'}:
            del env[key]
    env['BUZZ_OFFLINE'] = str(int(settings.offline))
    env['BUZZ_HIDE_PSN_STORE'] = str(int(settings.hide_store))
    command = ['powershell.exe', '-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
               '-File', str(root / 'launcher/start-game.ps1'),
               '-InputConfig', str(inputs), '-PathsConfig', str(paths)]
    if settings.background:
        command.append('-Background')
    return command, env
