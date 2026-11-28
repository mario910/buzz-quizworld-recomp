#include "controller.h"
#include "buzz_import.h"
#include "languages.h"
#include "localization.h"
#include "ps2_media.h"
#include "psp_media.h"
#include "ps3_source.h"
#include "source_movie.h"
#include "media_tools.h"
#include "project_media.h"
#include "tts.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUndoCommand>
#include <QUuid>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <functional>
#include <utility>

namespace {
QString displaySource(const QString &source) {
    return source == "Własne" ? QCoreApplication::translate("Main", "Własne") : source;
}
bool textValid(const studio::Question &q) {
    return !q.text.trimmed().isEmpty() && q.correct >= 0 && q.correct < 4 && q.answers.size() == 4 &&
           std::none_of(
               q.answers.begin(), q.answers.end(), [](const QString &s) { return s.trimmed().isEmpty(); });
}
bool emptyTranslation(const QJsonObject &value) {
    for (const auto &key : {"question", "image", "speech", "video", "music", "topic", "tts_text"})
        if (!value[key].toString().isEmpty())
            return false;
    for (const auto &answer : value["answers"].toArray())
        if (!answer.toString().isEmpty())
            return false;
    return true;
}
bool canRecoverVisual(const studio::Question &question) {
    return studio::hasSourceQuestionMedia(question) || studio::hasBuzzSourcePicture(question);
}
bool isVisualReference(const QJsonValue &value) {
    const auto type = value.toObject().value("type");
    return type == 2 || type == 3 || type == "ps2-picture" || type == "ps2-video" ||
           type == "psp-picture" || type == "psp-video";
}
class Command final : public QUndoCommand {
    std::function<void(bool)> apply_;

