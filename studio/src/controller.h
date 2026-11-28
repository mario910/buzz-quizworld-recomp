#pragma once
#include "document.h"
#include "pack_builder.h"
#include <QAbstractListModel>
#include <QFuture>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QUndoStack>
#include <QUrl>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace studio {
struct ImportedSection {
    QString game;
    QStringList path;
    int questions = 0;
};
struct SourceMediaStatus {
    int recoverable = 0, visualRecoverable = 0, musicRecoverable = 0;
    int unavailable = 0, musicMissing = 0, pending = 0;
    int total() const { return pending; }
};
}

class QuestionModel final : public QAbstractListModel {
    Q_OBJECT
  public:
    enum Role { Uid = Qt::UserRole + 1, Title, Category, Difficulty, Image, Audio, Valid, Source, ContentType, Music };
    studio::Document document;
    explicit QuestionModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void reset(studio::Document value);
    void replace(int row, const std::optional<studio::Question> &value, bool insert = false);
    int find(const QString &id) const;
};
class QuestionFilter final : public QSortFilterProxyModel {
  public:
    QString text, category, kind;
    using QSortFilterProxyModel::QSortFilterProxyModel;
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    void refresh() { invalidateFilter(); }
};
class StudioController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *questions READ questions CONSTANT)
    Q_PROPERTY(QVariantMap current READ current NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY contentChanged)
    Q_PROPERTY(QStringList recentFiles READ recentFiles NOTIFY contentChanged)
    Q_PROPERTY(QString packTitle READ packTitle NOTIFY contentChanged)
    Q_PROPERTY(QString packLanguage READ packLanguage NOTIFY contentChanged)
    Q_PROPERTY(QVariantList projectLanguages READ projectLanguages NOTIFY contentChanged)
    Q_PROPERTY(QVariantList languageOptions READ languageOptions CONSTANT)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY contentChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QStringList importWarnings READ importWarnings NOTIFY contentChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY historyChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool recoveryAvailable READ recoveryAvailable NOTIFY contentChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY contentChanged)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY filterChanged)
    Q_PROPERTY(int imageCount READ imageCount NOTIFY contentChanged)
    Q_PROPERTY(int audioCount READ audioCount NOTIFY contentChanged)
    Q_PROPERTY(int musicCount READ musicCount NOTIFY contentChanged)
    Q_PROPERTY(QVariantList ttsVoices READ ttsVoices CONSTANT)
    Q_PROPERTY(bool buildActive READ buildActive NOTIFY buildProgressChanged)
    Q_PROPERTY(int buildProgress READ buildProgress NOTIFY buildProgressChanged)
    Q_PROPERTY(QVariantMap lastBuild READ lastBuild NOTIFY buildProgressChanged)
  public:
    explicit StudioController(QObject *parent = nullptr, const QString &newProjectLanguage = QStringLiteral("pl"));
    ~StudioController() override;
    QAbstractItemModel *questions() { return &filter_; }
    QVariantMap current() const;
    QVariantList categories() const;
    QStringList recentFiles() const;
    QString packTitle() const { return model_.document.title; }
    QString packLanguage() const { return model_.document.language; }
    QVariantList projectLanguages() const;
    QVariantList languageOptions() const;
    QVariantList editingLanguageOptions() const;
    void setNewProjectLanguage(const QString &code) { newProjectLanguage_ = code; }
    QString projectPath() const { return projectPath_; }
    QString status() const { return status_; }
    QStringList importWarnings() const { return warnings_; }
    int importedQuestionsWithGameCategory() const { return importedQuestionsWithGameCategory_; }
    bool dirty() const { return !history_.isClean(); }
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }
    bool busy() const { return busy_; }
    bool recoveryAvailable() const;
    int totalCount() const { return model_.rowCount(); }
    int visibleCount() const { return filter_.rowCount(); }
    int imageCount() const;
    int audioCount() const;
    int musicCount() const;
    QVariantList ttsVoices() const;
    bool buildActive() const { return bool(buildCancel_); }
    int buildProgress() const { return buildProgress_; }
    QVariantMap lastBuild() const { return lastBuild_; }
    Q_INVOKABLE QVariantMap buildSettings() const;
    Q_INVOKABLE QString localFile(const QUrl &url) const {
        return url.isLocalFile() ? url.toLocalFile() : QString();
    }
    Q_INVOKABLE void buildPack(const QVariantMap &options, bool install = false);
    Q_INVOKABLE void installPack(const QUrl &source, const QString &runtimeRoot, const QString &titleId);
    Q_INVOKABLE void cancelBuild();
    Q_INVOKABLE void revealFile(const QUrl &url);
    Q_INVOKABLE void newProject(bool example = false, const QString &exampleLanguage = {});
    Q_INVOKABLE void openFile(const QUrl &url, bool append = false, const QString &language = {});
    Q_INVOKABLE void prepareImport(const QUrl &url, bool append = false, bool guided = false);
    Q_INVOKABLE void finishImport(const QString &language);
    void finishImport(const QString &language, bool append);
    Q_INVOKABLE void cancelImport();
    Q_INVOKABLE bool saveFile(const QUrl &url = QUrl(), bool legacy = false);
    // The project and all its media as one .buzzproject archive.
    Q_INVOKABLE bool exportProject(const QUrl &url);
    Q_INVOKABLE void select(const QString &id);
    Q_INVOKABLE void setFilter(const QString &text, const QString &category, const QString &kind);
    Q_INVOKABLE void updateCurrent(const QVariantMap &patch);
    Q_INVOKABLE void addQuestion();
    Q_INVOKABLE void duplicateCurrent();
    Q_INVOKABLE void deleteCurrent();
    Q_INVOKABLE void setPackTitle(const QString &title);
    studio::RoundPlans roundPlans() const { return model_.document.roundPlans(); }
    void setRoundPlans(const studio::RoundPlans &plans);
    // Custom subjects/subcategories: [{id, parent, names: {code: name}}], subjects first.
    Q_INVOKABLE QVariantList packCategories() const;
    // Replaces them (undoable); questions in removed categories lose their mapping.
    Q_INVOKABLE void setPackCategories(const QVariantList &categories);
    // Adds a subject (with one subcategory of the same name); returns the subject id.
    Q_INVOKABLE int addPackSubject(const QVariantMap &names);
    bool hasImportedSections(bool latestOnly = false) const;
    QVector<studio::ImportedSection> importedSections(bool latestOnly) const;
    int assignImportedSection(const QString &game, const QStringList &section, int subject, int category,
                              const QString &subjectName, bool latestOnly);
    Q_INVOKABLE void setPackLanguage(const QString &language);
    Q_INVOKABLE void refreshTranslations();
    Q_INVOKABLE void undo() {
        if (!busy_)
            history_.undo();
    }
    Q_INVOKABLE void redo() {
        if (!busy_)
            history_.redo();
    }
    Q_INVOKABLE void decodeSourceMedia(bool playVideo = false);
    void recoverCurrentMusic(bool narration = false);
    bool hasUnresolvedSourceMedia() const;
    bool hasRecoverableSourceMedia() const;
    QVariantList unresolvedMediaQuestions() const;
    studio::SourceMediaStatus sourceMediaStatus(const QString &language = {}, bool latestOnly = false) const;
    bool packNeedsFfmpeg() const;
    QFuture<studio::BuildReadiness> checkBuildReadiness() const;
    QString currentBuildProblem() const;
    bool hasQuestion(const QString &id) const;
    bool recoverSourceMedia(const QString &language = {}, bool latestOnly = false);
    void cancelSourceMediaRecovery();
    bool sourceMediaRecoveryActive() const { return bool(sourceMediaCancel_); }
    int sourceMediaRecoveryPercent() const { return sourceMediaPercent_; }
    Q_INVOKABLE void generateAudio(const QUrl &destination, const QString &text, const QString &voice,
                                   int rate);
    Q_INVOKABLE void recover();
    // Remembers the user's PS3 .rap license; returns an error message or empty.
    Q_INVOKABLE QString addLicense(const QString &contentId, const QUrl &rap);
  signals:
    void contentChanged();
    void projectReplaced();
    void selectionChanged();
    void filterChanged();
    void statusChanged();
    void sourceMediaProgress(int done, int total);
    void sourceMediaRecoveryFinished(const QStringList &errors);
    void sourceMediaRecoveryCancelled();
    void currentMusicRecovered(const QString &path);
    void currentNarrationRecovered(const QString &path);
    void currentVideoRecovered(const QString &path);
    void mediaFfmpegRequired();
    void historyChanged();
    void busyChanged();
    void errorOccurred(const QString &message);
    void importLanguageRequested(const QVariantList &choices, const QString &preferred);
    void importSourcePrepared(const QVariantList &choices, const QString &preferred, bool wholeProject);
    void importFinished(int added, int updated, int unchanged);
    // A PS3 DLC needs its .rap license; import again with append after adding it.
    void licenseRequested(const QUrl &source, bool append, const QString &contentId, const QString &message);
    void buildProgressChanged();
    void packFinished(const QVariantMap &result);
    void packReviewRequired(const QVariantMap &result);

  private:
    QString newProjectLanguage_;
    QuestionModel model_;
    QuestionFilter filter_;
    QUndoStack history_;
    QString selected_, projectPath_, status_ = QStringLiteral("Gotowe do tworzenia"), recoveryPath_;
    QStringList warnings_;
    QSet<QString> lastImportedQuestions_;
    int importedQuestionsWithGameCategory_ = 0;
    bool busy_ = false;
    QUrl pendingImport_;
    bool pendingAppend_ = false;
    mutable QVariantList cachedVoices_;
    mutable bool voicesLoaded_ = false;
    std::shared_ptr<std::atomic_bool> buildCancel_;
    std::shared_ptr<std::atomic_bool> sourceMediaCancel_;
    int sourceMediaPercent_ = 0;
    int buildProgress_ = 0;
    QVariantMap lastBuild_;
    void
    startPackJob(std::function<studio::BuildResult(const studio::pack::Progress &, std::atomic_bool *)> job);
    void notifyContent();
    void setStatus(const QString &text);
    void remember(const QString &path);
    void edit(int row, std::optional<studio::Question> before, std::optional<studio::Question> after,
              const QString &label);
};
