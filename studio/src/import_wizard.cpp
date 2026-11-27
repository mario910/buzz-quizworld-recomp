#include "import_wizard.h"
#include "controller.h"
#include "languages.h"
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>
#include <utility>

namespace {
QString mainText(const char *text) { return QCoreApplication::translate("Main", text); }

QLabel *textLabel(const QString &text = {}, const char *style = nullptr) {
    auto *label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    if (style)
        label->setObjectName(style);
    return label;
}

QString importFile(const QString &path) {
    const QFileInfo info(path);
    if (info.isFile())
        return info.absoluteFilePath();
    if (info.isDir()) {
        const QDir folder(info.absoluteFilePath());
        for (const auto *name : {"PS3_GAME/PARAM.SFO", "PARAM.SFO", "Media/resources.assets", "resources.assets"})
            if (const QFileInfo candidate(folder.filePath(name)); candidate.isFile())
                return candidate.absoluteFilePath();
    }
    return {};
}
} // namespace

QString ImportWizard::sourceFilter() {
    return tr("Pytania i gry") +
           " (*.iso *.ISO *.cso *.CSO *.pak *.PAK *.edat *.EDAT *.sdat *.SDAT *.pkg *.PKG "
           "*.sfo *.SFO *.qdb *.QDB *.assets *.json *.buzzproject)";
}

