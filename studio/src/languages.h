#pragma once
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace studio {
QString languageCode(const QString &value);
QString languageName(const QString &code);
QString preferredLanguage(const QStringList &available);
QVariantList languageChoices(const QStringList &codes = {});
struct SourceLanguages {
    QStringList codes;
    QString preferred, error;
    // Content ID of a PS3 package that needs the user's .rap license file.
    QString license;
    bool project = false;
    bool ok() const { return error.isEmpty(); }
};
SourceLanguages inspectLanguages(const QString &path);
} // namespace studio
