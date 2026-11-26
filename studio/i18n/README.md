# Interface translations

Polish is the source language. There is one Qt Linguist catalog, `studio_<code>.ts`, for each
other interface language of the Buzz! games:

- en, de, fr, it, es, nl, pt;
- sv, da, no, fi;
- ru, cs, el, hr.

CMake compiles every `studio_*.ts` with `lrelease` and embeds the QM files, so nothing is
downloaded and no external files are needed at runtime. Qt's own dialog translations come from
the Qt runtime that is installed or deployed with the app.

On the first start the interface follows the system language and falls back to English.
After that, the language picked under **… → Język interfejsu** is remembered.

After changing interface strings, run Qt's `lupdate` from the repository root:

```sh
for ts in studio/i18n/studio_*.ts; do
  lupdate studio/src -ts "$ts" -source-language pl -no-obsolete
done
```

Fill every new entry with Qt Linguist or by editing the TS XML. Keep `%1` placeholders and
file globs as they are. CTest rejects:

- unfinished or missing translations;
- changed placeholders;
- catalogs whose message count differs from `studio_en.ts`.

Content strings that come from games or that users wrote must not go through interface
translation.

To add another interface language:

1. Add its `studio_<code>.ts` catalog.
2. Add the language to `Localization::languages()`.
3. Update the catalog count in `document_test.cpp`.

Project and import language codes do not depend on which interface catalogs exist.
