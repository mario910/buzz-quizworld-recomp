"""Synthetic pack registration/database consistency checks (no game assets)."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "questions"))
import buzzpack
import buzzq
import clu


class PackCategoriesTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.template = self.root / "template.pak"
        categories = [
            (("AddQuestionCategoryItem",), [235, "Science", "CAT_SCIENCE", 0, 0, 1, 100, 1]),
            (("AddQuestionCategoryItem",), [237, "General", "CAT_GENERAL", 235, 0, 1, 100, 1]),
            (("AddQuestionCategoryItem",), [436, "Last", "CAT_LAST", 235, 0, 0, 100, 1]),
        ]
        info = clu.call_script([(("AddPackInfo",), [100, "PACK0100", "PackTitlePk100Pack",
            "PackDescPk100Pack", "p100PackGame1p", "p100PackGame4p", "p100PackGame8p"])])
        hdr, hst = buzzq.encode_strings(["Science", "General"])
        with zipfile.ZipFile(self.template, "w") as z:
            z.writestr("POL/SCRIPTS/GENERATED/questioncategories.clu", clu.dump(clu.call_script(categories)))
            z.writestr("POL/SCRIPTS/GENERATED/addpackinfo.clu", clu.dump(info))
            z.writestr("POL/NAMEDTEXT/default.hdr", hdr)
            z.writestr("POL/NAMEDTEXT/default.hst", hst)
            z.writestr("POL/NAMEDTEXT/default.ndx", "\ufeff1 1\r\n2 2\r\n".encode())
        self.source = {"pack": {"title": "Synthetic pack", "topic": "Science"}, "questions": [
            {"id": "synthetic", "subject": 235, "category": 237, "question": "Two plus two?",
             "answers": ["Four", "One", "Two", "Three"]}]}

    def build(self, number=102):
        source = self.root / "source.json"
        source.write_text(json.dumps(self.source), encoding="utf-8")
        return buzzpack.build(source, self.template, self.root, number)[0]

    def test_global_category_ids_names_and_round_references_agree(self):
        ids_by_pack = []
        for number in (102, 103):
            pak = self.build(number)
            with zipfile.ZipFile(pak) as z:
                cats = buzzpack.template_categories(z, "POL")
                ids = {c[0] for c in cats}
                self.assertTrue(all(1 <= c <= 999 for c in ids))
                self.assertFalse(ids & {235, 237, 436})
                ids_by_pack.append(ids)
                root = next(c for c in cats if c[3] == 0)
                self.assertEqual(root[1], f"p{number}_Science")
                self.assertEqual(root[2], "CAT_SCIENCE")
                child = next(c for c in cats if c[3])
                self.assertEqual(child[3], root[0])
                setup = clu.load(z.read("POL/SCRIPTS/GENERATED/roundsetup.clu"))
                self.assertIn(root[1], setup.consts)
                self.assertNotIn("Science", setup.consts)
                for ext in ("hdr", "hst", "qak", "kaq"):
                    (self.root / f"round.{ext}").write_bytes(z.read(f"POL/ROUNDS/p{number}_science.{ext}"))
                q = buzzq.parse_round(str(self.root / "round"))["questions"][0]
                self.assertEqual(set(q["categories"]), ids)
                self.assertEqual(q["round_ids"], [root[7]])
                self.assertEqual(q["question"], "Two plus two?")
                self.assertEqual([a["text"] for a in q["answers"]], ["Four", "One", "Two", "Three"])
        self.assertFalse(ids_by_pack[0] & ids_by_pack[1])

    def test_invalid_or_colliding_ranges_fail_before_media_encoding(self):
        for base in (0, 999, 10201, 235, 436, 600.5, True):
            with self.subTest(base=base):
                self.source["pack"]["category_base"] = base
                with patch.object(buzzpack, "encode_speech") as encode:
                    with self.assertRaises(ValueError):
                        self.build()
                    encode.assert_not_called()

    def test_explicit_free_range_for_other_pack_number(self):
        self.source["pack"]["category_base"] = 950
        with zipfile.ZipFile(self.build(120)) as z:
            self.assertEqual({c[0] for c in buzzpack.template_categories(z, "POL")}, {950, 951})

    def test_round_choices_cover_subcategories_and_their_question_file(self):
        with zipfile.ZipFile(self.build()) as z:
            cats = buzzpack.template_categories(z, "POL")
            setup = clu.load(z.read("POL/SCRIPTS/GENERATED/roundsetup.clu"))
            registered, registers = [], {}
            for ins in setup.code:
                op, a, bx = ins & 63, (ins >> 6) & 255, ins >> 14
                if op in (1, 5):  # LOADK, GETGLOBAL: generated scripts use constants
                    registers[a] = setup.consts[bx]
                elif op == 28 and registers[0] == "AddRoundNew":
                    registered.append([registers[i] for i in range(1, 8)])
            self.assertEqual({r[4] for r in registered}, {c[1] for c in cats})
            for category in cats:
                choices = [r for r in registered if r[4] == category[1]]
                self.assertEqual({r[1] for r in choices},
                                 {"p102PackGame1p", "p102PackGame4p", "p102PackGame8p"})
                self.assertEqual({r[6] for r in choices}, {"p102_science"})
                self.assertIn("POL/ROUNDS/p102_science.qak", z.namelist())

    def test_native_game_registration_uses_all_nine_arguments(self):
        calls = buzzpack.round_setup_calls(102, "PACK0102", {"4p": "test4p"},
                                          [("Science", "test_science"), ("Music", "test_music")])
        args = next(args for names, args in calls if names == ("AddGame",))
        self.assertEqual(len(args), 9)  # native 0x14FB20 reads nine Lua arguments
        self.assertEqual(args[7:], [0, 4])  # boolean flag followed by number of choices
        plans = [args[1] for names, args in calls if names == ("AddGamePlanRounds",)]
        self.assertEqual(plans[0], "PointBuilder")
        self.assertGreater(len(set(plans)), 1)
        self.assertEqual(plans[-1], "FinalCountdown")


if __name__ == "__main__":
    unittest.main()