  public:
    Command(QString label, std::function<void(bool)> apply) : QUndoCommand(label), apply_(std::move(apply)) {}
    void undo() override { apply_(false); }
    void redo() override { apply_(true); }
};
} // namespace
int QuestionModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : document.questions.size();
}
QVariant QuestionModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const auto &q = document.questions[index.row()];
    switch (role) {
    case Uid:
        return q.id;
    case Title:
        return q.text;
    case Category:
        return q.category;
    case Difficulty:
        return q.difficulty;
    case Image:
        return !q.image.isEmpty();
    case Audio:
        return !q.speech.isEmpty();
    case Music:
        return q.usesMusic();
    case ContentType:
        return int(q.contentType());
    case Valid:
        return textValid(q);
    case Source:
        return displaySource(q.source);
    default:
        return {};
    }
}
QHash<int, QByteArray> QuestionModel::roleNames() const {
    return {{Uid, "uid"},          {Title, "questionTitle"}, {Category, "categoryLabel"},
            {Difficulty, "level"}, {Image, "hasImage"},      {Audio, "hasAudio"},
            {Valid, "isValid"},    {Source, "sourceLabel"}, {ContentType, "contentType"}, {Music, "usesMusic"}};
}
void QuestionModel::reset(studio::Document value) {
    beginResetModel();
    document = std::move(value);
    endResetModel();
}
void QuestionModel::replace(int row, const std::optional<studio::Question> &value, bool insert) {
    if (value && insert) {
        beginInsertRows({}, row, row);
        document.questions.insert(row, *value);
        endInsertRows();
    } else if (value) {
        document.questions[row] = *value;
        emit dataChanged(index(row), index(row));
    } else {
        beginRemoveRows({}, row, row);
        document.questions.removeAt(row);
        endRemoveRows();
    }
}
int QuestionModel::find(const QString &id) const {
    for (int i = 0; i < rowCount(); ++i)
        if (document.questions[i].id == id)
            return i;
    return -1;
}
bool QuestionFilter::filterAcceptsRow(int row, const QModelIndex &) const {
    const auto *model = static_cast<const QuestionModel *>(sourceModel());
    const auto &q = model->document.questions[row];
    if (!category.isEmpty() && q.category != category)
        return false;
    if (kind == "images" && q.image.isEmpty())
        return false;
    if (kind == "audio" && q.speech.isEmpty())
        return false;
    if (kind == "music" && !q.usesMusic())
        return false;
    if (kind == "drafts" && textValid(q))
        return false;
    if (text.isEmpty())
        return true;
    return (q.text + " " + q.answers.join(' ') + " " + q.category + " " + q.id + " " + q.source)
        .contains(text, Qt::CaseInsensitive);
}
StudioController::StudioController(QObject *parent, const QString &newProjectLanguage)
    : QObject(parent), newProjectLanguage_(newProjectLanguage), model_(this), filter_(this), history_(this) {
    filter_.setSourceModel(&model_);
    filter_.setDynamicSortFilter(true);
    connect(&history_, &QUndoStack::indexChanged, this, &StudioController::historyChanged);
    connect(&history_, &QUndoStack::cleanChanged, this, &StudioController::historyChanged);
    const auto data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(data);
    recoveryPath_ = QDir(data).filePath("recovery.json");
    auto *timer = new QTimer(this);
    timer->setInterval(30000);
    connect(timer, &QTimer::timeout, this, [this] {
        if (dirty() && (!busy_ || buildActive())) {
            QString error;
            if (!studio::saveDocument(model_.document, recoveryPath_, &error))
                setStatus(tr("Nie udało się zapisać kopii odzyskiwania: ") + error);
        }
    });
    timer->start();
    newProject();
}
StudioController::~StudioController() {
    if (buildCancel_)
        buildCancel_->store(true);
}
QVariantMap StudioController::buildSettings() const {
    auto values = studio::suggestBuildSettings(model_.document.language);
    if (model_.document.packExtra.contains("number"))
        values["number"] = model_.document.packExtra["number"].toInt();
    if (model_.document.packExtra.contains("category_base"))
        values["categoryBase"] = model_.document.packExtra["category_base"].toInt();
    return values;
}
void StudioController::startPackJob(
    std::function<studio::BuildResult(const studio::pack::Progress &, std::atomic_bool *)> job) {
    if (busy_)
        return;
    busy_ = true;
    buildProgress_ = 0;
    buildCancel_ = std::make_shared<std::atomic_bool>(false);
    emit busyChanged();
    emit buildProgressChanged();
    setStatus(tr("Przygotowanie paczki…"));
    const auto cancel = buildCancel_;
    const QPointer<StudioController> guard(this);
    auto *dispatcher = QCoreApplication::instance();
    const studio::pack::Progress progress = [dispatcher, guard, cancel](int percent, const QString &message) {
        QMetaObject::invokeMethod(
            dispatcher,
            [guard, cancel, percent, message] {
                if (!guard || guard->buildCancel_ != cancel || guard->buildCancel_->load() ||
                    percent < guard->buildProgress_)
                    return;
                guard->buildProgress_ = percent;
                guard->setStatus(message);
                emit guard->buildProgressChanged();
            },
            Qt::QueuedConnection);
    };
    auto *watcher = new QFutureWatcher<studio::BuildResult>(this);
    connect(watcher, &QFutureWatcher<studio::BuildResult>::finished, this, [this, watcher] {
        const auto result = watcher->result();
        watcher->deleteLater();
        busy_ = false;
        buildCancel_.reset();
        lastBuild_ = result.map();
        lastBuild_["fileUrl"] =
            result.path.isEmpty() ? QString() : QUrl::fromLocalFile(result.path).toString();
        emit busyChanged();
        emit buildProgressChanged();
        if (result.cancelled) {
            setStatus(result.path.isEmpty() ? tr("Budowanie anulowane")
                                            : tr("Paczka zbudowana; instalacja anulowana"));
        } else if (!result.ok()) {
            setStatus(tr("Operacja na paczce nie powiodła się"));
            if (!result.excluded.isEmpty())
                emit packReviewRequired(lastBuild_);
            else
                emit errorOccurred(result.error + (result.path.isEmpty()
                                                       ? QString()
                                                       : tr("\n\nZbudowany plik: ") + result.path));
        } else {
            buildProgress_ = 100;
            setStatus(result.installedPath.isEmpty() ? tr("Paczka zbudowana i zweryfikowana")
                                                     : tr("Paczka zainstalowana; uruchom grę ponownie"));
            emit packFinished(lastBuild_);
        }
    });
    watcher->setFuture(QtConcurrent::run([job, cancel, progress] { return job(progress, cancel.get()); }));
}
void StudioController::buildPack(const QVariantMap &options, bool install) {
    if (busy_)
        return;
    studio::BuildRequest request;
    request.number = options["number"].toInt();
    request.categoryBase = options["categoryBase"].toInt();
    request.templatePath = studio::localPath(options["templatePath"].toString());
    request.outputDirectory = studio::localPath(options["outputDirectory"].toString());
    request.ffmpeg = studio::localPath(options["ffmpeg"].toString());
    request.runtimeRoot = studio::localPath(options["runtimeRoot"].toString());
    request.titleId = options["titleId"].toString();
    request.install = install;
    request.skipInvalid = options["skipInvalid"].toBool();
    const auto before = model_.document.packExtra;
    auto after = before;
    after["number"] = request.number;
    after["category_base"] = request.categoryBase;
    if (before != after)
        history_.push(new Command(tr("Ustawienia paczki"), [this, before, after](bool forward) {
            model_.document.packExtra = forward ? after : before;
            notifyContent();
        }));
    request.document = model_.document;
    QSettings settings;
    for (auto it = options.constBegin(); it != options.constEnd(); ++it)
        settings.setValue("build/" + it.key(), it.value());
    startPackJob([request](const studio::pack::Progress &progress, std::atomic_bool *cancel) {
        return studio::buildPack(request, progress, cancel);
    });
}
void StudioController::installPack(const QUrl &source, const QString &runtimeRoot, const QString &titleId) {
    if (busy_ || !source.isLocalFile())
        return;
    const auto root = studio::localPath(runtimeRoot);
    QSettings settings;
    settings.setValue("build/runtimeRoot", root);
    settings.setValue("build/titleId", titleId);
    startPackJob([source, root, titleId](const studio::pack::Progress &progress, std::atomic_bool *cancel) {
        return studio::installPack(source.toLocalFile(), root, titleId, progress, cancel);
    });
}
void StudioController::cancelBuild() {
    if (buildCancel_) {
        buildCancel_->store(true);
        setStatus(tr("Anulowanie…"));
    }
}
void StudioController::revealFile(const QUrl &url) {
    if (url.isLocalFile())
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(url.toLocalFile()).absolutePath()));
}
void StudioController::notifyContent() {
    emit contentChanged();
    emit selectionChanged();
    emit filterChanged();
}
void StudioController::setStatus(const QString &text) {
    status_ = text;
    emit statusChanged();
}
QStringList StudioController::recentFiles() const { return QSettings().value("recentFiles").toStringList(); }
void StudioController::remember(const QString &path) {
    auto files = recentFiles();
    files.removeAll(path);
    files.prepend(path);
    while (files.size() > 8)
        files.removeLast();
    QSettings().setValue("recentFiles", files);
}
bool StudioController::recoveryAvailable() const { return QFileInfo::exists(recoveryPath_); }
int StudioController::imageCount() const {
    return int(std::count_if(model_.document.questions.begin(), model_.document.questions.end(),
                             [](const studio::Question &q) { return !q.image.isEmpty(); }));
}
int StudioController::audioCount() const {
    return int(std::count_if(model_.document.questions.begin(), model_.document.questions.end(),
                             [](const studio::Question &q) { return !q.speech.isEmpty(); }));
}
int StudioController::musicCount() const {
    return int(std::count_if(model_.document.questions.cbegin(), model_.document.questions.cend(),
                             [](const studio::Question &q) { return q.usesMusic(); }));
}
QVariantList StudioController::categories() const {
    QMap<QString, int> counts;
    for (const auto &q : model_.document.questions)
        ++counts[q.category];
    QVariantList out;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        out << QVariantMap{{"name", it.key()}, {"count", it.value()}};
    return out;
}
QVariantMap StudioController::current() const {
    const int row = model_.find(selected_);
    if (row < 0)
        return {};
    const auto &q = model_.document.questions[row];
    auto map = q.json().toVariantMap();
    map["problems"] = q.problems();
    map["sourceLabel"] = displaySource(q.source);
    map["imageUrl"] = q.image.isEmpty() ? QString() : QUrl::fromLocalFile(q.image).toString();
    map["canDecodeSourceMedia"] = q.hasUnresolvedVisualMedia() && studio::hasSourceQuestionMedia(q);
    map["canRecoverVideo"] = !QFileInfo(q.video).isFile() && !studio::sourceQuestionClipKey(q).isEmpty();
    map["hasMusicClip"] = !q.music.isEmpty() && !q.hasUnresolvedMusic();
    map["usesMusic"] = q.usesMusic();
    map["canRecoverMusic"] = studio::hasBuzzSourceMusic(q);
    map["canRecoverNarration"] = studio::hasPs2Narration(q) || studio::hasPspNarration(q);
    return map;
}
QVariantList StudioController::projectLanguages() const {
    return studio::languageChoices(model_.document.languages());
}
QVariantList StudioController::languageOptions() const { return studio::languageChoices(); }
QVariantList StudioController::editingLanguageOptions() const {
    QStringList codes;
    for (const auto &entry : Localization::languages())
        codes << entry.first;
    for (const auto &code : model_.document.languages())
        if (!codes.contains(code))
            codes << code;
    return studio::languageChoices(codes);
}
void StudioController::refreshTranslations() {
    setStatus(tr("Gotowe do tworzenia"));
    if (model_.rowCount())
        emit model_.dataChanged(model_.index(0), model_.index(model_.rowCount() - 1),
                                {QuestionModel::Source});
    notifyContent();
}
void StudioController::newProject(bool example, const QString &exampleLanguage) {
    if (busy_)
        return;
    history_.clear();
    const auto sampleLanguage = exampleLanguage.isEmpty() ? newProjectLanguage_ : exampleLanguage;
    model_.reset(example ? studio::exampleDocument(sampleLanguage) : studio::Document());
    if (!example)
        model_.document.language = newProjectLanguage_;
    if (!example)
        model_.document.title = tr("Moja paczka");
    selected_ = model_.rowCount() ? model_.document.questions[0].id : QString();
    projectPath_.clear();
    warnings_.clear();
    lastImportedQuestions_.clear();
    importedQuestionsWithGameCategory_ = 0;
    emit projectReplaced();
    setFilter("", "", "");
    notifyContent();
    setStatus(example ? tr("Otworzono przykładowy projekt") : tr("Nowy projekt"));
}
void StudioController::prepareImport(const QUrl &url, bool append, bool guided) {
    if (busy_ || !pendingImport_.isEmpty() || !url.isLocalFile())
        return;
    busy_ = true;
    emit busyChanged();
    setStatus(tr("Sprawdzanie języków źródła…"));
    const auto path = url.toLocalFile();
    auto *watcher = new QFutureWatcher<studio::SourceLanguages>(this);
    connect(watcher, &QFutureWatcher<studio::SourceLanguages>::finished, this, [this, watcher, url, append, guided] {
        const auto result = watcher->result();
        watcher->deleteLater();
        busy_ = false;
        emit busyChanged();
        if (!result.ok() && !result.license.isEmpty()) {
            setStatus(tr("Potrzebna licencja paczki"));
            emit licenseRequested(url, append, result.license, result.error);
        } else if (!result.ok()) {
            setStatus(tr("Nie udało się wczytać pliku"));
            emit errorOccurred(result.error);
        } else if (guided || (!result.project && result.codes.size() > 1)) {
            pendingImport_ = url;
            pendingAppend_ = append;
            if (!guided)
                setStatus(tr("Wybierz język importu"));
            const auto choices = studio::languageChoices(result.codes);
            if (guided)
                emit importSourcePrepared(choices, result.codes.contains(newProjectLanguage_)
                    ? newProjectLanguage_ : result.preferred, result.project);
            else
                emit importLanguageRequested(choices, result.preferred);
        } else {
            openFile(url, append, result.project ? QString() : result.preferred);
        }
    });
    watcher->setFuture(QtConcurrent::run([path] { return studio::inspectLanguages(path); }));
}
QString StudioController::addLicense(const QString &contentId, const QUrl &rap) {
    QString error;
    if (!rap.isLocalFile() || !studio::ps3::rememberLicense(contentId, rap.toLocalFile(), &error))
        return error.isEmpty() ? tr("Wybierz plik licencji .rap.") : error;
    return {};
}
void StudioController::finishImport(const QString &language) {
    finishImport(language, pendingAppend_);
}
void StudioController::finishImport(const QString &language, bool append) {
    if (pendingImport_.isEmpty())
        return;
    const auto url = pendingImport_;
    pendingImport_.clear();
    openFile(url, append, language);
}
void StudioController::cancelImport() {
    pendingImport_.clear();
    setStatus(tr("Import anulowany"));
}
void StudioController::openFile(const QUrl &url, bool append, const QString &language) {
    if (busy_ || !url.isLocalFile())
        return;
    const auto path = url.toLocalFile();
    busy_ = true;
    emit busyChanged();
    setStatus(tr("Wczytywanie pytań…"));
    auto *watcher = new QFutureWatcher<studio::LoadResult>(this);
    connect(watcher, &QFutureWatcher<studio::LoadResult>::finished, this, [this, watcher, path, append] {
        auto result = watcher->result();
        watcher->deleteLater();
        busy_ = false;
        emit busyChanged();
        if (!result.ok() && !result.license.isEmpty()) {
            setStatus(tr("Potrzebna licencja paczki"));
            emit licenseRequested(QUrl::fromLocalFile(path), append, result.license, result.error);
            return;
        }
        if (!result.ok()) {
            setStatus(tr("Nie udało się wczytać pliku"));
            emit errorOccurred(result.error);
            return;
        }
        const int previousCount = model_.rowCount();
        const bool appending = append && (previousCount > 0 || dirty() || !projectPath_.isEmpty());
        int updated = 0, unchanged = 0;
        int keptCategories = 0;
        QSet<QString> importedQuestions;
        auto warnings = result.warnings;
        if (appending) {
            auto before = model_.document;
            auto after = before;
            QMap<QString, int> ids;
            for (int i = 0; i < before.questions.size(); ++i)
                ids.insert(before.questions[i].id, i);
            int merged = 0, restoredSpeech = 0;
            QMap<int, int> categories;
            try {
                categories = studio::mergeCustomCategories(after, result.document.categories);
            } catch (const std::exception &e) {
                emit errorOccurred(QString::fromUtf8(e.what()));
                return;
            }
            for (auto q : result.document.questions) {
                q.subjectId = categories.value(q.subjectId, q.subjectId);
                q.categoryId = categories.value(q.categoryId, q.categoryId);
                if (ids.contains(q.id)) {
                    auto &existing = after.questions[ids[q.id]];
                    const auto sourceLabels = q.extra.value("source_category_labels");
                    const bool refreshedLabels = existing.source == q.source && sourceLabels.isObject() &&
                                                 existing.extra.value("source_category_labels") != sourceLabels;
                    if (refreshedLabels)
                        existing.extra["source_category_labels"] = sourceLabels;
                    auto variants = q.translations;
                    variants[q.language] = q.localized();
                    bool changed = false, attachedMedia = false, savedOrigins = false;
                    for (auto it = variants.constBegin(); it != variants.constEnd(); ++it) {
                        if (it.key() == existing.language && emptyTranslation(existing.localized())) {
                            existing.loadLocalized(it.value().toObject());
                            existing.translations[it.key()] = it.value();
                            changed = true;
                            continue;
                        }
                        // Only entirely empty drafts are replaced.
                        if (it.key() != existing.language &&
                            (!existing.translations.contains(it.key()) ||
                             emptyTranslation(existing.translations[it.key()].toObject()))) {
                            existing.translations[it.key()] = it.value();
                            changed = true;
                            continue;
                        }
                        // The source voice is valid only for the same language and unchanged prompt.
                        const bool active = it.key() == existing.language;
                        auto current = active ? existing.localized() : existing.translations.value(it.key()).toObject();
                        const auto imported = it.value().toObject();
                        if (result.gameImport && existing.source == q.source &&
                            current["question"] == imported["question"] && current["answers"] == imported["answers"] &&
                            current["correct_index"] == imported["correct_index"] &&
                            !QFileInfo(current["image"].toString()).isFile() &&
                            !QFileInfo(current["video"].toString()).isFile()) {
                            auto incoming = q;
                            incoming.switchLanguage(it.key());
                            const bool recoverable = canRecoverVisual(incoming);
                            const bool hasImage = QFileInfo(incoming.image).isFile();
                            const bool hasVideo = QFileInfo(incoming.video).isFile();
                            if (recoverable || hasImage || hasVideo) {
                                const auto beforeMedia = current;
                                current["image"] = hasImage ? incoming.image : QString();
                                if (hasVideo)
                                    current["video"] = incoming.video;
                                else
                                    current.remove("video");
                                if (recoverable) {
                                    QJsonArray refs;
                                    for (const auto &ref : current["source_media"].toArray())
                                        if (!isVisualReference(ref))
                                            refs << ref;
                                    for (const auto &ref : imported["source_media"].toArray())
                                        if (isVisualReference(ref))
                                            refs << ref;
                                    current["source_media"] = refs;
                                    for (const auto *key : {"source_archive", "source_round"})
                                        if (imported.contains(key))
                                            current[key] = imported[key];
                                    if (!hasImage && !hasVideo)
                                        importedQuestions.insert(q.id);
                                }
                                if (current != beforeMedia) {
                                    existing.translations[it.key()] = current;
                                    if (active)
                                        existing.loadLocalized(current);
                                    attachedMedia = true;
                                }
                            }
                        }
                        auto origins = current["source_rounds"].toArray();
                        const auto incomingOrigins = imported["source_rounds"].toArray();
                        const auto beforeOrigins = origins.size();
                        for (const auto &origin : incomingOrigins)
                            if (!origins.contains(origin))
                                origins << origin;
                        if (origins.size() != beforeOrigins) {
                            current["source_rounds"] = origins;
                            existing.translations[it.key()] = current;
                            savedOrigins = true;
                        }
                        const auto speech = imported["speech"].toString();
                        if (current["speech"].toString().isEmpty() && current["question"] == imported["question"] &&
                            !speech.isEmpty() && QFileInfo(speech).isFile()) {
                            current["speech"] = speech;
                            existing.translations[it.key()] = current;
                            if (active)
                                existing.speech = speech;
                            attachedMedia = true;
                            ++restoredSpeech;
                        }
                        const auto music = imported["music"].toString();
                        if (result.gameImport && !music.isEmpty() && current["question"] == imported["question"] &&
                            !QFileInfo(current["music"].toString()).isFile() && QFileInfo(music).isFile()) {
                            const auto refs = current["source_media"].toArray();
                            if (std::any_of(refs.cbegin(), refs.cend(), [](const auto &ref) {
                                    return ref.toObject().value("type") == 1;
                                })) {
                                current["music"] = music;
                                existing.translations[it.key()] = current;
                                if (active)
                                    existing.music = music;
                                attachedMedia = true;
                            }
                        }
                    }
                    if (changed)
                        ++merged;
                    if (changed || attachedMedia || savedOrigins || refreshedLabels)
                        ++updated;
                    else
                        ++unchanged;
                    continue;
                }
                q.switchLanguage(after.language);
                importedQuestions.insert(q.id);
                ids.insert(q.id, after.questions.size());
                after.questions << q;
            }
            if (result.gameImport)
                keptCategories = studio::retainGameCategories(after, previousCount, result.document.language, warnings);
            else
                after.refreshCategoryNames();
            if (unchanged)
                warnings << tr("Pominięto %1 istniejących identyfikatorów.").arg(unchanged);
            if (merged)
                warnings << tr("Dodano wersje językowe do %1 istniejących pytań.").arg(merged);
            if (restoredSpeech)
                warnings << tr("Uzupełnione nagrania lektora: %1.").arg(restoredSpeech);
            if (after.questions.size() != previousCount || updated || before.categories != after.categories) {
                const auto selected = selected_;
                const auto importedSelection = selected.isEmpty() && !after.questions.isEmpty()
                                                   ? after.questions.first().id : selected;
                history_.push(new Command(tr("Import pytań"), [this, before, after, selected, importedSelection](bool forward) {
                    model_.reset(forward ? after : before);
                    selected_ = forward ? importedSelection : selected;
                    notifyContent();
                }));
            }
        } else {
            history_.clear();
            if (result.gameImport)
                keptCategories = studio::retainGameCategories(result.document, 0, result.document.language, warnings);
            model_.reset(std::move(result.document));
            for (const auto &q : std::as_const(model_.document.questions))
                importedQuestions.insert(q.id);
            selected_ = model_.rowCount() ? model_.document.questions[0].id : QString();
            // Imported archives/JSON remain sources; Save As creates a Studio project.
            projectPath_ = !result.projectPath.isEmpty()                    ? result.projectPath
                           : path.endsWith(".buzz.json", Qt::CaseInsensitive) ? path
                                                                              : QString();
            if (projectPath_.isEmpty())
                history_.resetClean();
            else
                history_.setClean();
            emit projectReplaced();
            setFilter("", "", "");
        }
        warnings_ = std::move(warnings);
        importedQuestionsWithGameCategory_ = keptCategories;
        lastImportedQuestions_ = std::move(importedQuestions);
        remember(path);
        notifyContent();
        setStatus(
            QString(tr("Wczytano %1 pytań%2"))
                .arg(result.document.questions.size() ? result.document.questions.size() : model_.rowCount())
                .arg(warnings_.isEmpty() ? QString() : tr(" · sprawdź uwagi importu")));
        emit importFinished(model_.rowCount() - (appending ? previousCount : 0), updated, unchanged);
    });
    watcher->setFuture(QtConcurrent::run([path, language] { return studio::loadDocument(path, language); }));
}
bool StudioController::saveFile(const QUrl &url, bool legacy) {
    if (busy_)
        return false;
    const QString path = url.isLocalFile() ? url.toLocalFile() : projectPath_;
    if (path.isEmpty())
        return false;
    QString error;
    studio::MediaReport media;
    if (!legacy) {
        // A project keeps its own copy of every picture, recording and video.
        media = studio::collectMedia(model_.document, QFileInfo(path).absolutePath());
        if (!media.ok()) {
            emit errorOccurred(media.error);
            return false;
        }
    }
    if (!studio::saveDocument(model_.document, path, &error, legacy)) {
        emit errorOccurred(error);
        return false;
    }
    if (!legacy) {
        projectPath_ = path;
        history_.setClean();
        QFile::remove(recoveryPath_);
        remember(path);
    }
    if (legacy)
        setStatus(tr("Wyeksportowano JSON dla buzzpack"));
    else if (media.missing)
        setStatus(tr("Projekt zapisany · brakuje %1 plików mediów").arg(media.missing));
    else if (media.copied)
        setStatus(tr("Projekt zapisany · skopiowano %1 plików do folderu media").arg(media.copied));
    else
        setStatus(tr("Projekt zapisany"));
    notifyContent();
    return true;
}
bool StudioController::exportProject(const QUrl &url) {
    if (busy_ || !url.isLocalFile())
        return false;
    const auto report = studio::exportProjectArchive(model_.document, url.toLocalFile());
    if (!report.ok()) {
        emit errorOccurred(report.error);
        return false;
    }
    setStatus(report.missing ? tr("Wyeksportowano projekt · brakuje %1 plików mediów").arg(report.missing)
                             : tr("Wyeksportowano projekt z %1 plikami mediów").arg(report.copied));
    return true;
}
void StudioController::select(const QString &id) {
    if (busy_)
        return;
    if (model_.find(id) < 0)
        return;
    selected_ = id;
    emit selectionChanged();
}
void StudioController::setFilter(const QString &text, const QString &category, const QString &kind) {
    filter_.text = text;
    filter_.category = category;
    filter_.kind = kind;
    filter_.refresh();
    emit filterChanged();
}
void StudioController::edit(int row, std::optional<studio::Question> before,
                            std::optional<studio::Question> after, const QString &label) {
    history_.push(new Command(label, [this, row, before, after](bool forward) {
        const auto value = forward ? after : before;
        const auto previous = forward ? before : after;
        model_.replace(row, value, !previous.has_value());
        if (value)
            selected_ = value->id;
        else
            selected_ = model_.rowCount() ? model_.document.questions[std::min(row, model_.rowCount() - 1)].id
                                          : QString();
        notifyContent();
    }));
}
void StudioController::updateCurrent(const QVariantMap &patch) {
    if (busy_)
        return;
    const int row = model_.find(selected_);
    if (row < 0)
        return;
    const auto before = model_.document.questions[row];
    auto q = before;
    if (patch.contains("question"))
        q.text = patch["question"].toString();
    if (patch.contains("category_name"))
        q.category = patch["category_name"].toString().trimmed();
    if (patch.contains("difficulty"))
        q.difficulty = std::clamp(patch["difficulty"].toInt(), 1, 3);
    if (patch.contains("subject"))
        q.subjectId = patch["subject"].toInt();
    if (patch.contains("category"))
        q.categoryId = patch["category"].toInt();
    if (q.subjectId != before.subjectId || q.categoryId != before.categoryId || q.category != before.category) {
        const auto original = before.sourceCategoryPath();
        if (!original.isEmpty() && !q.extra["source_category_path"].isArray())
            q.extra["source_category_path"] = QJsonArray::fromStringList(original);
    }
    if (patch.contains("correct_index"))
        q.correct = std::clamp(patch["correct_index"].toInt(), 0, 3);
    if (patch.contains("tts_text"))
        q.ttsText = patch["tts_text"].toString();
    if (patch.contains("image"))
        q.image = studio::localPath(patch["image"].toString());
    if (patch.contains("speech"))
        q.speech = studio::localPath(patch["speech"].toString());
    if (patch.contains("video"))
        q.video = studio::localPath(patch["video"].toString());
    if (patch.contains("answer_index")) {
        const int i = patch["answer_index"].toInt();
        if (i >= 0 && i < 4)
            q.answers[i] = patch["answer_text"].toString();
    }
    if (q.json() != before.json())
        edit(row, before, q, tr("Edycja pytania"));
}
void StudioController::addQuestion() {
    if (busy_)
        return;
    auto q = studio::newQuestion();
    q.language = model_.document.language;
    if (!filter_.category.isEmpty())
        q.category = filter_.category;
    edit(model_.rowCount(), std::nullopt, q, tr("Nowe pytanie"));
    setFilter("", filter_.category, "");
}
void StudioController::duplicateCurrent() {
    if (busy_)
        return;
    const int row = model_.find(selected_);
    if (row < 0)
        return;
    auto q = model_.document.questions[row];
    q.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    edit(row + 1, std::nullopt, q, tr("Duplikowanie pytania"));
}
void StudioController::deleteCurrent() {
    if (busy_)
        return;
    const int row = model_.find(selected_);
    if (row >= 0)
        edit(row, model_.document.questions[row], std::nullopt, tr("Usunięcie pytania"));
}
void StudioController::setPackTitle(const QString &title) {
    if (busy_ || title.trimmed().isEmpty() || title == packTitle())
        return;
    const auto old = packTitle();
    history_.push(new Command(tr("Nazwa paczki"), [this, old, title](bool forward) {
        model_.document.title = forward ? title : old;
        notifyContent();
    }));
}
QVariantList StudioController::packCategories() const {
    QVariantList out;
    for (const bool subjects : {true, false})
        for (const auto &c : model_.document.categories)
            if ((c.parent == 0) == subjects)
                out << QVariantMap{{"id", c.id},
                                   {"parent", c.parent},
                                   {"name", model_.document.categoryLabel(c.id)},
                                   {"names", c.names.toVariantMap()}};
    return out;
}
bool StudioController::hasImportedSections(bool latestOnly) const {
    const auto &questions = model_.document.questions;
    return std::any_of(questions.cbegin(), questions.cend(), [&](const studio::Question &question) {
        return (!latestOnly || lastImportedQuestions_.contains(question.id)) &&
               !question.sourceCategoryPath().isEmpty();
    });
}

