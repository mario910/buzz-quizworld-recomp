#pragma once

#include <QDialog>
#include <QUrl>
#include <QVariantList>
#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class StudioController;

class ImportWizard final : public QDialog {
    Q_OBJECT
  public:
    ImportWizard(StudioController &studio, bool preferAppend,
                 std::function<void(const std::function<void()> &)> guard, QWidget *parent);
    static QString sourceFilter();
    void setSourcePath(const QString &path);
    bool showImportError(const QString &message);
    bool requestLicense(const QString &contentId, const QString &message);
    void recoveryNotStarted();
    void reject() override;

  signals:
    void assignCategoriesRequested();
    void recoverMediaRequested(const QString &language);
    void buildPackRequested();

  protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

  private:
    enum class Stage { Source, Checking, Review, Importing, License, Summary, Recovering };
    void setStage(Stage stage);
    void checkSource();
    void reviewSource(const QVariantList &choices, const QString &preferred, bool wholeProject);
    void updateReviewNote();
    QString languageNote() const;
    void startImport();
    void chooseLicense();
    void showSummary(int added, int updated, int unchanged);
    QString mediaLanguage() const;
    void updateMediaSummary();
    void finishRecovery(const QStringList &errors);
    void back();
    void next();
    bool working() const;

    StudioController &studio_;
    std::function<void(const std::function<void()> &)> guard_;
    Stage stage_ = Stage::Source, beforeLicense_ = Stage::Checking;
    QUrl source_;
    QString preferred_, importLanguage_, license_;
    bool wholeProject_ = false, pending_ = false, languageChosen_ = false, append_ = false;
    bool recoveryAttempted_ = false;
    int beforeRecovery_ = 0;
    QStackedWidget *pages_;
    QWidget *sourcePage_, *reviewPage_, *licensePage_, *summaryPage_;
    QLabel *step_, *heading_, *error_, *sourceName_, *languageLabel_, *reviewNote_, *licenseMessage_, *summary_,
           *summaryNote_, *mediaSummary_, *mediaResult_, *warningsLabel_;
    QLineEdit *sourcePath_;
    QComboBox *destination_, *language_;
    QPlainTextEdit *warnings_;
    QProgressBar *progress_;
    QPushButton *back_, *next_, *cancel_, *recover_, *build_, *assignCategories_;
};