ImportWizard::ImportWizard(StudioController &studio, bool preferAppend,
                           std::function<void(const std::function<void()> &)> guard, QWidget *parent)
    : QDialog(parent), studio_(studio), guard_(std::move(guard)) {
    setObjectName("importWizard");
    setWindowTitle(tr("Importuj pytania"));
    setAcceptDrops(true);
    resize(625, 435);
    setMinimumWidth(530);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(24, 20, 24, 20);
    column->setSpacing(12);
    step_ = textLabel({}, "accent");
    heading_ = textLabel({}, "heading");
    column->addWidget(step_);
    column->addWidget(heading_);
    pages_ = new QStackedWidget;
    column->addWidget(pages_, 1);

    sourcePage_ = new QWidget;
    auto *sourceColumn = new QVBoxLayout(sourcePage_);
    sourceColumn->setContentsMargins(0, 0, 0, 0);
    sourceColumn->addWidget(textLabel(tr("Wybierz obraz płyty, plik pytań lub rozpakowany folder gry PS3 albo KiP."), "muted"));
    sourcePath_ = new QLineEdit;
    sourcePath_->setObjectName("importSourcePath");
    sourcePath_->setPlaceholderText(tr("Wklej ścieżkę lub przeciągnij plik albo folder"));
    sourcePath_->setAccessibleName(tr("Źródło pytań"));
    sourcePath_->setClearButtonEnabled(true);
    sourcePath_->setAcceptDrops(false); // The dialog handles file drops over the entire source page.
    sourceColumn->addWidget(sourcePath_);
    auto *sources = new QHBoxLayout;
    auto *file = new QPushButton(tr("Wybierz plik…"));
    auto *folder = new QPushButton(tr("Wybierz folder…"));
    sources->addWidget(file);
    sources->addWidget(folder);
    sources->addStretch();
    sourceColumn->addLayout(sources);
    sourceColumn->addWidget(textLabel(tr("Z rozpakowanej gry PS2 lub PSP wybierz plik ROUND.PAK lub MERGED.QDB."), "muted"));
    sourceColumn->addStretch();
    pages_->addWidget(sourcePage_);
    connect(file, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Wybierz plik…"), sourcePath_->text(), sourceFilter());
        if (!path.isEmpty())
            setSourcePath(path);
    });
    connect(folder, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, tr("Wybierz folder…"), sourcePath_->text());
        if (!path.isEmpty())
            setSourcePath(path);
    });

    reviewPage_ = new QWidget;
    auto *reviewColumn = new QVBoxLayout(reviewPage_);
    reviewColumn->setContentsMargins(0, 0, 0, 0);
    sourceName_ = textLabel({}, "accent");
    sourceName_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    reviewColumn->addWidget(sourceName_);
    auto *form = new QFormLayout;
    language_ = new QComboBox;
    language_->setObjectName("importLanguageCombo");
    languageLabel_ = textLabel(mainText("Język pytań"));
    languageLabel_->setBuddy(language_);
    form->addRow(languageLabel_, language_);
    destination_ = new QComboBox;
    destination_->setObjectName("importDestination");
    if (studio_.totalCount() || studio_.dirty() || !studio_.projectPath().isEmpty())
        destination_->addItem(tr("Dodaj do bieżącego projektu"), true);
    destination_->addItem(tr("Otwórz jako nowy projekt"), false);
    destination_->setCurrentIndex(preferAppend ? 0 : destination_->count() - 1);
    form->addRow(tr("Miejsce importu"), destination_);
    reviewColumn->addLayout(form);
    reviewNote_ = textLabel({}, "muted");
    reviewColumn->addWidget(reviewNote_);
    reviewColumn->addStretch();
    pages_->addWidget(reviewPage_);
    connect(language_, &QComboBox::activated, this, [this] { languageChosen_ = true; });
    connect(language_, &QComboBox::currentIndexChanged, this, &ImportWizard::updateReviewNote);
    connect(destination_, &QComboBox::currentIndexChanged, this, [this] {
        if (!languageChosen_) {
            const int current = destination_->currentData().toBool() ? language_->findData(studio_.packLanguage()) : -1;
            language_->setCurrentIndex(current >= 0 ? current : qMax(0, language_->findData(preferred_)));
        }
        updateReviewNote();
    });

    licensePage_ = new QWidget;
    auto *licenseColumn = new QVBoxLayout(licensePage_);
    licenseColumn->setContentsMargins(0, 0, 0, 0);
    licenseMessage_ = textLabel();
    licenseMessage_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    licenseColumn->addWidget(licenseMessage_);
    licenseColumn->addWidget(textLabel(mainText("Plik .rap znajdziesz w folderze exdata konsoli lub emulatora (np. "
        "dev_hdd0/home/00000001/exdata). Studio zapamięta jego położenie."), "muted"));
    licenseColumn->addStretch();
    pages_->addWidget(licensePage_);

    summaryPage_ = new QWidget;
    auto *summaryColumn = new QVBoxLayout(summaryPage_);
    summaryColumn->setContentsMargins(0, 0, 0, 0);
    summary_ = textLabel();
    summaryNote_ = textLabel({}, "muted");
    mediaSummary_ = textLabel({}, "accent");
    mediaResult_ = textLabel({}, "accent");
    mediaResult_->hide();
    warningsLabel_ = textLabel(mainText("Uwagi importu"), "muted");
    warnings_ = new QPlainTextEdit;
    warnings_->setReadOnly(true);
    warnings_->setAccessibleName(mainText("Uwagi importu"));
    summaryColumn->addWidget(summary_);
    summaryColumn->addWidget(summaryNote_);
    assignCategories_ = new QPushButton(mainText("Przypisz kategorie po imporcie…"));
    assignCategories_->setObjectName("assignImportedCategories");
    assignCategories_->setAutoDefault(false);
    summaryColumn->addWidget(assignCategories_, 0, Qt::AlignLeft);
    connect(assignCategories_, &QPushButton::clicked, this, &ImportWizard::assignCategoriesRequested);
    summaryColumn->addWidget(mediaSummary_);
    summaryColumn->addWidget(mediaResult_);
    summaryColumn->addWidget(warningsLabel_);
    summaryColumn->addWidget(warnings_, 1);
    summaryColumn->addStretch();
    pages_->addWidget(summaryPage_);

    error_ = textLabel({}, "issues");
    error_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    error_->hide();
    column->addWidget(error_);
    progress_ = new QProgressBar;
    progress_->setRange(0, 0);
    column->addWidget(progress_);
    auto *buttons = new QHBoxLayout;
    cancel_ = new QPushButton(mainText("Anuluj"));
    back_ = new QPushButton(tr("Wstecz"));
    back_->setObjectName("importBack");
    next_ = new QPushButton;
    next_->setObjectName("importNext");
    next_->setProperty("primary", true);
    next_->setDefault(true);
    recover_ = new QPushButton(mainText("Odzyskaj media z gry"));
    recover_->setObjectName("importRecoverMedia");
    build_ = new QPushButton(mainText("Zbuduj paczkę"));
    build_->setObjectName("importBuildPack");
    buttons->addWidget(cancel_);
    buttons->addStretch();
    buttons->addWidget(back_);
    buttons->addWidget(recover_);
    buttons->addWidget(build_);
    buttons->addWidget(next_);
    column->addLayout(buttons);
    for (auto *button : {file, folder, cancel_, back_, recover_, build_})
        button->setAutoDefault(false);
    connect(cancel_, &QPushButton::clicked, this, &ImportWizard::reject);
    connect(back_, &QPushButton::clicked, this, &ImportWizard::back);
    connect(next_, &QPushButton::clicked, this, &ImportWizard::next);
    connect(sourcePath_, &QLineEdit::textChanged, this, [this](const QString &path) {
        error_->hide();
        next_->setEnabled(!path.trimmed().isEmpty());
    });
    connect(recover_, &QPushButton::clicked, this, [this] {
        beforeRecovery_ = studio_.sourceMediaStatus(mediaLanguage(), true).total();
        setStage(Stage::Recovering);
        emit recoverMediaRequested(mediaLanguage());
    });
    connect(build_, &QPushButton::clicked, this, [this] { accept(); emit buildPackRequested(); });
    connect(&studio_, &StudioController::importSourcePrepared, this, &ImportWizard::reviewSource);
    connect(&studio_, &StudioController::importFinished, this, &ImportWizard::showSummary);
    connect(&studio_, &StudioController::sourceMediaRecoveryFinished, this, &ImportWizard::finishRecovery);
    connect(&studio_, &StudioController::sourceMediaRecoveryCancelled, this, [this] { finishRecovery({}); });
    connect(&studio_, &StudioController::sourceMediaProgress, this, [this](int done, int total) {
        if (stage_ == Stage::Recovering) {
            progress_->setRange(0, total);
            progress_->setValue(done);
        }
    });
    setStage(Stage::Source);
    sourcePath_->setFocus();
}