QVector<studio::ImportedSection> StudioController::importedSections(bool latestOnly) const {
    QMap<QString, studio::ImportedSection> sections;
    for (const auto &question : model_.document.questions) {
        if (latestOnly && !lastImportedQuestions_.contains(question.id))
            continue;
        const auto path = question.sourceCategoryPath();
        if (path.isEmpty())
            continue;
        auto key = QJsonArray::fromStringList(path);
        key.prepend(question.source);
        auto &section = sections[QString::fromUtf8(QJsonDocument(key).toJson(QJsonDocument::Compact))];
        if (!section.questions) {
            section.game = question.source;
            section.path = path;
        }
        ++section.questions;
    }
    return sections.values();
}

int StudioController::assignImportedSection(const QString &game, const QStringList &section, int subject, int category,
                                           const QString &subjectName, bool latestOnly) {
    if (busy_ || section.isEmpty() || subject <= 0 || category <= 0 || subject == category)
        return 0;
    const auto before = model_.document;
    if (studio::isCustomCategory(subject) || studio::isCustomCategory(category)) {
        const auto *root = before.customCategory(subject);
        const auto *child = before.customCategory(category);
        if (!root || root->parent || !child || child->parent != subject)
            return 0;
    }
    const auto name = studio::isCustomCategory(subject) ? before.categoryLabel(subject) : subjectName;
    auto after = before;
    int changed = 0;
    for (auto &q : after.questions) {
        if (q.source != game || (latestOnly && !lastImportedQuestions_.contains(q.id)))
            continue;
        if (q.subjectId == subject && q.categoryId == category)
            continue;
        const auto original = q.sourceCategoryPath();
        if (original.size() < section.size() || original.first(section.size()) != section)
            continue;
        if (!q.extra["source_category_path"].isArray())
            q.extra["source_category_path"] = QJsonArray::fromStringList(original);
        q.extra["category_override"] = true;
        q.subjectId = subject;
        q.categoryId = category;
        q.category = name;
        ++changed;
    }
    if (changed) {
        history_.push(new Command(tr("Zbiorcze przypisanie kategorii"), [this, before, after](bool forward) {
            model_.reset(forward ? after : before);
            notifyContent();
        }));
    }
    return changed;
}
void StudioController::setPackCategories(const QVariantList &list) {
    if (busy_)
        return;
    QVector<studio::CustomCategory> categories;
    QSet<int> ids, subjects;
    for (const auto &value : list) {
        const auto map = value.toMap();
        studio::CustomCategory c{map["id"].toInt(), map["parent"].toInt(), {}};
        if (const auto *original = model_.document.customCategory(c.id); original && original->parent == c.parent) {
            c.sourceKey = original->sourceKey;
            c.sourceName = original->sourceName;
        }
        const auto names = map["names"].toMap();
        for (auto it = names.cbegin(); it != names.cend(); ++it) {
            const auto name = it.value().toString().simplified().left(studio::customCategoryNameLimit);
            if (!name.isEmpty())
                c.names[it.key()] = name;
        }
        if (!studio::isCustomCategory(c.id) || ids.contains(c.id) ||
            (c.parent && !subjects.contains(c.parent)))
            return; // Subjects come before their subcategories.
        ids.insert(c.id);
        if (!c.parent)
            subjects.insert(c.id);
        categories << c;
    }
    if (categories.size() > 99 || categories == model_.document.categories)
        return;
    const auto before = model_.document;
    auto after = before;
    after.categories = categories;
    for (auto &q : after.questions) {
        const bool lostSubject = studio::isCustomCategory(q.subjectId) && !ids.contains(q.subjectId);
        const bool lostCategory = studio::isCustomCategory(q.categoryId) && !ids.contains(q.categoryId);
        if (lostSubject) {
            q.subjectId = q.categoryId = 0;
            q.category.clear();
        } else if (lostCategory) {
            // The subject's first remaining subcategory, or none.
            q.categoryId = 0;
            for (const auto &c : categories)
                if (c.parent == q.subjectId) {
                    q.categoryId = c.id;
                    break;
                }
        }
    }
    after.refreshCategoryNames();
    const auto selected = selected_;
    history_.push(new Command(tr("Kategorie paczki"), [this, before, after, selected](bool forward) {
        model_.reset(forward ? after : before);
        selected_ = selected;
        notifyContent();
    }));
}
int StudioController::addPackSubject(const QVariantMap &names) {
    if (busy_ || model_.document.categories.size() > 97)
        return 0;
    auto list = packCategories();
    const int subject = studio::nextCustomCategoryId(model_.document);
    list.prepend(QVariantMap{{"id", subject}, {"parent", 0}, {"names", names}});
    list << QVariantMap{{"id", subject + 1}, {"parent", subject}, {"names", names}};
    setPackCategories(list);
    return model_.document.customCategory(subject) ? subject : 0;
}
void StudioController::setPackLanguage(const QString &value) {
    const auto code = studio::languageCode(value);
    if (busy_ || code.isEmpty() || code == model_.document.language)
        return;
    if (model_.document.questions.isEmpty() && projectPath_.isEmpty() && !dirty()) {
        model_.document.switchLanguage(code);
        notifyContent();
        return;
    }
    const auto before = model_.document;
    auto after = before;
    after.switchLanguage(code);
    const auto selected = selected_;
    history_.push(new Command(tr("Język paczki"), [this, before, after, selected](bool forward) {
        model_.reset(forward ? after : before);
        selected_ = selected;
        notifyContent();
    }));
    setStatus(tr("Język pytań: %1").arg(studio::languageName(code)));
}
void StudioController::decodeSourceMedia(bool playVideo) {
    const auto row = model_.find(selected_);
    if (busy_ || row < 0)
        return;
    const auto q = model_.document.questions[row];
    if (playVideo ? studio::sourceQuestionClipKey(q).isEmpty() : !studio::hasSourceQuestionMedia(q))
        return;
    const auto ffmpeg = studio::configuredMediaFfmpeg();
    if (ffmpeg.isEmpty()) {
        emit errorOccurred(studio::missingMediaFfmpeg());
        return;
    }
    busy_ = true;
    setStatus(tr("Dekodowanie mediów źródłowych…"));
    emit busyChanged();
    auto *watcher = new QFutureWatcher<studio::Ps2Media>(this);
    connect(watcher, &QFutureWatcher<studio::Ps2Media>::finished, this, [this, watcher, q, playVideo] {
        const auto decoded = watcher->result();
        watcher->deleteLater();
        busy_ = false;
        emit busyChanged();
        if (!decoded.error.isEmpty()) {
            setStatus(tr("Nie udało się zdekodować mediów źródłowych."));
            emit errorOccurred(tr("Nie udało się zdekodować mediów źródłowych.") + "\n\n" + decoded.error);
            return;
        }
        const auto before = current();
        if (selected_ == q.id && before.value("language").toString() == q.language) {
            QVariantMap patch;
            if (before.value("image").toString().isEmpty() && !decoded.image.isEmpty())
                patch["image"] = decoded.image;
            if (!QFileInfo(before.value("video").toString()).isFile() && !decoded.video.isEmpty())
                patch["video"] = decoded.video;
            if (!patch.isEmpty())
                updateCurrent(patch);
            if (playVideo && !decoded.video.isEmpty())
                emit currentVideoRecovered(decoded.video);
        }
        setStatus(decoded.video.isEmpty() ? tr("Obraz zdekodowany.") : tr("Film zdekodowany."));
    });
    watcher->setFuture(QtConcurrent::run([q, ffmpeg] {
        return studio::decodeSourceQuestionMedia(q, ffmpeg);
    }));
}
void StudioController::recoverCurrentMusic(bool narration) {
    const auto row = model_.find(selected_);
    if (busy_ || row < 0)
        return;
    const auto question = model_.document.questions[row];
    if (narration ? !(studio::hasPs2Narration(question) || studio::hasPspNarration(question))
                  : !studio::hasBuzzSourceMusic(question))
        return;

    busy_ = true;
    setStatus(tr("Odzyskiwanie mediów z gry…"));
    emit busyChanged();
    auto *watcher = new QFutureWatcher<QPair<studio::Question, QStringList>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, row, question, narration] {
        const auto [recovered, warnings] = watcher->result();
        watcher->deleteLater();
        const bool resolved = narration ? !recovered.speech.isEmpty()
                                        : !recovered.music.isEmpty() && !recovered.hasUnresolvedMusic();
        if (resolved) {
            edit(row, question, recovered, tr("Odzyskaj media z gry"));
            setStatus(tr("Uzupełniono media (%1).").arg(1));
        }
        busy_ = false;
        emit busyChanged();
        if (!resolved) {
            const auto error = warnings.value(0, narration ? tr("Nie udało się zdekodować mediów źródłowych.")
                : QCoreApplication::translate("BuzzImport", "Nie udało się odzyskać klipu muzycznego."));
            setStatus(error);
            emit errorOccurred(error);
        } else if (selected_ == question.id && current().value("language").toString() == question.language) {
            if (narration)
                emit currentNarrationRecovered(recovered.speech);
            else
                emit currentMusicRecovered(recovered.music);
        }
    });
    watcher->setFuture(QtConcurrent::run([question, narration] {
        QVector<studio::Question> selected{question};
        QStringList warnings;
        if (narration) {
            auto &q = selected.first();
            try {
                q.speech = studio::hasPspNarration(question) ? studio::recoverPspNarration(question)
                                                           : studio::recoverPs2Narration(question);
                q.translations[q.language] = q.localized();
            } catch (const std::exception &e) {
                warnings << QString::fromUtf8(e.what());
            }
        } else {
            studio::recoverBuzzMusic(selected, warnings);
        }
        return qMakePair(selected.first(), warnings);
    }));
}
bool StudioController::hasUnresolvedSourceMedia() const {
    return std::any_of(model_.document.questions.cbegin(), model_.document.questions.cend(),
                       [](const auto &q) { return q.hasUnresolvedSourceMedia(); });
}
bool StudioController::hasRecoverableSourceMedia() const {
    const auto &questions = model_.document.questions;
    return std::any_of(questions.cbegin(), questions.cend(), [](const auto &question) {
        return studio::hasBuzzSourceMusic(question) ||
               (question.hasUnresolvedVisualMedia() &&
                (studio::hasSourceQuestionMedia(question) || studio::hasBuzzSourcePicture(question)));
    });
}
QFuture<studio::BuildReadiness> StudioController::checkBuildReadiness() const {
    return QtConcurrent::run([document = model_.document] { return studio::checkBuildReadiness(document); });
}

