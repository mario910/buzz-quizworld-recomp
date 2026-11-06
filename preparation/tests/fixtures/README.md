# Authored native-import fixtures

`generate.py` freezes synthetic containers using the existing Python reference
helpers in `tests/test_ps3_import.py` and `tests/test_setup_pipeline.py`.
All payloads are original strings or byte sequences; the test key material is
arbitrary sequential/repeated bytes. There is no retail code, media, firmware or
private production key material in these fixtures.

Binary fixtures deliberately use the `.fixture` suffix so generated user-owned
`.self`, `.elf`, `.bin`, `.iso`, `.pup`, `.pkg` and private `.keys` inputs stay
git-ignored. `test-keys.json` contains only the arbitrary fixture keys.

Native tests read the frozen fixtures directly, with no Python process. They
compare reconstructed ELF bytes and extracted contents against reference output,
and check corruption, traversal, collisions, cancellation and resume behavior.
`ppu-image.fixture` and `spu-container.fixture` are authored ELF samples for
loader/import tables and SPU extent/fingerprint checks. `import_spu_tables.py`
regenerates the checked-in native opcode tables from the pinned SDK and the
frozen decoder parity digest. Neither generator is part of the build.
