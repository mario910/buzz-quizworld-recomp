# Buzz! Quiz TV (PL Special Edition, BCES00305) vs Quiz World pack 100

## Disc layout (Joliet; PAKs are plain stored ZIPs, method 0, no EDAT/NPD)
- USRDIR/EBOOT.BIN, PRODUCT.DAT
- USRDIR/GAME/: GLOBAL.PAK (385MB), GLOBALSCRIPTS.PAK (322 .clu), POL.PAK (1.03GB: commentary, music, VIDEO 80 .sdi, NAMEDTEXT), POLSCRIPTS.PAK (only speechinfo.clu), CHRSND02..35.PAK
- USRDIR/PACK0002,0003,0004,0005,0006,0018,0023/POLPACK00xx.PAK — question packs. Each has:
  POL/ROUNDS/<subject>_<roundtype>.{hdr,hst,qak,kaq} (36 files = 4 subjects x 9 round types; 9 files for 0018/0023) + ROUNDS/stringTables.dir (text list)
  POL/QUESTIONASSETS/SPEECH/QUESTIONS/ONDEMAND/*.rsf, MUSIC/STREAMED/*.rsf (pack 2 only), VIDEO/*.sdi + VIDEO/pics001..003.sdi
  POL/NAMEDTEXT/default.{hdr,hst,ndx} + NamedCharMap.adt (QW has no .adt)
  POL/SCRIPTS/register.clu, packswitch.clu, questioncategories.clu, questiondifficulty.clu, questionreveals.clu,
  POL/SCRIPTS/GENERATED/addpackinfo.clu, roundsetup.clu, roundpackages.clu
  0018/0023 also POL/GRAPHICS/Pack00xxTextures.ete (pack info image)
- Listings: isolist.txt, paklist.txt

## Formats
- qak header: QTV ver 0x3EA (1002), QW 0x3EB (1003). Everything else same: FE FF 01 00, ver, u32 11 tables, u32 380, u16 (QW 11, QTV 1), u16 packId (QW 100; QTV 2..23), name, copyright@0xD4, "POL"@0x174, u32 (1 or 3), 11 table descriptors {u32 off,u16 count,u16 idx}@0x17C.
  Record sizes identical: t0 16, t1 36 (question), t2 4, t3 8, t4 4 (answers, 4/question), t5 2, t6 2.
  Semantic diffs: t5 count = 2x questions in QW, 1x in QTV; t6 count = questions in QW, larger in QTV.
- QW EBOOT loader func_000E4828 (port/recompiled/ppu_recomp_005.cpp): reads u16 ver at +4, accepts (ver-1002) <= 1 => 1002 AND 1003 accepted; then checks FEFF/FFFE magic, u16 +2 == 1, copyright string at +0xD4.
- hdr/hst: identical Huffman scheme (00 FF, u32 BE nstr, u32 BE totalSyms, [u16 LE sym][u32 BE freq]...). qfmt/hufftext.py decodes all 221 QTV files fully (bytes used == file size).
  QTV text is ALL CAPS; per question 6 strings (question, bet-intro line, 4 answers) like QW (QW 2nd string = category name). allthatapply 5/question, pointstealer 1/question.
- rsf/sdi headers same layout (rsf 05=speech/06=music, 0xBB80=48kHz; sdi 640x368, QTV 25fps 0x61A8 vs QW 29.97 0x7512). rsf byte 0x18: QTV 03 (speech)/01 02 (music) vs QW 00.

## Registration
- QTV register.clu: IncludeScript('Generated/AddPackInfo') only. packswitch.clu defines PackSwitch.SwitchToPack -> includes QuestionCategories/Difficulty/Reveals (QW has no PackSwitch).
- AddPackInfo 13 args, same signature as QW:
  P2 (1000,2,1,'PackTitleMusicEras','PACK0002','Music',0,'MusicalErasPackGame1p/4p/8p','PackDescMusicEras','BZ3_InfoPackImage_Music','')
  P3 (1000,3,2,...'TVCinema'), P4 (1000,4,3,...'Knowledge'), P5 (1000,5,4,...'Sport'), P6 (1000,6,5,...'Lifestyle'),
  P18 (1000,18,17,'PackTitlePk18Pack','PACK0018','Knowledge',5,'p18PackGame..','PackDescPk18Pack','BZ3_InfoPackImage_Pack0018',''), P23 (1000,23,22,...,'Lifestyle',5,...)
  If true -> IncludeScript Generated/RoundSetup + Generated/RoundPackages (QW: only RoundPackages).
- RoundSetup: AddGame(name,id,'1P/4P/8PPACKSPECIFIC','MaxNPlayers',0,-1,1,1,4) ids N000..N002; AddGamePlanRounds(game,roundType,n);
  AddRound(id,game,group,roundType,'SUBJECT_x','PACK000N','<subject>_<roundtype>','RoundPlanNonAsset|RoundPlanFinalCountdown','NULL_EVENT_PLAN',6,0,1|2,'9023942..') 13 args, ids N300..N455 (39 for 18/23).
- RoundPackages: PackageData.NewPackage(file); AddRoundFile(n,0,file,file) — same API as QW.
- Categories: QTV only AddQuestionCategoryItem(115,'UNCATEGORISED','QC_UNCATEGORISED',67) (4 args, after ClearAll). QW uses 8 args, ids 116..436 with packid 100.
- QW still registers natives AddRound (0x150CC0, separate from AddRoundNew), AddGame, AddGamePlanRounds; QW gamesetup.clu still defines ChannelHopper/Music/Knowledge/TVCinema/Sport/LifestyleGame*, and RoundPlanNonAsset/FinalCountdown scripts exist => legacy Quiz TV pack support likely. NULL_EVENT_PLAN, SUBJECT_* keys not present in QW.

## Counts (questions; see question_counts.txt, question_ids.txt)
Unique question IDs per pack: P2 822, P3 855, P4 921, P5 848, P6 900, P18 609, P23 557 = 5512 (QW pack100: 6173).
QTV QIDs 7556..63258, QW 362053..373460 -> no overlap. Game/round/category/pack IDs don't collide either.
Asset IDs: speech 6000051481..6000064609 (QW 6000098975+), no collision; music 1000704 and video 1001124 exist in both (only matters if lookup isn't pack-relative; pics001..003.sdi exists in every pack, so lookup is per-pack).

## Pictures
No image files for questions; picture questions are pics001..003.sdi in each pack's QUESTIONASSETS/VIDEO (frame container, flag 00000001, no audio) — same in QW pack 100.
