# BuzzSetup translations

English is the source language. There is one Qt Linguist catalog, `buzzsetup_<code>.ts`, for each
other interface language of the Buzz! games:

- pl, de, fr, it, es, nl, pt;
- sv, da, no, fi;
- ru, cs, el, hr.

CMake compiles every catalog and embeds the result in BuzzSetup.

Interface strings go through `T("…")` in `src/wizard.cpp` and `src/release_update.cpp` (translation
context `BuzzSetup`). The build log and error messages from the core library stay in English.

On the first start BuzzSetup follows the system language and falls back to English. The language
list in the header switches the window and remembers the choice.

`T()` is a plain helper with a fixed context, which `lupdate` does not recognize. When you add or
change a `T("…")` string, add a `<message>` with that English source to the `BuzzSetup` context of
every catalog, with its translation. The `preparation-catalogs` test rejects:

- empty or unfinished translations;
- changed placeholders or line breaks;
- catalogs that do not all translate the same messages.
