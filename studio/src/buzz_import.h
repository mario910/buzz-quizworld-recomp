#pragma once
#include "document.h"
#include "languages.h"
namespace studio {
// Read-only parsing: retail Lua bytecode is never executed.
LoadResult importBuzz(const QString &path, const QString &language = {}, bool extractSpeech = true);
SourceLanguages buzzLanguages(const QString &path);
QStringList decodeHuffman(const QByteArray &header, const QByteArray &stream);
} // namespace studio