void ImportWizard::setSourcePath(const QString &path) { sourcePath_->setText(QDir::toNativeSeparators(path)); }

bool ImportWizard::working() const { return stage_ == Stage::Checking || stage_ == Stage::Importing || stage_ == Stage::Recovering; }

void ImportWizard::setStage(Stage stage) {
    stage_ = stage;
    error_->hide();
    const bool source = stage == Stage::Source || stage == Stage::Checking;
    const bool done = stage == Stage::Summary;
    const bool recovering = stage == Stage::Recovering;
    const bool offerRecovery = done && !recoveryAttempted_ && studio_.sourceMediaStatus(mediaLanguage(), true).recoverable > 0;
    const bool checkingLicense = stage == Stage::License && beforeLicense_ == Stage::Checking;
    step_->setText(tr("Krok %1 z 3").arg(source || checkingLicense ? 1 : done || recovering ? 3 : 2));
    switch (stage) {
    case Stage::Source: heading_->setText(tr("Źródło pytań")); break;
    case Stage::Checking: heading_->setText(tr("Sprawdzanie źródła…")); break;
    case Stage::Review: heading_->setText(tr("Ustawienia importu")); break;
    case Stage::Importing: heading_->setText(tr("Importowanie pytań…")); break;
    case Stage::License: heading_->setText(mainText("Licencja paczki PS3")); break;
    case Stage::Summary: heading_->setText(tr("Import zakończony")); break;
    case Stage::Recovering:
        heading_->setText(QCoreApplication::translate("StudioController", "Odzyskiwanie mediów z gry…"));
        break;
    }
    pages_->setCurrentWidget(source ? sourcePage_ : done || recovering ? summaryPage_ : stage == Stage::License ? licensePage_ : reviewPage_);
    pages_->setEnabled(!working());
    progress_->setRange(0, 0);
    progress_->setVisible(working());
    cancel_->setVisible(!done);
    cancel_->setEnabled(!working() || recovering);
    back_->setVisible(stage == Stage::Review || stage == Stage::License);
    next_->setText(done ? (offerRecovery ? tr("Pomiń i przejdź do edytora") : tr("Przejdź do edytora"))
                        : stage == Stage::License ? mainText("Wskaż plik .rap…")
                        : source ? tr("Dalej") : mainText("Importuj"));
    next_->setEnabled(!working() && (!source || !sourcePath_->text().trimmed().isEmpty()));
    recover_->setVisible(offerRecovery);
    next_->setProperty("primary", !offerRecovery);
    recover_->setProperty("primary", true);
    for (auto *button : {next_, recover_}) {
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
    next_->setDefault(!offerRecovery);
    recover_->setDefault(offerRecovery);
    assignCategories_->setVisible(done && studio_.hasImportedSections(true));
    build_->setVisible(done && !offerRecovery && languageNote().isEmpty() && studio_.totalCount() > 0);
    if (offerRecovery)
        recover_->setFocus();
}

void ImportWizard::checkSource() {
    auto path = sourcePath_->text().trimmed();
    if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
        path = path.mid(1, path.size() - 2);
    if (const QUrl url(path); url.isLocalFile())
        path = url.toLocalFile();
    const auto filename = importFile(QDir::fromNativeSeparators(path));
    if (filename.isEmpty()) {
        error_->setText(tr("Wybierz istniejący plik lub folder z obsługiwaną grą."));
        error_->show();
        return;
    }
    source_ = QUrl::fromLocalFile(filename);
    sourceName_->setText(QFileInfo(QDir::cleanPath(path)).fileName());
    sourceName_->setToolTip(QDir::toNativeSeparators(filename));
    setStage(Stage::Checking);
    studio_.prepareImport(source_, false, true);
}

void ImportWizard::reviewSource(const QVariantList &choices, const QString &preferred, bool wholeProject) {
    if (stage_ != Stage::Checking)
        return;
    wholeProject_ = wholeProject;
    pending_ = true;
    languageChosen_ = false;
    preferred_ = preferred;
    language_->clear();
    for (const auto &value : choices) {
        const auto choice = value.toMap();
        language_->addItem(choice["name"].toString(), choice["code"]);
    }
    const int current = destination_->currentData().toBool() ? language_->findData(studio_.packLanguage()) : -1;
    language_->setCurrentIndex(current >= 0 ? current : qMax(0, language_->findData(preferred_)));
    languageLabel_->setVisible(!wholeProject_);
    language_->setVisible(!wholeProject_);
    updateReviewNote();
    setStage(Stage::Review);
    (wholeProject_ ? static_cast<QWidget *>(destination_) : language_)->setFocus();
}

QString ImportWizard::languageNote() const {
    const auto language = language_->currentData().toString();
    if (wholeProject_ || !destination_->currentData().toBool() || language.isEmpty() || language == studio_.packLanguage())
        return {};
    return tr("Aby zobaczyć tę wersję pytań, ustaw język pytań w projekcie na %1.").arg(studio::languageName(language));
}

void ImportWizard::updateReviewNote() {
    auto note = wholeProject_ ? tr("Projekt zachowa wszystkie wersje językowe.")
                              : mainText("Pytania bez wybranego języka zostaną pominięte.");
    if (const auto language = languageNote(); !language.isEmpty())
        note += "\n\n" + language;
    reviewNote_->setText(note);
}

void ImportWizard::startImport() {
    append_ = destination_->currentData().toBool();
    importLanguage_ = wholeProject_ ? QString() : language_->currentData().toString();
    const auto load = [this] {
        setStage(Stage::Importing);
        if (std::exchange(pending_, false))
            studio_.finishImport(importLanguage_, append_);
        else
            studio_.openFile(source_, append_, importLanguage_);
    };
    if (append_)
        load();
    else
        guard_(load);
}

bool ImportWizard::showImportError(const QString &message) {
    if (!working())
        return false;
    setStage(stage_ == Stage::Checking ? Stage::Source : Stage::Review);
    error_->setText(message);
    error_->show();
    return true;
}

bool ImportWizard::requestLicense(const QString &contentId, const QString &message) {
    if (!working())
        return false;
    beforeLicense_ = stage_;
    license_ = contentId;
    licenseMessage_->setText(message);
    setStage(Stage::License);
    return true;
}

void ImportWizard::chooseLicense() {
    const auto path = QFileDialog::getOpenFileName(this, mainText("Wskaż plik licencji .rap"), {},
                                                  mainText("Licencja PS3 (*.rap *.RAP)"));
    if (path.isEmpty())
        return;
    const auto error = studio_.addLicense(license_, QUrl::fromLocalFile(path));
    if (!error.isEmpty()) {
        error_->setText(error);
        error_->show();
        return;
    }
    setStage(beforeLicense_);
    if (beforeLicense_ == Stage::Checking)
        studio_.prepareImport(source_, false, true);
    else
        studio_.openFile(source_, append_, importLanguage_);
}

void ImportWizard::showSummary(int added, int updated, int unchanged) {
    if (stage_ != Stage::Importing)
        return;
    QStringList counts{tr("Dodane pytania: %1").arg(added)};
    if (updated)
        counts << tr("Zaktualizowane pytania: %1").arg(updated);
    if (unchanged)
        counts << tr("Istniejące pytania zachowane bez zmian: %1").arg(unchanged);
    const int preserved = studio_.importedQuestionsWithGameCategory();
    if (preserved)
        counts << tr("Pytania z zachowaną kategorią gry: %1").arg(preserved);
    if (preserved && preserved == added)
        assignCategories_->setText(tr("Zmień przypisanie kategorii…"));
    summary_->setText(counts.join('\n'));
    const auto note = languageNote();
    summaryNote_->setText(note);
    summaryNote_->setVisible(!note.isEmpty());
    updateMediaSummary();
    const auto warnings = studio_.importWarnings();
    warnings_->setPlainText(warnings.join("\n\n"));
    warnings_->setVisible(!warnings.isEmpty());
    warningsLabel_->setVisible(!warnings.isEmpty());
    setStage(Stage::Summary);
    if (!recover_->isVisible())
        next_->setFocus();
}

QString ImportWizard::mediaLanguage() const {
    return importLanguage_.isEmpty() ? studio_.packLanguage() : importLanguage_;
}

void ImportWizard::updateMediaSummary() {
    const auto media = studio_.sourceMediaStatus(mediaLanguage(), true);
    QStringList text;
    if (media.visualRecoverable) {
        text << (recoveryAttempted_
            ? tr("Obrazy lub filmy nadal brakujące po odzyskiwaniu: %1")
            : tr("Pytania z obrazem lub filmem do automatycznego odzyskania: %1")).arg(media.visualRecoverable);
    }
    if (media.musicMissing)
        text << tr("Pytania z brakującym klipem muzycznym: %1").arg(media.musicMissing);
    if (media.unavailable)
        text << tr("Pytania z brakującym lub nieobsługiwanym obrazem albo filmem: %1").arg(media.unavailable);
    mediaSummary_->setText(text.join('\n'));
    mediaSummary_->setVisible(!text.isEmpty());
}

void ImportWizard::finishRecovery(const QStringList &errors) {
    if (stage_ != Stage::Recovering)
        return;
    recoveryAttempted_ = true;
    const int restored = beforeRecovery_ - studio_.sourceMediaStatus(mediaLanguage(), true).total();
    mediaResult_->setText(tr("Pytania uzupełnione automatycznie: %1").arg(restored));
    mediaResult_->show();
    if (!errors.isEmpty()) {
        warnings_->appendPlainText(errors.join("\n\n"));
        warnings_->show();
        warningsLabel_->show();
    }
    updateMediaSummary();
    setStage(Stage::Summary);
    next_->setFocus();
}

void ImportWizard::recoveryNotStarted() {
    if (stage_ == Stage::Recovering)
        setStage(Stage::Summary);
}

void ImportWizard::back() {
    if (pending_)
        studio_.cancelImport();
    pending_ = false;
    setStage(Stage::Source);
    sourcePath_->setFocus();
}

void ImportWizard::next() {
    switch (stage_) {
    case Stage::Source: checkSource(); break;
    case Stage::Review: startImport(); break;
    case Stage::License: chooseLicense(); break;
    case Stage::Summary: accept(); break;
    default: break;
    }
}

void ImportWizard::reject() {
    if (stage_ == Stage::Recovering) {
        studio_.cancelSourceMediaRecovery();
        return;
    }
    if (working())
        return;
    if (pending_)
        studio_.cancelImport();
    QDialog::reject();
}

void ImportWizard::dragEnterEvent(QDragEnterEvent *event) {
    const auto urls = event->mimeData()->urls();
    if (stage_ == Stage::Source && urls.size() == 1 && urls.first().isLocalFile())
        event->acceptProposedAction();
}

void ImportWizard::dropEvent(QDropEvent *event) {
    const auto urls = event->mimeData()->urls();
    if (stage_ == Stage::Source && urls.size() == 1 && urls.first().isLocalFile()) {
        setSourcePath(urls.first().toLocalFile());
        event->acceptProposedAction();
    }
}
