#pragma once
#include <QJsonObject>
#include <QStringList>
#include <QVector>

namespace studio {
struct Question {
    QString id, text, category, topic, image, speech, ttsText, source, language = QStringLiteral("pl");
    QStringList answers;
    int correct = 0, difficulty = 2, subjectId = 0, categoryId = 0;
    QJsonObject extra;
    QJsonObject translations;
    QStringList problems() const;
    QJsonObject json(const QString &baseDir = {}) const;
    QJsonObject localized(const QString &baseDir = {}) const;
    void loadLocalized(const QJsonObject &value);
    void switchLanguage(const QString &code);
    QStringList languages() const;
};
struct Document {
    QString title = QStringLiteral("Moja paczka"), description, language = QStringLiteral("pl");
    QStringList languageCodes;
    QJsonObject packExtra, rootExtra;
    QVector<Question> questions;
    QStringList languages() const;
    void switchLanguage(const QString &code);
};
struct LoadResult {
    Document document;
    QString error;
    QStringList warnings;
    int duplicates = 0;
    bool ok() const { return error.isEmpty(); }
};
QString categoryName(int id);
QString localPath(const QString &value, const QString &baseDir = {});
LoadResult parseJson(const QByteArray &bytes, const QString &baseDir = {}, const QString &language = {});
LoadResult loadDocument(const QString &path, const QString &language = {});
bool saveDocument(const Document &document, const QString &path, QString *error, bool legacy = false);
QByteArray serialize(const Document &document, const QString &baseDir = {}, bool legacy = false);
Question newQuestion();
Document exampleDocument();
} // namespace studio
