#pragma once
#include "document.h"
#include "languages.h"

namespace studio {
// Questions from the PS2 (Buzz! The Mega Quiz, Hollywood Quiz, ...) and PSP
// (Master Quiz, Quiz World, ...) games. Accepts a disc image (.iso/.cso), a
// PS2 <LANG>ROUND.PAK or a PSP MERGED.QDB next to its MERGED.TXT.
// Read-only: nothing is executed or written next to the source.
bool isLegacySource(const QString &path);
SourceLanguages legacyLanguages(const QString &path);
LoadResult importLegacy(const QString &path, const QString &language = {});
} // namespace studio
