#include "controller.h"
#include "languages.h"
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
    for (const auto &key : {"question", "image", "speech", "topic", "tts_text"})
        if (!value[key].toString().isEmpty())
            return false;
    for (const auto &answer : value["answers"].toArray())
        if (!answer.toString().isEmpty())
            return false;
    return true;
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
            {Valid, "isValid"},    {Source, "sourceLabel"}};
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
    if (kind == "drafts" && textValid(q))
        return false;
    if (text.isEmpty())
        return true;
    return (q.text + " " + q.answers.join(' ') + " " + q.category + " " + q.id + " " + q.source)
        .contains(text, Qt::CaseInsensitive);
}
StudioController::StudioController(QObject *parent)
    : QObject(parent), model_(this), filter_(this), history_(this) {
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
    return map;
}
QVariantList StudioController::projectLanguages() const {
    return studio::languageChoices(model_.document.languages());
}
QVariantList StudioController::languageOptions() const { return studio::languageChoices(); }
void StudioController::refreshTranslations() {
    setStatus(tr("Gotowe do tworzenia"));
    if (model_.rowCount())
        emit model_.dataChanged(model_.index(0), model_.index(model_.rowCount() - 1),
                                {QuestionModel::Source});
    notifyContent();
}
void StudioController::newProject(bool example) {
    if (busy_)
        return;
    history_.clear();
    model_.reset(example ? studio::exampleDocument() : studio::Document());
    if (!example)
        model_.document.title = tr("Moja paczka");
    selected_ = model_.rowCount() ? model_.document.questions[0].id : QString();
    projectPath_.clear();
    warnings_.clear();
    setFilter("", "", "");
    notifyContent();
    setStatus(example ? tr("Otworzono przykładowy projekt") : tr("Nowy projekt"));
}
void StudioController::prepareImport(const QUrl &url, bool append) {
    if (busy_ || !pendingImport_.isEmpty() || !url.isLocalFile())
        return;
    busy_ = true;
    emit busyChanged();
    setStatus(tr("Sprawdzanie języków źródła…"));
    const auto path = url.toLocalFile();
    auto *watcher = new QFutureWatcher<studio::SourceLanguages>(this);
    connect(watcher, &QFutureWatcher<studio::SourceLanguages>::finished, this, [this, watcher, url, append] {
        const auto result = watcher->result();
        watcher->deleteLater();
        busy_ = false;
        emit busyChanged();
        if (!result.ok()) {
            setStatus(tr("Nie udało się wczytać pliku"));
            emit errorOccurred(result.error);
        } else if (!result.project && result.codes.size() > 1) {
            pendingImport_ = url;
            pendingAppend_ = append;
            setStatus(tr("Wybierz język importu"));
            emit importLanguageRequested(studio::languageChoices(result.codes), result.preferred);
        } else {
            openFile(url, append, result.project ? QString() : result.preferred);
        }
    });
    watcher->setFuture(QtConcurrent::run([path] { return studio::inspectLanguages(path); }));
}
void StudioController::finishImport(const QString &language) {
    if (pendingImport_.isEmpty())
        return;
    const auto url = pendingImport_;
    const auto append = pendingAppend_;
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
        if (!result.ok()) {
            setStatus(tr("Nie udało się wczytać pliku"));
            emit errorOccurred(result.error);
            return;
        }
        warnings_ = result.warnings;
        if (append && model_.rowCount()) {
            auto before = model_.document;
            auto after = before;
            QMap<QString, int> ids;
            for (int i = 0; i < before.questions.size(); ++i)
                ids.insert(before.questions[i].id, i);
            int skipped = 0, merged = 0;
            for (auto q : result.document.questions) {
                if (ids.contains(q.id)) {
                    auto &existing = after.questions[ids[q.id]];
                    auto variants = q.translations;
                    variants[q.language] = q.localized();
                    bool changed = false;
                    for (auto it = variants.constBegin(); it != variants.constEnd(); ++it) {
                        if (it.key() == existing.language && emptyTranslation(existing.localized())) {
                            existing.loadLocalized(it.value().toObject());
                            existing.translations[it.key()] = it.value();
                            changed = true;
                            continue;
                        }
                        // Existing text/media always win; only entirely empty drafts are filled.
                        if (it.key() != existing.language &&
                            (!existing.translations.contains(it.key()) ||
                             emptyTranslation(existing.translations[it.key()].toObject()))) {
                            existing.translations[it.key()] = it.value();
                            changed = true;
                        }
                    }
                    if (changed)
                        ++merged;
                    else
                        ++skipped;
                    continue;
                }
                q.switchLanguage(after.language);
                ids.insert(q.id, after.questions.size());
                after.questions << q;
            }
            if (skipped)
                warnings_ << QString(tr("Pominięto %1 istniejących identyfikatorów.")).arg(skipped);
            if (merged)
                warnings_ << tr("Dodano wersje językowe do %1 istniejących pytań.").arg(merged);
            const auto selected = selected_;
            history_.push(new Command(tr("Import pytań"), [this, before, after, selected](bool forward) {
                model_.reset(forward ? after : before);
                selected_ = selected;
                notifyContent();
            }));
        } else {
            history_.clear();
            model_.reset(std::move(result.document));
            selected_ = model_.rowCount() ? model_.document.questions[0].id : QString();
            // Imported archives/JSON remain sources; Save As creates a Studio project.
            projectPath_ = path.endsWith(".buzz.json", Qt::CaseInsensitive) ? path : QString();
            if (projectPath_.isEmpty())
                history_.resetClean();
            else
                history_.setClean();
            setFilter("", "", "");
        }
        remember(path);
        notifyContent();
        setStatus(
            QString(tr("Wczytano %1 pytań%2"))
                .arg(result.document.questions.size() ? result.document.questions.size() : model_.rowCount())
                .arg(warnings_.isEmpty() ? QString() : tr(" · sprawdź uwagi importu")));
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
    setStatus(legacy ? tr("Wyeksportowano JSON dla buzzpack") : tr("Projekt zapisany"));
    notifyContent();
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
    if (patch.contains("correct_index"))
        q.correct = std::clamp(patch["correct_index"].toInt(), 0, 3);
    if (patch.contains("tts_text"))
        q.ttsText = patch["tts_text"].toString();
    if (patch.contains("image"))
        q.image = studio::localPath(patch["image"].toString());
    if (patch.contains("speech"))
        q.speech = studio::localPath(patch["speech"].toString());
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
void StudioController::setPackLanguage(const QString &value) {
    const auto code = studio::languageCode(value);
    if (busy_ || code.isEmpty() || code == model_.document.language)
        return;
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
void StudioController::openAudio() {
    const int row = model_.find(selected_);
    if (row >= 0 && !model_.document.questions[row].speech.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(model_.document.questions[row].speech));
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
