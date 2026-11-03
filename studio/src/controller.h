#pragma once
#include "document.h"
#include "pack_builder.h"
#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QUndoStack>
#include <QUrl>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class QuestionModel final : public QAbstractListModel {
    Q_OBJECT
  public:
    enum Role { Uid = Qt::UserRole + 1, Title, Category, Difficulty, Image, Audio, Valid, Source };
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
    Q_PROPERTY(QVariantList ttsVoices READ ttsVoices CONSTANT)
    Q_PROPERTY(bool buildActive READ buildActive NOTIFY buildProgressChanged)
    Q_PROPERTY(int buildProgress READ buildProgress NOTIFY buildProgressChanged)
    Q_PROPERTY(QVariantMap lastBuild READ lastBuild NOTIFY buildProgressChanged)
  public:
    explicit StudioController(QObject *parent = nullptr);
    ~StudioController() override;
    QAbstractItemModel *questions() { return &filter_; }
    QVariantMap current() const;
    QVariantList categories() const;
    QStringList recentFiles() const;
    QString packTitle() const { return model_.document.title; }
    QString packLanguage() const { return model_.document.language; }
    QVariantList projectLanguages() const;
    QVariantList languageOptions() const;
    QString projectPath() const { return projectPath_; }
    QString status() const { return status_; }
    QStringList importWarnings() const { return warnings_; }
    bool dirty() const { return !history_.isClean(); }
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }
    bool busy() const { return busy_; }
    bool recoveryAvailable() const;
    int totalCount() const { return model_.rowCount(); }
    int visibleCount() const { return filter_.rowCount(); }
    int imageCount() const;
    int audioCount() const;
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
    Q_INVOKABLE void newProject(bool example = false);
    Q_INVOKABLE void openFile(const QUrl &url, bool append = false, const QString &language = {});
    Q_INVOKABLE void prepareImport(const QUrl &url, bool append = false);
    Q_INVOKABLE void finishImport(const QString &language);
    Q_INVOKABLE void cancelImport();
    Q_INVOKABLE bool saveFile(const QUrl &url = QUrl(), bool legacy = false);
    Q_INVOKABLE void select(const QString &id);
    Q_INVOKABLE void setFilter(const QString &text, const QString &category, const QString &kind);
    Q_INVOKABLE void updateCurrent(const QVariantMap &patch);
    Q_INVOKABLE void addQuestion();
    Q_INVOKABLE void duplicateCurrent();
    Q_INVOKABLE void deleteCurrent();
    Q_INVOKABLE void setPackTitle(const QString &title);
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
    Q_INVOKABLE void openAudio();
    Q_INVOKABLE void generateAudio(const QUrl &destination, const QString &text, const QString &voice,
                                   int rate);
    Q_INVOKABLE void recover();
  signals:
    void contentChanged();
    void selectionChanged();
    void filterChanged();
    void statusChanged();
    void historyChanged();
    void busyChanged();
    void errorOccurred(const QString &message);
    void importLanguageRequested(const QVariantList &choices, const QString &preferred);
    void buildProgressChanged();
    void packFinished(const QVariantMap &result);

  private:
    QuestionModel model_;
    QuestionFilter filter_;
    QUndoStack history_;
    QString selected_, projectPath_, status_ = QStringLiteral("Gotowe do tworzenia"), recoveryPath_;
    QStringList warnings_;
    bool busy_ = false;
    QUrl pendingImport_;
    bool pendingAppend_ = false;
    mutable QVariantList cachedVoices_;
    mutable bool voicesLoaded_ = false;
    std::shared_ptr<std::atomic_bool> buildCancel_;
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
