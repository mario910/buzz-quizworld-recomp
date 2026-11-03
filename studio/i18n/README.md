# Interface translations

Polish is the source language. `studio_en.ts` is the English Qt Linguist catalog.
CMake runs `lrelease` and embeds the QM resource; no translation downloads or
external files are needed at runtime. Qt's own dialog translations come from the
installed/deployed Qt runtime.

After changing interface strings, run Qt's `lupdate` from the repository root:

```sh
lupdate studio/src studio/ui -ts studio/i18n/studio_en.ts \
  -source-language pl -target-language en -no-obsolete
```

Fill all new entries with Qt Linguist or by editing the TS XML, preserving `%1`
placeholders and file globs. CTest rejects unfinished/missing translations and
changed placeholders. Content strings imported from games or authored by users
must not be passed through interface translation.

To add another interface language, add its TS catalog to CMake and the supported
choices in `Localization`/the language menu. Project/import language codes are
independent of the available interface catalogs.
