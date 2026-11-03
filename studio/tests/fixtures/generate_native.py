"""Regenerate tiny authored Buzz fixtures; not needed to run Studio/tests."""
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/questions"))
import buzzround

buzzround.COPYRIGHT = "Authored Question Studio test fixture. MIT."
strings = ["Która litera jest pierwsza?", "Alfabet", "A", "B", "C", "D"]
questions = [
    buzzround.Question(1, 2, [(1, 1, 1), (1, 2, 2)],
                       [(3, 5), (4, 1), (5, 1), (6, 1)], [235, 237], 1),
    # The 0 here is a media row, not a string reference.
    buzzround.Question(2, 1, [(2, 1, 1)],
                       [(0, 6), (1, 2), (2, 2), (3, 2)], [235, 237], 1),
    buzzround.Question(3, 1, [(3, 1, 1)],
                       [(3, 5), (4, 5), (5, 1), (6, 1)], [235, 237], 1),
]
model = buzzround.RoundModel("StudioFixture", 991, "POL", strings, questions, version=1002)
folder = Path(__file__).parent
with zipfile.ZipFile(folder / "native-quiztv.pak", "w", zipfile.ZIP_STORED) as archive:
    for ext, data in buzzround.write_round(model).items():
        if ext == "kaq":
            continue
        info = zipfile.ZipInfo("POL/ROUNDS/studio_test." + ext, (2026, 1, 1, 0, 0, 0))
        archive.writestr(info, data)

with zipfile.ZipFile(folder / "native-multilingual.pak", "w", zipfile.ZIP_STORED) as archive:
    for lang, words in [("POL", strings),
                        ("ENG", ["Which letter comes first?", "Alphabet", "A", "B", "C", "D"])]:
        bilingual = buzzround.RoundModel("StudioFixture", 991, lang, words, questions[:1], version=1002)
        for ext, data in buzzround.write_round(bilingual).items():
            if ext != "kaq":
                info = zipfile.ZipInfo(f"{lang}/ROUNDS/studio_test.{ext}", (2026, 1, 1, 0, 0, 0))
                archive.writestr(info, data)
