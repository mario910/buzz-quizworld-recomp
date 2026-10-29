"""Exact executable identities; detection is NOT the same as build compatibility."""
from dataclasses import dataclass

from buzz_paths import EXPECTED_ELF_SHA256


@dataclass(frozen=True)
class Profile:
    key: str
    title_id: str
    version: str
    elf_sha256: str
    buildable: bool
    note: str
    adapter_family: str = "unvalidated"
    build_switches: tuple[str, ...] = ()
    native_spu_float: bool = False  # Unreviewed versions must not inherit an optimized default.
    runtime_defaults: tuple[tuple[str, str], ...] = ()

    def compiler_options(self):
        return {"NativeSpuFloat": self.native_spu_float, **{name: True for name in self.build_switches}}


PROFILES = (
    Profile("bces00647-102", "BCES00647", "1.02", EXPECTED_ELF_SHA256, True,
            "Verified generation/native-adapter profile; see README for gameplay limitations.",
            adapter_family="quizworld-102-verified",
            native_spu_float=True,
            build_switches=("NativeCompletion", "NativeImage40", "NativeImage39", "NativeImage36",
                            "ResultGuardFP", "StageF", "NativeImage7", "MStreamModules")),
    *(Profile(f"{title.lower()}-disc", title, "1.00 / original disc",
              "6b07344c81a669174586ea637634f3b522eb91a7824e9e88623581b88d0405a8", True,
              "EU 1.00 experimental profile. Menus and native job replay tested on Polish assets; "
              "other language assets and full gameplay still need validation.",
              adapter_family="quizworld-eu100", native_spu_float=True,
              runtime_defaults=(("BUZZ_NATIVE_COMPLETION", "2"),),
              build_switches=("NativeCompletion", "NativeImage40", "NativeImage39", "NativeImage36",
                              "ResultGuardFP", "StageF", "NativeImage7", "MStreamModules"))
      for title in ("BCES00647", "BCES00644", "BCES00645")),
    Profile("bcus98209-disc", "BCUS98209", "1.00 / original disc",
            "fb78b9e6d30ba5eaa76b7e31bc3afa838736dcb5cde40fc602a628f2305ebb5d", True,
            "US original-disc profile: menus, fresh profiles and standard two-player questions/scoring tested. See README for remaining limitations.",
            adapter_family="quizworld-us100", native_spu_float=True,
            runtime_defaults=(("BUZZ_NATIVE_COMPLETION", "2"),),
            build_switches=("NativeCompletion", "NativeImage40", "NativeImage39", "NativeImage36",
                            "ResultGuardFP", "StageF", "NativeImage7", "MStreamModules")),
)

KNOWN_TITLE_IDS = frozenset(profile.title_id for profile in PROFILES)


def identify_elf(digest):
    """Identify executable code without inventing a disc region for ELF-only checks."""
    matches = [profile for profile in PROFILES if profile.elf_sha256 == digest]
    if not matches or len({profile.adapter_family for profile in matches}) != 1:
        raise ValueError("Unsupported or ambiguous executable SHA-256: " + digest)
    return matches[0]


def identify(title_id, digest):
    for profile in PROFILES:
        if profile.title_id == title_id and profile.elf_sha256 == digest:
            return profile
    raise ValueError(f"Unsupported executable for {title_id}: SHA-256 {digest}. "
                     "Another region/language release needs its own tested profile; hash guards are not bypassed.")
