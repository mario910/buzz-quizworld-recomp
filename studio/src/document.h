#pragma once
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QVector>

namespace studio {
enum class QuestionContentType { Text, Picture, Video };

struct Question {
    QString id, text, category, topic, image, speech, video, music, ttsText, source, language = QStringLiteral("pl");
    QStringList answers;
    int correct = 0, difficulty = 2, subjectId = 0, categoryId = 0;
    QJsonObject extra;
    QJsonObject translations;
    QJsonArray sourceMedia() const;
    QJsonObject sourceMusic() const;
    bool usesMusic() const;
    QuestionContentType contentType() const;
    bool hasUnresolvedMusic() const;
    bool hasUnresolvedVisualMedia() const;
    bool hasUnresolvedSourceMedia() const;
    QStringList problems() const;
    QJsonObject json(const QString &baseDir = {}) const;
    QJsonObject localized(const QString &baseDir = {}) const;
    void loadLocalized(const QJsonObject &value);
    void switchLanguage(const QString &code);
    QStringList languages() const;
    QStringList sourceCategoryPath() const;
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
    QString sourceKey{}, sourceName{};
    QString name(const QString &language) const { return names.value(language).toString(sourceName); }
    bool operator==(const CustomCategory &other) const {
        return id == other.id && parent == other.parent && names == other.names &&
               sourceKey == other.sourceKey && sourceName == other.sourceName;
    }
};
struct RoundPlanEntry {
    QString type;
    int questionCount = 0; // Use the pack's default for this round type.
    bool operator==(const RoundPlanEntry &other) const {
        return type == other.type && questionCount == other.questionCount;
    }
};
// Pack scripts store numbers as 32-bit floats.
constexpr int maxRoundQuestionCount = 1 << 24;
using RoundPlans = QMap<QString, QList<RoundPlanEntry>>;
const RoundPlans &defaultRoundPlans();
QStringList roundTypesForMode(const QString &mode);

struct Document {
    // Title and description of the active language; other languages in titles/descriptions.
    QString title = QStringLiteral("Moja paczka"), description, language = QStringLiteral("pl");
    QStringList languageCodes;
    QJsonObject packExtra, rootExtra, titles, descriptions;
    QVector<CustomCategory> categories;
    QVector<Question> questions;
    QStringList languages() const;
    RoundPlans roundPlans() const;
    void setRoundPlans(const RoundPlans &plans);
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
    int sourcePacks = 0;
    bool gameImport = false;
    bool ok() const { return error.isEmpty(); }
};
QString categoryName(int id);
// Adds another project's custom categories (same names reuse an existing one);
// returns the incoming id -> id in `into`.
QMap<int, int> mergeCustomCategories(Document &into, const QVector<CustomCategory> &incoming);
int nextCustomCategoryId(const Document &document);
int retainGameCategories(Document &document, qsizetype firstQuestion, const QString &sourceLanguage,
                         QStringList &warnings);
QString localPath(const QString &value, const QString &baseDir = {});
LoadResult parseJson(const QByteArray &bytes, const QString &baseDir = {}, const QString &language = {});
LoadResult loadDocument(const QString &path, const QString &language = {});
bool saveDocument(const Document &document, const QString &path, QString *error, bool legacy = false);
QByteArray serialize(const Document &document, const QString &baseDir = {}, bool legacy = false);
Question newQuestion();
// Retain the source's correctness while adapting text answers to the four-choice format.
bool adaptImportedAnswers(Question &question, const QList<int> &correctAnswers);
QString adaptedAnswersWarning(int count);
QString unsupportedAnswersWarning(int count);
Document exampleDocument(const QString &language = QStringLiteral("pl"));
} // namespace studio
