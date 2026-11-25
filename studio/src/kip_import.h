#pragma once
#include "document.h"
#include "languages.h"
#include <QMap>

namespace studio {
// Knowledge Is Power / Wiedza to Potęga (PS4, CUSA09557): reads the question
// tables, localisation, pictures and voice straight from the user's fake-signed
// .pkg or from a folder extracted from it (choose Media/resources.assets).
// Same data as tools/questions/kip_export.py, without Python.
bool isKipSource(const QString &path);
SourceLanguages kipLanguages(const QString &path);
LoadResult importKip(const QString &path, const QString &language = {});
// Question tables (TextAssets named KIP_*) of a Unity resources.assets file.
QMap<QString, QString> kipTextAssets(const QByteArray &assets);
} // namespace studio
