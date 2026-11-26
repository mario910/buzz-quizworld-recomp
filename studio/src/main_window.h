#pragma once
#include <QMainWindow>
#include <QPointer>
#include <QVariantMap>
#include <functional>

class QButtonGroup;
class QComboBox;
class QDialog;
class QFrame;
class QLabel;
class QLineEdit;
class QListView;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QStackedWidget;
class Localization;
class StudioController;

// Qt Widgets interface of Buzz! Question Studio (the same Qt modules as
// BuzzSetup). All state lives in StudioController; the window is rebuilt when
// the interface language changes.
class MainWindow final : public QMainWindow {
    Q_OBJECT
  public:
    MainWindow(StudioController &studio, Localization &localization, QWidget *parent = nullptr);
    // Pushes unsaved edits of the open question (text fields) to the controller.
    Q_INVOKABLE void commitEdits();
    Q_INVOKABLE void openBuild();
    Q_INVOKABLE void openTts();
    Q_INVOKABLE void openProjectLanguages();
    // The project's own subjects and subcategories, named per content language.
    Q_INVOKABLE void openPackCategories();
    void setEditorTab(int tab);
    // The window with any open dialog drawn over it (for --screenshot).
    QImage capture();

  protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

  private:
    StudioController &studio;
    Localization &i18n;
    QString categoryFilter, kindFilter;
    bool loading = false, allowClose = false;
    int editorTab = 0;
    QPointer<QDialog> visibleDialog;
    // Widgets of the current build (recreated on language changes).
    QWidget *overlay = nullptr;
    QLabel *packTitle = nullptr, *saveState = nullptr, *statusText = nullptr, *footerCount = nullptr,
           *countText = nullptr, *readyChip = nullptr, *sourceText = nullptr, *imageName = nullptr,
           *imageThumb = nullptr, *speechName = nullptr, *videoName = nullptr, *mediaNote = nullptr, *issues = nullptr, *idText = nullptr,
           *overlayText = nullptr, *warningText = nullptr, *emptyTitle = nullptr, *emptyText = nullptr;
    QPushButton *languageButton = nullptr, *clearButton = nullptr, *undoButton = nullptr, *redoButton = nullptr,
                *imageRemove = nullptr, *speechOpen = nullptr, *speechRemove = nullptr, *videoOpen = nullptr, *videoRemove = nullptr, *overlayCancel = nullptr,
                *buildButton = nullptr, *imageButton = nullptr, *decodeMediaButton = nullptr;
    QList<QPushButton *> filterButtons, tabButtons, busyDisabled;
    QListWidget *categories = nullptr;
    QListView *questionList = nullptr;
    QLineEdit *search = nullptr;
    QList<QLineEdit *> answerEdits;
    QList<QRadioButton *> correctButtons;
    QList<QFrame *> answerRows;
    QPlainTextEdit *questionText = nullptr;
    QComboBox *subjectCombo = nullptr, *difficultyCombo = nullptr, *subCombo = nullptr;
    QStackedWidget *pages = nullptr, *editorPages = nullptr;
    QWidget *preview = nullptr, *emptyState = nullptr;
    QFrame *warningBar = nullptr;
    QProgressBar *overlayProgress = nullptr;

    void buildUi();
    QWidget *buildHeader();
    QWidget *buildSidebar();
    QWidget *buildList();
    QWidget *buildEditor();
    QWidget *buildFooter();
    void buildOverlay(QWidget *root);
    void refreshContent();
    void refreshSelection();
    void refreshHistory();
    void refreshBusy();
    void refreshFilterButtons();
    void applyFilter();
    // Runs the action now, or after asking to save unsaved changes.
    void guard(const std::function<void()> &action);
    void save(const std::function<void()> &done = {});
    void saveAs(const std::function<void()> &done = {});
    void chooseOpen(bool append);
    void showError(const QString &message);
    void showWarnings();
    void showImportLanguages(const QVariantList &choices, const QString &preferred);
    void askLicense(const QUrl &source, bool append, const QString &contentId, const QString &message);
    void showPackResult(const QVariantMap &result);
    void openInstall(const QString &source);
    void renamePack();
    QString languageLabel(const QString &code) const;
    void confirmDelete();
    void showAbout();
    void showDialog(QDialog *dialog);
    QVariantMap current() const;
};