QString StudioController::currentBuildProblem() const {
    const int row = model_.find(selected_);
    return row < 0 ? QString() : studio::preflightBuildProblem(model_.document.questions.at(row));
}

bool StudioController::hasQuestion(const QString &id) const { return model_.find(id) >= 0; }

bool StudioController::packNeedsFfmpeg() const {
    return std::any_of(model_.document.questions.cbegin(), model_.document.questions.cend(), [](const auto &q) {
        const bool usesFfmpeg = !q.image.isEmpty() || !q.video.isEmpty() ||
                                studio::hasPspNarration(q) ||
                                (!q.speech.isEmpty() && !q.speech.endsWith(".rsf", Qt::CaseInsensitive)) ||
                                (!q.music.isEmpty() && !q.music.endsWith(".rsf", Qt::CaseInsensitive));
        return usesFfmpeg && q.problems().isEmpty();
    });
}
QVariantList StudioController::unresolvedMediaQuestions() const {
    QVariantList questions;
    for (const auto &q : model_.document.questions)
        if (q.hasUnresolvedSourceMedia())
            questions << QVariantMap{{"id", q.id}, {"text", q.text}, {"missingMusic", q.hasUnresolvedMusic()}};
    return questions;
}
studio::SourceMediaStatus StudioController::sourceMediaStatus(const QString &language, bool latestOnly) const {
    studio::SourceMediaStatus status;
    for (auto question : model_.document.questions) {
        if (latestOnly && !lastImportedQuestions_.contains(question.id))
            continue;
        if (!language.isEmpty())
            question.switchLanguage(language);
        const bool visual = question.hasUnresolvedVisualMedia(), music = question.hasUnresolvedMusic();
        const bool visualRecoverable = visual && canRecoverVisual(question);
        const bool musicRecoverable = music && studio::hasBuzzSourceMusic(question);
        status.pending += visual || music;
        status.recoverable += visualRecoverable || musicRecoverable;
        status.visualRecoverable += visualRecoverable;
        status.musicRecoverable += musicRecoverable;
        status.unavailable += visual && !visualRecoverable;
        status.musicMissing += music;
    }
    return status;
}
bool StudioController::recoverSourceMedia(const QString &selectedLanguage, bool latestOnly) {
    if (busy_)
        return false;
    struct Target {
        qsizetype row;
        QString language;
    };
    struct Job {
        studio::Question question;
        QList<Target> targets;
    };
    auto before = model_.document;
    QMap<QString, Job> jobs;
    QVector<studio::Question> stills, music;
    QList<Target> pictureTargets, musicTargets;
    for (qsizetype row = 0; row < before.questions.size(); ++row) {
        const auto &source = before.questions.at(row);
        if (latestOnly && !lastImportedQuestions_.contains(source.id))
            continue;
        auto languages = source.translations.keys();
        if (!languages.contains(source.language))
            languages << source.language;
        for (const auto &language : languages) {
            if (!selectedLanguage.isEmpty() && language != selectedLanguage)
                continue;
            auto q = source;
            q.switchLanguage(language);
            if (studio::hasBuzzSourceMusic(q)) {
                music << q;
                musicTargets << Target{row, language};
            }
            if (!q.hasUnresolvedVisualMedia())
                continue;
            const auto key = studio::sourceQuestionClipKey(q);
            if (key.isEmpty()) {
                if (studio::hasPs2Media(q) || studio::hasBuzzSourcePicture(q) || studio::hasPspPicture(q)) {
                    stills << std::move(q);
                    pictureTargets << Target{row, language};
                }
                continue;
            }
            auto i = jobs.find(key);
            if (i == jobs.end())
                i = jobs.insert(key, Job{std::move(q), {}});
            i->targets << Target{row, language};
        }
    }
    if (jobs.isEmpty() && stills.isEmpty() && music.isEmpty()) {
        setStatus(tr("Nie znaleziono mediów do odzyskania."));
        emit sourceMediaRecoveryFinished({});
        return false;
    }
    const auto ffmpeg = studio::configuredMediaFfmpeg();
    if (ffmpeg.isEmpty() && music.isEmpty()) {
        emit mediaFfmpegRequired();
        return false;
    }
    struct Result {
        studio::Document after;
        QStringList errors;
        int decoded = 0;
    };
    const auto cancel = sourceMediaCancel_ = std::make_shared<std::atomic_bool>(false);
    sourceMediaPercent_ = 0;
    const auto dispatcher = QCoreApplication::instance();
    const QPointer<StudioController> guard(this);
    const auto total = (ffmpeg.isEmpty() ? 0 : int(jobs.size()) + !stills.isEmpty()) + !music.isEmpty();
    busy_ = true;
    setStatus(tr("Odzyskiwanie mediów z gry…"));
    emit busyChanged();
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, before, cancel] {
        const auto result = watcher->result();
        watcher->deleteLater();
        sourceMediaCancel_.reset();
        if (result.decoded) {
            const auto after = result.after;
            history_.push(new Command(tr("Odzyskaj media z gry"), [this, before, after](bool forward) {
                model_.reset(forward ? after : before);
                notifyContent();
            }));
        }
        busy_ = false;
        emit busyChanged();
        setStatus(tr("Uzupełniono media (%1).").arg(result.decoded));
        if (!cancel->load())
            emit sourceMediaRecoveryFinished(result.errors);
        else
            emit sourceMediaRecoveryCancelled();
    });
    watcher->setFuture(QtConcurrent::run([before, jobs, stills = std::move(stills), pictureTargets, ffmpeg,
                                         music = std::move(music), musicTargets, cancel, guard, dispatcher, total]() mutable {
        Result result;
        result.after = before;
        int done = 0;
        const auto report = [&] {
            QMetaObject::invokeMethod(dispatcher, [guard, cancel, done = ++done, total] {
                if (guard && guard->sourceMediaCancel_ == cancel) {
                    guard->sourceMediaPercent_ = int(100ll * done / total);
                    emit guard->sourceMediaProgress(done, total);
                }
            }, Qt::QueuedConnection);
        };
        QSet<QPair<qsizetype, QString>> updated;
        const auto attach = [&](const Target &target, const auto &setMedia) {
            auto &q = result.after.questions[target.row];
            const auto active = q.language;
            q.switchLanguage(target.language);
            setMedia(q);
            q.translations[q.language] = q.localized();
            q.switchLanguage(active);
            updated.insert({target.row, target.language});
        };
        if (!music.isEmpty() && !cancel->load()) {
            studio::recoverBuzzMusic(music, result.errors, cancel.get());
            for (qsizetype i = 0; i < music.size(); ++i)
                if (!music[i].music.isEmpty() && !music[i].hasUnresolvedMusic())
                    attach(musicTargets[i], [&](auto &q) { q.music = music[i].music; });
            report();
        }
        if (!ffmpeg.isEmpty() && !stills.isEmpty() && !cancel->load()) {
            studio::importPs2Pictures(stills, ffmpeg, result.errors, cancel.get(), true);
            studio::recoverBuzzPictures(stills, ffmpeg, result.errors, cancel.get());
            studio::recoverPspPictures(stills, result.errors, cancel.get());
            for (qsizetype i = 0; i < stills.size(); ++i)
                if (!stills[i].image.isEmpty())
                    attach(pictureTargets[i], [&](auto &q) { q.image = stills[i].image; });
            report();
        }
        for (const auto &job : jobs) {
            if (ffmpeg.isEmpty() || cancel->load())
                break;
            const auto media = studio::decodeSourceQuestionMedia(job.question, ffmpeg, cancel.get());
            if (cancel->load())
                break;
            if (media.video.isEmpty()) {
                if (result.errors.size() < 5)
                    result.errors << job.question.text + ": " + media.error.left(300);
            } else {
                for (const auto &target : job.targets)
                    attach(target, [&](auto &q) {
                        q.image = media.image;
                        q.video = media.video;
                    });
            }
            report();
        }
        result.decoded = updated.size();
        return result;
    }));
    return true;
}

