#pragma once
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QVector>

namespace studio {
struct Question {
    QString id, text, category, topic, image, speech, video, ttsText, source, language = QStringLiteral("pl");
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
// The project's own subjects and subcategories (ids from firstCustomCategory up,
// never Quiz World ids). A subject has parent 0; a subcategory's parent is one of
// the project's subjects. Names are per content language.
constexpr int firstCustomCategory = 10000;
constexpr int customCategoryNameLimit = 40;
inline bool isCustomCategory(int id) { return id >= firstCustomCategory; }
struct CustomCategory {
    int id = 0, parent = 0;
    QJsonObject names; // language code -> name
    QString name(const QString &language) const { return names.value(language).toString(); }
    bool operator==(const CustomCategory &other) const {
        return id == other.id && parent == other.parent && names == other.names;
    }
};
struct Document {
    // Title and description of the active language; other languages in titles/descriptions.
    QString title = QStringLiteral("Moja paczka"), description, language = QStringLiteral("pl");
    QStringList languageCodes;
    QJsonObject packExtra, rootExtra, titles, descriptions;
    QVector<CustomCategory> categories;
    QVector<Question> questions;
    QStringList languages() const;
    void switchLanguage(const QString &code);
    const CustomCategory *customCategory(int id) const;
    // Display name of a Quiz World or custom category in the active language.
    QString categoryLabel(int id) const;
    // Questions in custom subjects show their subject's name in the active language.
    void refreshCategoryNames();
};
struct LoadResult {
    Document document;
    QString error;
    QStringList warnings;
    // Content ID of a PS3 package that needs the user's .rap license file.
    QString license;
    // Where the project lives when it differs from the opened file (.buzzproject).
    QString projectPath;
    int duplicates = 0;
    bool ok() const { return error.isEmpty(); }
};
QString categoryName(int id);
// Adds another project's custom categories (same names reuse an existing one);
// returns the incoming id -> id in `into`.
QMap<int, int> mergeCustomCategories(Document &into, const QVector<CustomCategory> &incoming);
int nextCustomCategoryId(const Document &document);
QString localPath(const QString &value, const QString &baseDir = {});
LoadResult parseJson(const QByteArray &bytes, const QString &baseDir = {}, const QString &language = {});
LoadResult loadDocument(const QString &path, const QString &language = {});
bool saveDocument(const Document &document, const QString &path, QString *error, bool legacy = false);
QByteArray serialize(const Document &document, const QString &baseDir = {}, bool legacy = false);
Question newQuestion();
Document exampleDocument();
} // namespace studio
