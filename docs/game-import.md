# Built-in game and firmware preparation

Run `Prepare-Buzz.cmd` to prepare your own game and firmware. The importer
reads the files directly; it does not start RPCS3, scetool, or another
decryption executable. Building still requires the development tools listed
in README. The resulting game and native launcher do not need Python.

Choose your game ISO/dump, firmware PUP or installed `dev_flash`, an optional
update PKG/EBOOT, and a private local key file for encrypted inputs. A key
file at `runtime/keys/ps3.keys` is selected automatically when the wizard
opens. The importer never downloads keys, games or firmware. Key values are
not put in logs, preparation ledgers or source snapshots. The ledger stores
the key file's path and SHA-256 to detect changes on resume.

You can also prepare files without opening a window:

```powershell
.\toolenv\Scripts\python.exe tools/prepare-game.py `
  --game 'D:\MyGame' --firmware 'D:\MyFirmware\PS3UPDAT.PUP' `
  --keys 'D:\Private\ps3.keys' --update 'D:\MyUpdate.pkg' --import-only
```

Omit `--import-only` to generate/build after import. Omit `--update` to use
the disc executable. Use `--resume <session-directory>` with the same inputs
to resume. Old sessions prepared with the RPCS3 helper are retained, but must
be imported into a new session with the built-in importer. Existing prepared
builds can still be played.

## Private key-file formats

The repository intentionally contains no real decryption key database. Store
your keys outside the repository or in its ignored `runtime/keys/` directory.
The key file is data, not another executable or a running service.

Two text formats are accepted:

- A scetool-style key file with `[section]` blocks, `type=SELF`/`type=PKG`,
  `revision` (hex), `self_type`, `erk` and `riv`. Repeated section names for
  different revisions are supported. SELF types include `APP` and `NPDRM`.
  Named `[NP_klic_key]` and `[NP_klic_free]` blocks use `key=<hex>`.
- A JSON object with `format: 1`, a `roots` array and a `named` object, as
  illustrated below. Numbers are decimal; byte values are hexadecimal strings.

```json
{
  "format": 1,
  "roots": [
    {"category": 1, "self_type": 4, "revision": 0, "erk": "<32 bytes in hex>", "riv": "<16 bytes in hex>"},
    {"category": 1, "self_type": 8, "revision": 0, "erk": "<32 bytes in hex>", "riv": "<16 bytes in hex>"},
    {"category": 3, "self_type": 0, "revision": 0, "erk": "<32 bytes in hex>", "riv": "<16 bytes in hex>"}
  ],
  "named": {
    "pup_hmac": "<64 bytes in hex>",
    "np_klic_key": "<16 bytes in hex>",
    "np_klic_free": "<16 bytes in hex>"
  }
}
```

These placeholders are not usable keys. Only include the entries your inputs
require: disc EBOOTs need their application SELF root, free-license update
EBOOTs also need the NPDRM root and named NPDRM keys, and firmware PUPs need
the firmware package root (`category: 3`) and `pup_hmac`. A scetool key file
may lack the PUP HMAC entry; add a `[pup_hmac]` block with `type=OTHER` and
`key=<64 bytes in hex>`. No public/private signing keys or curve files are
needed for extraction. Already decrypted ELF and installed `dev_flash`
inputs do not require keys (pass the ELF through `--update` if necessary).

## Supported inputs and checks

- PS3 big-endian ELF64 and SELF EBOOTs, including free-license NPDRM disc
  updates. AES-256-CBC root metadata, AES-128-CTR sections, and bounded zlib
  decompression run in-process. Extracted program sections are checked against
  their SHA-1/HMAC-SHA-1/HMAC-SHA-256 digests. The reconstructed ELF must match
  an existing supported game/profile SHA-256 before its stage is completed.
- Full PS3 firmware PUPs: the header and update archive HMACs are checked;
  `dev_flash_*` firmware packages are decrypted and their TAR payloads unpacked.
  The required `flashATRAC.pic` code hash and actual font files are checked
  before the completed `dev_flash` directory is published.
- PS3 retail/debug update PKGs: the existing SDK keystream is reused in-process,
  with bounded chunked reads, strict paths and title-ID validation before
  extraction. Package signatures are not verified; executable hashes and
  runtime content checks remain the compatibility guards.

Source files are read-only. Outputs stay in `runtime/setup/sessions/`.
Imports reject archive traversal, links/reparse points in output paths,
Windows filename aliases, colliding file names and invalid/oversized ranges.
Cancellation is checked during hashing, archive iteration, section decoding
and PKG chunks. Incomplete firmware stays in an unpublished staging directory;
it can be retained after cancellation, and resume starts a fresh extraction.

Encrypted disc sectors, account-licensed PSN executables, ELF32/SPU SELF
extraction, custom firmware layouts and unrecognized game versions are outside
this importer's supported scope. SELF signing certificates are not verified.
This does not expand the runtime's supported game/firmware identities.

## Format references

The parsers were written from the public descriptions of
[Certified Files](https://www.psdevwiki.com/ps3/Certified_File),
[SELF/SPRX](https://www.psdevwiki.com/ps3/SELF_-_SPRX),
[PUP](https://www.psdevwiki.com/ps3/Playstation_Update_Package_(PUP)) and
[PKG](https://www.psdevwiki.com/ps3/PKG_files). No code from RPCS3 or scetool
was copied or translated into the importer.