void StudioController::cancelSourceMediaRecovery() {
    if (sourceMediaCancel_) {
        sourceMediaCancel_->store(true);
        setStatus(tr("Anulowanie…"));
    }
}

QVariantList StudioController::ttsVoices() const {
    if (!voicesLoaded_) {
        cachedVoices_ = studio::speechVoices();
        voicesLoaded_ = true;
    }
    return cachedVoices_;
}
void StudioController::generateAudio(const QUrl &destination, const QString &text, const QString &voice,
                                     int rate) {
    if (busy_ || !destination.isLocalFile() || model_.find(selected_) < 0)
        return;
    const auto path = destination.toLocalFile();
    const auto id = selected_;
    busy_ = true;
    emit busyChanged();
    setStatus(tr("Generowanie nagrania lektora…"));
    auto *watcher = new QFutureWatcher<studio::SpeechResult>(this);
    connect(watcher, &QFutureWatcher<studio::SpeechResult>::finished, this, [this, watcher, id, text] {
        const auto result = watcher->result();
        watcher->deleteLater();
        busy_ = false;
        emit busyChanged();
        if (!result.ok()) {
            setStatus(tr("Nie udało się wygenerować nagrania"));
            emit errorOccurred(result.error);
            return;
        }
        selected_ = id;
        updateCurrent({{"speech", result.path}, {"tts_text", text}});
        setStatus(tr("Wygenerowano WAV i przypisano do pytania"));
    });
    watcher->setFuture(QtConcurrent::run(
        [text, voice, rate, path] { return studio::synthesizeSpeech(text, voice, rate, path); }));
}
void StudioController::recover() {
    if (recoveryAvailable())
        openFile(QUrl::fromLocalFile(recoveryPath_));
}
