#include "main_window.h"
#include "controller.h"
#include "import_wizard.h"
#include "languages.h"
#include "localization.h"
#include "media_preview.h"
#include "media_tools.h"
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTextEdit>
#include <QTextLayout>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <iterator>
#include <utility>

namespace {
// Translation contexts "Main" and "PackDialogs" group the interface strings in studio_en.ts.
QString T(const char *text) { return QCoreApplication::translate("Main", text); }
QString P(const char *text) { return QCoreApplication::translate("PackDialogs", text); }

QString roundTypeLabel(const QString &type) {
    static const QMap<QString, const char *> labels{
        {"TriggerFinger", "Szybki palec"}, {"StopTheClockChallenge", "Zatrzymaj zegar"},
        {"OverTheEdgeChallenge", "Na krawędzi"}, {"PointBuilder", "Zdobywanie punktów"},
        {"PassTheBomb", "Podaj bombę"}, {"FastestFingerFirst", "Kto pierwszy"},
        {"PieFight", "Bitwa na torty"}, {"PointStealerMix", "Kradzież punktów"},
        {"BetIt", "Obstawianie"}, {"FinalCountdown", "Odliczanie finałowe"}};
    const auto label = labels.value(type);
    return label ? T(label) : type;
}

void copyFfmpegInstallCommand(QWidget *parent, const QString &command) {
    QApplication::clipboard()->setText(command);
    QMessageBox::information(parent, P("Silnik mediów (FFmpeg)"),
                             T("Uruchom skopiowane polecenie w terminalu, a potem wróć do Studio.\n\n%1")
                                 .arg(command));
}

const QColor answerColors[4] = {QColor("#689EFF"), QColor("#F4AD63"), QColor("#96D471"), QColor("#E9D878")};

struct Subject {
    const char *name;
    int id, sub;
};
// Quiz World subjects and their default subcategory.
const Subject subjects[] = {{"Nauka i przyroda", 235, 237}, {"Sztuka i literatura", 177, 179},
                            {"Historia i wydarzenia", 182, 184}, {"Czas wolny", 188, 191},
                            {"Film", 199, 208}, {"Muzyka", 175, 224},
                            {"Ludzie", 230, 232}, {"Sport", 174, 247},
                            {"Świat", 250, 253}, {"Telewizja", 257, 271}};
QVector<QPair<int, const char *>> subcategories(int subject) {
    switch (subject) {
    case 235: return {{237, "Nauka"}, {236, "Internet i technologia"}, {238, "Człowiek"},
                      {239, "Fizyka i chemia"}, {240, "Przyroda"}};
    case 177: return {{179, "Książki i pisarze"}, {178, "Sztuka i artyści"}, {181, "Teatr"}};
    case 182: return {{184, "Historia"}, {183, "Aktualności"}, {187, "Trendy"}};
    case 188: return {{191, "Zainteresowania"}, {189, "Dom"}, {196, "Gry i zabawy"}};
    case 199: return {{208, "Kino"}};
    case 175: return {{224, "Muzyka popularna"}};
    case 230: return {{232, "Społeczeństwo"}};
    case 174: return {{247, "Dyscypliny sportowe"}};
    case 250: return {{253, "Geografia"}, {251, "Wierzenia"}};
    case 257: return {{271, "Programy TV"}};
    default: return {};
    }
}

void fillCategorySubjects(QComboBox *combo, const QVariantList &ownCategories) {
    combo->clear();
    for (const auto &subject : subjects)
        combo->addItem(T(subject.name), subject.id);
    bool separated = false;
    for (const auto &value : ownCategories) {
        const auto category = value.toMap();
        if (category["parent"].toInt())
            continue;
        if (!separated)
            combo->insertSeparator(combo->count());
        separated = true;
        combo->addItem(category["name"].toString(), category["id"]);
    }
}

void fillCategoryChildren(QComboBox *combo, int subject, const QVariantList &ownCategories) {
    combo->clear();
    if (!subject)
        return;
    for (const auto &[id, name] : subcategories(subject))
        combo->addItem(T(name), id);
    for (const auto &value : ownCategories) {
        const auto category = value.toMap();
        if (category["parent"].toInt() == subject)
            combo->addItem(category["name"].toString(), category["id"]);
    }
}

QLabel *label(const QString &text, const char *role = nullptr, QWidget *parent = nullptr) {
    auto *l = new QLabel(text, parent);
    if (role)
        l->setObjectName(role);
    l->setTextFormat(Qt::PlainText);
    return l;
}
QPushButton *button(const QString &text, const char *role = nullptr) {
    auto *b = new QPushButton(text);
    if (role)
        b->setObjectName(role);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}
QFrame *divider(bool vertical = false) {
    auto *f = new QFrame;
    f->setObjectName("divider");
    if (vertical)
        f->setFixedWidth(1);
    else
        f->setFixedHeight(1);
    return f;
}
void restyle(QWidget *w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
}
QString fileName(const QString &path) {
    const auto name = QFileInfo(path).fileName();
    return name.size() > 26 ? name.left(15) + QChar(0x2026) + name.right(10) : name;
}

// Category names fit the game's chooser: at most 40 characters.
class LimitedTextDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override {
        auto *editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (auto *line = qobject_cast<QLineEdit *>(editor))
            line->setMaxLength(40);
        return editor;
    }
};

// Question cards in the library list.
class QuestionDelegate final : public QStyledItemDelegate {
  public:
    const QString *currentId = nullptr;
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return {300, 124}; }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRect r = option.rect.adjusted(14, 4, -14, -4);
        const bool chosen = currentId && index.data(QuestionModel::Uid).toString() == *currentId;
        const bool hovered = option.state & QStyle::State_MouseOver;
        QPainterPath card;
        card.addRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
        p->fillPath(card, chosen ? QColor("#302451") : hovered ? QColor("#29284A") : QColor("#1F1F36"));
        p->setPen(chosen ? QColor("#7A62B0") : QColor("#36365A"));
        p->drawPath(card);
        if (chosen) {
            p->setPen(Qt::NoPen);
            p->setBrush(QColor("#C4A5FF"));
            p->drawRoundedRect(QRectF(r.left() + 1, r.center().y() - 19, 3, 38), 1.5, 1.5);
        }
        const auto category = index.data(QuestionModel::Category).toString();
        const bool valid = index.data(QuestionModel::Valid).toBool();
        const auto content = studio::QuestionContentType(index.data(QuestionModel::ContentType).toInt());
        auto kind = content == studio::QuestionContentType::Video ? T("WIDEO")
                    : content == studio::QuestionContentType::Picture ? T("OBRAZ") : T("TEKST");
        if (index.data(QuestionModel::Music).toBool())
            kind += " · " + T("Muzyka").toUpper();
        if (!valid)
            kind += " · " + T("SZKIC");
        QFont small = option.font;
        small.setPixelSize(10);
        p->setFont(small);
        p->setPen(chosen ? QColor("#C4A5FF") : QColor("#C4BFD9"));
        const QRect top(r.left() + 16, r.top() + 10, r.width() - 32, 16);
        QFont tiny = small;
        tiny.setPixelSize(8);
        tiny.setLetterSpacing(QFont::AbsoluteSpacing, 0.7);
        const int categoryWidth = std::max(0, top.width() - QFontMetrics(tiny).horizontalAdvance(kind) - 10);
        p->drawText(QRect(top.left(), top.top(), categoryWidth, top.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    p->fontMetrics().elidedText(category, Qt::ElideRight, categoryWidth));
        p->setFont(tiny);
        p->setPen(valid ? QColor("#C4BFD9") : QColor("#FFCF63"));
        p->drawText(top, Qt::AlignRight | Qt::AlignVCenter, kind);
        QFont title = option.font;
        title.setPixelSize(13);
        title.setWeight(QFont::Medium);
        p->setFont(title);
        p->setPen(QColor("#F5F3FF"));
        auto text = index.data(QuestionModel::Title).toString();
        if (text.isEmpty())
            text = T("Nowe pytanie");
        const QRect body(r.left() + 16, r.top() + 32, r.width() - 32, 42);
        QString shown;
        {
            // Two lines at most, elided.
            QTextLayout layout(text, title);
            layout.beginLayout();
            int lines = 0;
            while (lines < 2) {
                auto line = layout.createLine();
                if (!line.isValid())
                    break;
                line.setLineWidth(body.width());
                const auto part = text.mid(line.textStart(), line.textLength());
                shown += lines == 1 && line.textStart() + line.textLength() < text.size()
                             ? p->fontMetrics().elidedText(text.mid(line.textStart()), Qt::ElideRight, body.width())
                             : part;
                ++lines;
            }
            layout.endLayout();
        }
        p->drawText(body, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, shown);
        const int level = index.data(QuestionModel::Difficulty).toInt();
        p->setPen(Qt::NoPen);
        for (int i = 0; i < 3; ++i) {
            p->setBrush(i < level ? (chosen ? QColor("#C4A5FF") : QColor("#8F89A8")) : QColor("#3A3958"));
            p->drawRoundedRect(QRectF(r.left() + 16 + i * 15, r.bottom() - 18, 12, 3), 1.5, 1.5);
        }
        QFont foot = option.font;
        foot.setPixelSize(9);
        p->setFont(foot);
        p->setPen(QColor("#C4BFD9"));
        const char *levels[] = {"", "Łatwe", "Średnie", "Trudne"};
        const auto difficulty = level >= 1 && level <= 3 ? T(levels[level]) : QString();
        const int difficultyWidth = p->fontMetrics().horizontalAdvance(difficulty);
        p->drawText(QRect(r.left() + 64, r.bottom() - 24, difficultyWidth, 14), Qt::AlignLeft | Qt::AlignVCenter,
                    difficulty);
        const int left = r.left() + 64 + difficultyWidth + 10;
        const int right = r.right() - 16;
        const auto tagsText = index.data(QuestionModel::Audio).toBool()
            ? p->fontMetrics().elidedText(T("Lektor"), Qt::ElideRight, std::max(0, right - left)) : QString();
        const int tagsWidth = p->fontMetrics().horizontalAdvance(tagsText);
        const int sourceWidth = std::max(0, right - left - (tagsWidth ? tagsWidth + 8 : 0));
        p->setPen(QColor("#8F89A8"));
        p->drawText(QRect(left, r.bottom() - 24, sourceWidth, 14), Qt::AlignRight | Qt::AlignVCenter,
                    p->fontMetrics().elidedText(index.data(QuestionModel::Source).toString(), Qt::ElideRight, sourceWidth));
        if (tagsWidth) {
            p->setPen(QColor("#C4A5FF"));
            p->drawText(QRect(right - tagsWidth, r.bottom() - 24, tagsWidth, 14),
                        Qt::AlignRight | Qt::AlignVCenter, tagsText);
        }
        p->restore();
    }
};

// "How the players see it": question, picture and the four coloured answers.
class PreviewWidget final : public QWidget {
  public:
    std::function<QVariantMap()> question;
    using QWidget::QWidget;

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto q = question ? question() : QVariantMap();
        const QRectF frame = QRectF(rect()).adjusted(28, 50, -28, -50);
        p.setFont(font());
        QFont caption = font();
        caption.setPixelSize(10);
        caption.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
        p.setFont(caption);
        p.setPen(QColor("#C4BFD9"));
        p.drawText(QRectF(28, 18, width() - 56, 24), Qt::AlignLeft | Qt::AlignVCenter, T("TAK ZOBACZĄ JE GRACZE"));
        QPainterPath card;
        card.addRoundedRect(frame, 16, 16);
        p.fillPath(card, QColor("#151527"));
        p.setPen(QColor("#59467B"));
        p.drawPath(card);
        QRectF area = frame.adjusted(24, 24, -24, -24);
        QFont small = font();
        small.setPixelSize(11);
        p.setFont(small);
        p.setPen(QColor("#C4A5FF"));
        p.drawText(QRectF(area.left(), area.top(), area.width(), 18), Qt::AlignLeft, q["category_name"].toString());
        QFont title = font();
        title.setPixelSize(25);
        title.setWeight(QFont::DemiBold);
        p.setFont(title);
        p.setPen(QColor("#F5F3FF"));
        const auto text = q["question"].toString().isEmpty() ? T("Treść pytania") : q["question"].toString();
        const QRectF textRect(area.left(), area.top() + 28, area.width(), 1000);
        const QRectF used = p.boundingRect(textRect, Qt::TextWordWrap, text);
        p.drawText(textRect, Qt::TextWordWrap, text);
        qreal y = used.bottom() + 18;
        const auto answers = q["answers"].toList();
        const qreal answerHeight = 50, gap = 10;
        const qreal answersTop = area.bottom() - 4 * answerHeight - 3 * gap;
        const auto image = q["image"].toString();
        if (!image.isEmpty()) {
            QImageReader reader(image);
            reader.setAutoTransform(true);
            const QImage picture = reader.read();
            if (!picture.isNull() && answersTop - y > 40) {
                const QSizeF box(area.width(), std::min<qreal>(210, answersTop - y - 18));
                const QSizeF scaled = QSizeF(picture.size()).scaled(box, Qt::KeepAspectRatio);
                p.drawImage(QRectF(area.left() + (area.width() - scaled.width()) / 2, y, scaled.width(),
                                   scaled.height()),
                            picture);
            }
        }
        QFont answerFont = font();
        answerFont.setPixelSize(16);
        p.setFont(answerFont);
        for (int i = 0; i < 4; ++i) {
            const QRectF box(area.left(), answersTop + i * (answerHeight + gap), area.width(), answerHeight);
            QPainterPath path;
            path.addRoundedRect(box, 8, 8);
            p.fillPath(path, answerColors[i].darker(360));
            p.setPen(answerColors[i]);
            p.drawPath(path);
            p.setPen(QColor("#F5F3FF"));
            p.drawText(box.adjusted(16, 0, -16, 0), Qt::AlignVCenter | Qt::TextWordWrap,
                       answers.value(i).toString());
        }
        QFont note = font();
        note.setPixelSize(11);
        p.setFont(note);
        p.setPen(QColor("#C4BFD9"));
        p.drawText(QRectF(28, height() - 40, width() - 56, 30), Qt::AlignLeft | Qt::AlignVCenter,
                   T("Podgląd układu pytania. Poprawna odpowiedź jest ukryta."));
    }
};

// Path field with a browse button (the build and install dialogs).
QLineEdit *pathRow(QVBoxLayout *layout, const QString &caption, const char *name,
                   const std::function<void(QLineEdit *)> &browse) {
    layout->addWidget(label(caption, "small"));
    auto *row = new QHBoxLayout;
    auto *edit = new QLineEdit;
    if (name)
        edit->setObjectName(name);
    row->addWidget(edit, 1);
    auto *choose = button(P("Wybierz…"));
    QObject::connect(choose, &QPushButton::clicked, edit, [edit, browse] { browse(edit); });
    row->addWidget(choose);
    layout->addLayout(row);
    return edit;
}
const QStringList titleIds{"BCES00647", "BCES00644", "BCES00645", "BCUS98209"};
} // namespace

MainWindow::MainWindow(StudioController &studioController, Localization &localization, QWidget *parent)
    : QMainWindow(parent), studio(studioController), i18n(localization) {
    studio.setNewProjectLanguage(i18n.language());
    setMinimumSize(1120, 760);
    resize(1440, 940);
    buildUi();
    connect(&studio, &StudioController::contentChanged, this, &MainWindow::refreshContent);
    connect(&studio, &StudioController::projectReplaced, this, &MainWindow::clearBuildReview);
    connect(&studio, &StudioController::selectionChanged, this, &MainWindow::refreshSelection);
    connect(&studio, &StudioController::currentMusicRecovered, this, [this](const QString &path) {
        playMedia(path, T("Muzyka"));
    });
    connect(&studio, &StudioController::currentNarrationRecovered, this, [this](const QString &path) {
        playMedia(path, T("Lektor pytania"));
    });
    connect(&studio, &StudioController::currentVideoRecovered, this, [this](const QString &path) {
        playMedia(path, T("WIDEO"), true);
    });
    connect(&studio, &StudioController::filterChanged, this, [this] {
        if (countText)
            countText->setText(QString::number(studio.visibleCount()) + T(" pytań") +
                               (categoryFilter.isEmpty() ? QString() : " · " + categoryFilter));
        refreshContent();
    });
    connect(&studio, &StudioController::statusChanged, this, [this] {
        if (statusText)
            statusText->setText(studio.status());
        if (overlayText)
            overlayText->setText(studio.status());
    });
    connect(&studio, &StudioController::historyChanged, this, &MainWindow::refreshHistory);
    connect(&studio, &StudioController::busyChanged, this, &MainWindow::refreshBusy);
    connect(&studio, &StudioController::buildProgressChanged, this, &MainWindow::refreshBusy);
    connect(&studio, &StudioController::sourceMediaProgress, this, &MainWindow::refreshBusy);
    connect(&studio, &StudioController::mediaFfmpegRequired, this, [this] {
        if (chooseMediaFfmpeg())
            studio.recoverSourceMedia();
        else
            buildAfterMediaRecovery = false;
    });
    connect(&studio, &StudioController::sourceMediaRecoveryFinished, this, [this](const QStringList &errors) {
        mediaRecoveryErrors = errors;
        const bool openBuilder = buildAfterMediaRecovery;
        buildAfterMediaRecovery = false;
        if (openBuilder)
            openBuild();
    });
    connect(&studio, &StudioController::projectReplaced, this, [this] { mediaRecoveryErrors.clear(); });
    connect(&studio, &StudioController::errorOccurred, this, [this](const QString &message) {
        if (!importWizard || !importWizard->showImportError(message))
            showError(message);
    });
    connect(&studio, &StudioController::importLanguageRequested, this, &MainWindow::showImportLanguages);
    connect(&studio, &StudioController::licenseRequested, this,
            [this](const QUrl &source, bool append, const QString &contentId, const QString &message) {
        if (!importWizard || !importWizard->requestLicense(contentId, message))
            askLicense(source, append, contentId, message);
    });
    connect(&studio, &StudioController::packFinished, this, &MainWindow::showPackResult);
    connect(&studio, &StudioController::packReviewRequired, this, &MainWindow::showPackResult);
    connect(&i18n, &Localization::languageChanged, this, [this] {
        studio.setNewProjectLanguage(i18n.language());
        commitEdits();
        // Rebuild after the current event: the language menu item is still in use.
        QTimer::singleShot(0, this, [this] { buildUi(); });
    });
    auto shortcut = [this](QKeySequence key, std::function<void()> action) {
        connect(new QShortcut(key, this), &QShortcut::activated, this, action);
    };
    shortcut(QKeySequence::Save, [this] { save(); });
    shortcut(QKeySequence::SaveAs, [this] { saveAs(); });
    shortcut(QKeySequence::Open, [this] { chooseProject(); });
    shortcut(QKeySequence::New, [this] { guard([this] { studio.newProject(); }); });
    shortcut(QKeySequence::Undo, [this] {
        commitEdits();
        studio.undo();
    });
    shortcut(QKeySequence::Redo, [this] {
        commitEdits();
        studio.redo();
    });
}

QVariantMap MainWindow::current() const { return studio.current(); }

void MainWindow::buildUi() {
    const auto searchText = search ? search->text() : QString();
    filterButtons.clear();
    tabButtons.clear();
    busyDisabled.clear();
    answerEdits.clear();
    correctButtons.clear();
    answerRows.clear();
    auto *root = new QWidget;
    root->setObjectName("root");
    auto *outer = new QVBoxLayout(root);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(buildHeader());
    auto *columns = new QHBoxLayout;
    columns->setSpacing(0);
    columns->addWidget(buildSidebar());
    columns->addWidget(buildList(), 4);
    columns->addWidget(divider(true));
    columns->addWidget(buildEditor(), 6);
    outer->addLayout(columns, 1);
    outer->addWidget(buildFooter());
    buildOverlay(root);
    setCentralWidget(root); // Deletes the previous build.
    search->setText(searchText);
    refreshContent();
    refreshSelection();
    refreshHistory();
    refreshBusy();
    setEditorTab(editorTab);
}

QWidget *MainWindow::buildHeader() {
    auto *hero = new QFrame;
    hero->setObjectName("hero");
    hero->setFixedHeight(78);
    auto *row = new QHBoxLayout(hero);
    row->setContentsMargins(24, 0, 24, 0);
    row->setSpacing(14);
    auto *icon = new QLabel;
    icon->setPixmap(QIcon(":/studio.png").pixmap(38, 38));
    row->addWidget(icon);
    auto *brand = new QVBoxLayout;
    brand->setSpacing(1);
    brand->addStretch();
    brand->addWidget(label("QUESTION STUDIO  /  PC EDITION", "brand"));
    brand->addWidget(label("Buzz! Quiz World", "title"));
    brand->addStretch();
    row->addLayout(brand);
    row->addSpacing(20);
    auto *line = divider(true);
    line->setFixedHeight(34);
    row->addWidget(line);
    row->addSpacing(8);
    auto *pack = new QVBoxLayout;
    pack->setSpacing(3);
    pack->addStretch();
    packTitle = label({}, "packTitle");
    saveState = label({}, "small");
    pack->addWidget(packTitle);
    pack->addWidget(saveState);
    pack->addStretch();
    row->addLayout(pack, 1);
    languageButton = button({});
    languageButton->setToolTip(T("Język pytań"));
    connect(languageButton, &QPushButton::clicked, this, &MainWindow::openProjectLanguages);
    auto *open = button(T("Otwórz"));
    open->setObjectName("openLibraryButton");
    open->setToolTip(T("Otwórz bibliotekę"));
    connect(open, &QPushButton::clicked, this, &MainWindow::chooseProject);
    auto *import = button(T("↓  Importuj"));
    import->setObjectName("openImportWizardButton");
    connect(import, &QPushButton::clicked, this, [this] { openImportWizard(); });
    auto *saveButton = button(T("Zapisz projekt"));
    connect(saveButton, &QPushButton::clicked, this, [this] { save(); });
    buildButton = button(T("Zbuduj paczkę"), "primary");
    buildButton->setObjectName("buildPackButton");
    buildButton->setProperty("primary", true);
    buildButton->setStyleSheet(
        "QPushButton{background:#ffcf63;color:#271b23;border:2px solid #ffcf63;font-weight:bold;padding:4px 20px;}"
        "QPushButton:hover{background:#ffe298;}QPushButton:disabled{background:#2c2b42;color:#a09ab3;"
        "border-color:#46445e;}");
    connect(buildButton, &QPushButton::clicked, this, &MainWindow::openBuild);
    auto *more = button(QStringLiteral("···"));
    auto *menu = new QMenu(more);
    auto *buildItem = menu->addAction(T("Zbuduj paczkę .edat…"), this, &MainWindow::openBuild);
    menu->addAction(T("Zainstaluj paczkę .edat…"), this, [this] {
        commitEdits();
        const auto file = QFileDialog::getOpenFileName(this, P("Wybierz paczkę do instalacji"), {}, "EDAT (*.edat)");
        if (!file.isEmpty())
            openInstall(file);
    });
    menu->addSeparator();
    auto *languages = menu->addMenu(T("Język interfejsu"));
    auto *languageGroup = new QActionGroup(languages);
    for (const auto &[code, name] : Localization::languages()) {
        auto *item = languages->addAction(name);
        item->setCheckable(true);
        item->setChecked(i18n.language() == code);
        languageGroup->addAction(item);
        connect(item, &QAction::triggered, this, [this, code = code] { i18n.setLanguage(code); });
    }
    menu->addAction(T("Języki paczki…"), this, &MainWindow::openProjectLanguages);
    menu->addAction(T("Kategorie paczki…"), this, &MainWindow::openPackCategories);
    menu->addAction(T("Plan rund…"), this, &MainWindow::openRoundPlans)->setObjectName("editRoundPlans");
    auto *assignCategories = menu->addAction(T("Przypisz kategorie po imporcie…"), this,
                                             [this] { openImportedCategories(false, this); });
    connect(menu, &QMenu::aboutToShow, assignCategories, [this, assignCategories] {
        assignCategories->setEnabled(!studio.busy() && studio.hasImportedSections());
    });
    auto *recoverMedia = menu->addAction(T("Odzyskaj media z gry"), this, &MainWindow::recoverSourceMedia);
    menu->addAction(T("Pytania wymagające mediów"), this, [this] { showUnresolvedMedia(mediaRecoveryErrors); });
    menu->addAction(P("Silnik mediów (FFmpeg)"), this, [this] { chooseMediaFfmpeg(); });
    menu->addSeparator();
    menu->addAction(T("Nowa paczka"), this, [this] { guard([this] { studio.newProject(); }); });
    menu->addAction(T("Zapisz jako…"), this, [this] { saveAs(); });
    menu->addAction(T("Eksportuj projekt z mediami (.buzzproject)…"), this, [this] {
        commitEdits();
        auto file = QFileDialog::getSaveFileName(this, T("Eksportuj projekt z mediami"), {},
                                                 T("Archiwum projektu (*.buzzproject)"));
        if (!file.isEmpty()) {
            if (!file.endsWith(".buzzproject", Qt::CaseInsensitive))
                file += ".buzzproject";
            studio.exportProject(QUrl::fromLocalFile(file));
        }
    });
    menu->addAction(T("Eksport JSON dla buzzpack…"), this, [this] {
        commitEdits();
        auto file = QFileDialog::getSaveFileName(this, T("Eksport JSON dla buzzpack"), {}, T("JSON (*.json)"));
        if (!file.isEmpty()) {
            if (!file.endsWith(".json", Qt::CaseInsensitive))
                file += ".json";
            studio.saveFile(QUrl::fromLocalFile(file), true);
        }
    });
    menu->addAction(T("Zmień nazwę paczki…"), this, &MainWindow::renamePack);
    menu->addSeparator();
    menu->addAction(T("Przykładowy projekt"), this, [this] {
        guard([this] { studio.newProject(true, studio.packLanguage()); });
    });
    auto *recover = menu->addAction(T("Odzyskaj autozapis"), this, [this] { guard([this] { studio.recover(); }); });
    menu->addAction(T("O aplikacji"), this, &MainWindow::showAbout);
    connect(menu, &QMenu::aboutToShow, this, [this, buildItem, recover, recoverMedia] {
        buildItem->setEnabled(studio.totalCount() > 0);
        recover->setEnabled(studio.recoveryAvailable());
        recoverMedia->setEnabled(studio.totalCount() > 0);
    });
    connect(more, &QPushButton::clicked, this, [more, menu] { menu->popup(more->mapToGlobal(QPoint(0, more->height()))); });
    for (auto *b : {languageButton, open, import, saveButton, buildButton, more}) {
        b->setMinimumHeight(38);
        row->addWidget(b);
        busyDisabled << b;
    }
    return hero;
}

QWidget *MainWindow::buildSidebar() {
    auto *side = new QFrame;
    side->setObjectName("sidebar");
    side->setFixedWidth(214);
    auto *col = new QVBoxLayout(side);
    col->setContentsMargins(16, 26, 16, 12);
    col->setSpacing(4);
    col->addWidget(label(T("TWOJA BIBLIOTEKA"), "section"));
    col->addSpacing(10);
    const QList<QPair<const char *, QString>> library{
        {"Wszystkie pytania", {}}, {"Z obrazkiem", "images"}, {"Z muzyką", "music"},
        {"Z lektorem", "audio"}, {"Szkice", "drafts"}};
    for (const auto &[title, key] : library) {
        auto *b = button({}, "filter");
        b->setCheckable(true);
        b->setProperty("kind", key);
        b->setAccessibleName(T(title));
        auto *inner = new QHBoxLayout(b);
        inner->setContentsMargins(12, 0, 10, 0);
        auto *name = label(T(title));
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *count = label({}, "small");
        count->setObjectName("count");
        count->setAttribute(Qt::WA_TransparentForMouseEvents);
        inner->addWidget(name, 1);
        inner->addWidget(count);
        b->setMinimumHeight(36);
        connect(b, &QPushButton::clicked, this, [this, key] {
            commitEdits();
            kindFilter = key;
            categoryFilter.clear();
            applyFilter();
        });
        filterButtons << b;
        col->addWidget(b);
    }
    col->addSpacing(14);
    col->addWidget(divider());
    col->addSpacing(14);
    col->addWidget(label(T("KATEGORIE"), "section"));
    categories = new QListWidget;
    categories->setObjectName("categories");
    categories->setCursor(Qt::PointingHandCursor);
    categories->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    categories->setTextElideMode(Qt::ElideRight);
    connect(categories, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        commitEdits();
        categoryFilter = item->data(Qt::UserRole).toString();
        applyFilter();
    });
    col->addWidget(categories, 1);
    auto *promo = new QFrame;
    promo->setObjectName("promo");
    auto *promoCol = new QVBoxLayout(promo);
    promoCol->setContentsMargins(13, 11, 13, 11);
    promoCol->setSpacing(6);
    auto *promoTitle = label(T("Zbuduj własny quiz"));
    promoTitle->setStyleSheet("font-weight:600;");
    promoCol->addWidget(promoTitle);
    auto *promoText = label(T("Dodaj pytania lub zaimportuj\nbibliotekę z KiP i Buzza."), "small");
    promoCol->addWidget(promoText);
    auto *formats = label("JSON · PAK · EDAT · PKG · ISO · CSO", "accent");
    formats->setStyleSheet("font-size:9px;");
    promoCol->addWidget(formats);
    col->addWidget(promo);
    col->addSpacing(10);
    col->addWidget(label("QUESTION STUDIO   /   " + QCoreApplication::applicationVersion(), "faint"));
    return side;
}

QWidget *MainWindow::buildList() {
    auto *panel = new QWidget;
    panel->setMinimumWidth(315);
    panel->setMaximumWidth(470);
    auto *col = new QVBoxLayout(panel);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    auto *head = new QVBoxLayout;
    head->setContentsMargins(22, 22, 22, 14);
    head->setSpacing(12);
    auto *titleRow = new QHBoxLayout;
    titleRow->addWidget(label(T("Pytania"), "heading"), 1);
    auto *add = button(T("+  Dodaj"), "primary");
    connect(add, &QPushButton::clicked, this, [this] {
        commitEdits();
        studio.addQuestion();
        setEditorTab(0);
    });
    busyDisabled << add;
    titleRow->addWidget(add);
    head->addLayout(titleRow);
    search = new QLineEdit;
    search->setPlaceholderText(T("Szukaj pytań i odpowiedzi…"));
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, [this] { applyFilter(); });
    head->addWidget(search);
    auto *countRow = new QHBoxLayout;
    countText = label({}, "small");
    countRow->addWidget(countText, 1);
    clearButton = button(T("Wyczyść"), "quiet");
    connect(clearButton, &QPushButton::clicked, this, [this] {
        categoryFilter.clear();
        kindFilter.clear();
        search->clear();
        applyFilter();
    });
    countRow->addWidget(clearButton);
    head->addLayout(countRow);
    col->addLayout(head);
    col->addWidget(divider());
    auto *stack = new QStackedWidget;
    stack->setObjectName("listStack");
    questionList = new QListView;
    questionList->setObjectName("questionList");
    questionList->setModel(studio.questions());
    auto *delegate = new QuestionDelegate(questionList);
    static QString chosen;
    delegate->currentId = &chosen;
    questionList->setItemDelegate(delegate);
    questionList->setMouseTracking(true);
    questionList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    questionList->setSelectionMode(QAbstractItemView::NoSelection);
    questionList->setUniformItemSizes(true);
    questionList->setCursor(Qt::PointingHandCursor);
    connect(questionList, &QListView::clicked, this, [this](const QModelIndex &index) {
        commitEdits();
        studio.select(index.data(QuestionModel::Uid).toString());
    });
    connect(&studio, &StudioController::selectionChanged, questionList, [this, list = questionList] {
        chosen = current()["id"].toString();
        list->viewport()->update();
    });
    chosen = current()["id"].toString();
    stack->addWidget(questionList);
    auto *empty = new QWidget;
    auto *emptyCol = new QVBoxLayout(empty);
    emptyCol->setContentsMargins(28, 0, 28, 0);
    emptyCol->addStretch();
    emptyTitle = label({}, "packTitle");
    emptyTitle->setStyleSheet("font-size:19px;");
    emptyTitle->setWordWrap(true);
    emptyText = label({}, "muted");
    emptyText->setWordWrap(true);
    auto *import = button(ImportWizard::tr("Importuj pytania"));
    import->setObjectName("emptyImportWizardButton");
    import->setProperty("primary", true);
    connect(import, &QPushButton::clicked, this, [this] { openImportWizard(); });
    busyDisabled << import;
    auto *example = button(T("Otwórz przykład"));
    example->setObjectName("exampleButton");
    connect(example, &QPushButton::clicked, this, [this] {
        guard([this] { studio.newProject(true, studio.packLanguage()); });
    });
    emptyCol->addWidget(emptyTitle);
    emptyCol->addWidget(emptyText);
    emptyCol->addWidget(import, 0, Qt::AlignLeft);
    emptyCol->addWidget(example, 0, Qt::AlignLeft);
    emptyCol->addStretch();
    stack->addWidget(empty);
    col->addWidget(stack, 1);
    const auto refreshEmpty = [this, stack, import, example] {
        stack->setCurrentIndex(studio.visibleCount() == 0 ? 1 : 0);
        const bool empty = studio.totalCount() == 0;
        import->setVisible(empty);
        example->setVisible(empty);
    };
    connect(&studio, &StudioController::filterChanged, stack, refreshEmpty);
    refreshEmpty();
    return panel;
}

QWidget *MainWindow::buildEditor() {
    auto *panel = new QWidget;
    panel->setMinimumWidth(500);
    auto *col = new QVBoxLayout(panel);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    auto *tabs = new QHBoxLayout;
    tabs->setContentsMargins(24, 18, 24, 12);
    tabs->setSpacing(3);
    const char *names[] = {"Edytor pytania", "Podgląd"};
    for (int i = 0; i < 2; ++i) {
        auto *tab = button(T(names[i]), "tab");
        tab->setCheckable(true);
        connect(tab, &QPushButton::clicked, this, [this, i] {
            commitEdits();
            setEditorTab(i);
        });
        tabButtons << tab;
        tabs->addWidget(tab);
    }
    tabs->addStretch();
    undoButton = button(QStringLiteral("↶"), "quiet");
    undoButton->setToolTip(T("Cofnij (Ctrl+Z)"));
    connect(undoButton, &QPushButton::clicked, this, [this] {
        commitEdits();
        studio.undo();
    });
    redoButton = button(QStringLiteral("↷"), "quiet");
    redoButton->setToolTip(T("Ponów (Ctrl+Shift+Z)"));
    connect(redoButton, &QPushButton::clicked, this, [this] {
        commitEdits();
        studio.redo();
    });
    tabs->addWidget(undoButton);
    tabs->addWidget(redoButton);
    col->addLayout(tabs);
    col->addWidget(divider());
    col->addWidget(buildReviewControls());

    editorPages = new QStackedWidget;
    // Editor.
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    auto *body = new QWidget;
    body->setObjectName("scrollBody");
    auto *form = new QVBoxLayout(body);
    form->setContentsMargins(26, 22, 26, 22);
    form->setSpacing(16);
    auto *top = new QHBoxLayout;
    readyChip = label({}, "ready");
    top->addWidget(readyChip);
    sourceText = label({}, "small");
    top->addWidget(sourceText, 1);
    auto *duplicate = button(T("Duplikuj"), "quiet");
    connect(duplicate, &QPushButton::clicked, this, [this] {
        commitEdits();
        studio.duplicateCurrent();
    });
    auto *remove = button(T("Usuń"), "danger");
    connect(remove, &QPushButton::clicked, this, &MainWindow::confirmDelete);
    top->addWidget(duplicate);
    top->addWidget(remove);
    form->addLayout(top);
    form->addWidget(label(T("TREŚĆ PYTANIA"), "caption"));
    questionText = new QPlainTextEdit;
    questionText->setObjectName("questionText");
    questionText->setPlaceholderText(T("O co chcesz zapytać?"));
    questionText->setFixedHeight(108);
    questionText->setTabChangesFocus(true);
    questionText->installEventFilter(this);
    form->addWidget(questionText);
    auto *categoriesRow = new QGridLayout;
    categoriesRow->setHorizontalSpacing(14);
    auto *categoryHead = new QHBoxLayout;
    categoryHead->addWidget(label(T("KATEGORIA"), "caption"), 1);
    auto *ownCategories = button(T("Własne kategorie…"));
    ownCategories->setObjectName("ownCategoriesButton");
    connect(ownCategories, &QPushButton::clicked, this, &MainWindow::openPackCategories);
    categoryHead->addWidget(ownCategories);
    categoriesRow->addLayout(categoryHead, 0, 0);
    categoriesRow->addWidget(label(T("TRUDNOŚĆ"), "caption"), 0, 1);
    subjectCombo = new QComboBox;
    subjectCombo->setObjectName("subjectCombo");
    connect(subjectCombo, &QComboBox::activated, this, [this](int i) {
        const int id = subjectCombo->itemData(i).toInt();
        int sub = 0;
        for (const auto &s : subjects)
            if (s.id == id)
                sub = s.sub;
        for (const auto &value : studio.packCategories())
            if (!sub && value.toMap()["parent"].toInt() == id)
                sub = value.toMap()["id"].toInt();
        if (id)
            studio.updateCurrent({{"subject", id}, {"category", sub}, {"category_name", subjectCombo->itemText(i)}});
    });
    difficultyCombo = new QComboBox;
    difficultyCombo->addItems({T("Łatwe"), T("Średnie"), T("Trudne")});
    connect(difficultyCombo, &QComboBox::activated, this,
            [this](int i) { studio.updateCurrent({{"difficulty", i + 1}}); });
    categoriesRow->addWidget(subjectCombo, 1, 0);
    categoriesRow->addWidget(difficultyCombo, 1, 1);
    categoriesRow->setColumnStretch(0, 3);
    categoriesRow->setColumnStretch(1, 1);
    form->addLayout(categoriesRow);
    auto *subRow = new QHBoxLayout;
    subRow->addWidget(label(T("Podkategoria"), "small"));
    subCombo = new QComboBox;
    connect(subCombo, &QComboBox::activated, this, [this](int i) {
        const auto id = subCombo->itemData(i).toInt();
        if (id)
            studio.updateCurrent({{"category", id}});
    });
    subRow->addWidget(subCombo, 1);
    form->addLayout(subRow);
    auto *answersHead = new QHBoxLayout;
    answersHead->addWidget(label(T("ODPOWIEDZI"), "caption"), 1);
    answersHead->addWidget(label(T("Zaznacz poprawną"), "faint"));
    form->addLayout(answersHead);
    auto *correctGroup = new QButtonGroup(body);
    for (int i = 0; i < 4; ++i) {
        auto *row = new QFrame;
        row->setObjectName("answer");
        row->setMinimumHeight(49);
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(2, 6, 10, 6);
        line->setSpacing(10);
        auto *bar = new QFrame;
        bar->setFixedSize(4, 31);
        bar->setStyleSheet(QString("background:%1;border-radius:2px;border:0;").arg(answerColors[i].name()));
        line->addWidget(bar);
        auto *letter = label(QString(QChar('A' + i)));
        letter->setStyleSheet(QString("color:%1;font-weight:bold;").arg(answerColors[i].name()));
        line->addWidget(letter);
        auto *edit = new QLineEdit;
        edit->setObjectName("answerText");
        edit->setPlaceholderText(T("Wpisz odpowiedź"));
        connect(edit, &QLineEdit::editingFinished, this, [this, edit, i] {
            if (!loading && edit->text() != current()["answers"].toList().value(i).toString())
                studio.updateCurrent({{"answer_index", i}, {"answer_text", edit->text()}});
        });
        line->addWidget(edit, 1);
        auto *radio = new QRadioButton;
        radio->setToolTip(T("Poprawna odpowiedź"));
        correctGroup->addButton(radio, i);
        connect(radio, &QRadioButton::clicked, this, [this, i] {
            commitEdits();
            studio.updateCurrent({{"correct_index", i}});
        });
        line->addWidget(radio);
        answerEdits << edit;
        correctButtons << radio;
        answerRows << row;
        form->addWidget(row);
    }
    form->addWidget(divider());
    form->addWidget(label(T("MEDIA"), "caption"));
    auto *imageRow = new QHBoxLayout;
    imageRow->setSpacing(12);
    imageButton = new QPushButton;
    imageButton->setObjectName("quiet");
    imageButton->setFixedSize(100, 68);
    imageButton->setIconSize(QSize(94, 62));
    imageButton->setCursor(Qt::PointingHandCursor);
    imageButton->setStyleSheet("QPushButton{background:#222239;border:1px solid #40405c;border-radius:7px;}");
    auto chooseImage = [this] {
        commitEdits();
        const auto file = QFileDialog::getOpenFileName(this, T("Wybierz obrazek"), {},
                                                       T("Obrazki (*.png *.jpg *.jpeg *.bmp *.gif)"));
        if (!file.isEmpty())
            studio.updateCurrent({{"image", QUrl::fromLocalFile(file).toString()}});
    };
    connect(imageButton, &QPushButton::clicked, this, chooseImage);
    imageRow->addWidget(imageButton);
    auto *imageCol = new QVBoxLayout;
    imageName = label({});
    imageCol->addWidget(imageName);
    imageCol->addWidget(label(T("Obraz zachowuje swoje proporcje"), "faint"));
    auto *imageButtons = new QHBoxLayout;
    imageThumb = label({}); // Holds the "Add/Change picture" text of the button below.
    imageThumb->hide();
    auto *imageChange = button({});
    imageChange->setObjectName("imageChange");
    connect(imageChange, &QPushButton::clicked, this, chooseImage);
    imageRemove = button(T("Usuń"), "quiet");
    connect(imageRemove, &QPushButton::clicked, this, [this] { studio.updateCurrent({{"image", QString()}}); });
    imageButtons->addWidget(imageChange);
    imageButtons->addWidget(imageRemove);
    imageButtons->addStretch();
    imageCol->addLayout(imageButtons);
    imageRow->addLayout(imageCol, 1);
    form->addLayout(imageRow);
    auto *speechRow = new QHBoxLayout;
    speechName = label({}, "small");
    speechRow->addWidget(speechName, 1);
    auto *tts = button(T("Generuj głos"));
    connect(tts, &QPushButton::clicked, this, &MainWindow::openTts);
    auto *audio = button(T("Dodaj nagranie"));
    connect(audio, &QPushButton::clicked, this, [this] {
        commitEdits();
        const auto file = QFileDialog::getOpenFileName(this, T("Wybierz nagranie"), {},
                                                       T("Audio (*.wav *.mp3 *.ogg *.flac *.rsf *.wem)"));
        if (!file.isEmpty())
            studio.updateCurrent({{"speech", QUrl::fromLocalFile(file).toString()}});
    });
    speechPlay = button(T("Odtwórz"));
    speechPlay->setObjectName("previewSpeech");
    connect(speechPlay, &QPushButton::clicked, this, [this] { previewMedia("speech", T("Lektor pytania")); });
    busyDisabled << speechPlay;
    speechRemove = button(QStringLiteral("×"), "quiet");
    connect(speechRemove, &QPushButton::clicked, this, [this] { studio.updateCurrent({{"speech", QString()}}); });
    for (auto *b : {tts, audio, speechPlay, speechRemove})
        speechRow->addWidget(b);
    auto *videoRow = new QHBoxLayout;
    videoName = label({}, "small");
    videoRow->addWidget(videoName, 1);
    auto *video = button(T("Dodaj wideo"));
    connect(video, &QPushButton::clicked, this, [this] {
        commitEdits();
        const auto file = QFileDialog::getOpenFileName(this, T("Wybierz film"), {},
                                                       T("Wideo (*.mp4 *.mkv *.webm *.avi *.mov *.m4v *.mpg)"));
        if (!file.isEmpty())
            studio.updateCurrent({{"video", QUrl::fromLocalFile(file).toString()}});
    });
    videoPlay = button(T("Odtwórz"));
    videoPlay->setObjectName("previewVideo");
    connect(videoPlay, &QPushButton::clicked, this, [this] { previewMedia("video", T("WIDEO"), true); });
    busyDisabled << videoPlay;
    videoRemove = button(QStringLiteral("×"), "quiet");
    connect(videoRemove, &QPushButton::clicked, this, [this] { studio.updateCurrent({{"video", QString()}}); });
    for (auto *b : {video, videoPlay, videoRemove})
        videoRow->addWidget(b);
    form->addLayout(videoRow);
    decodeMediaButton = button(T("Zdekoduj media źródłowe"));
    connect(decodeMediaButton, &QPushButton::clicked, this, [this] {
        commitEdits();
        if (!studio::configuredMediaFfmpeg().isEmpty() || chooseMediaFfmpeg())
            studio.decodeSourceMedia();
    });
    busyDisabled << decodeMediaButton;
    form->addWidget(decodeMediaButton);
    mediaNote = label(T("Źródło zawiera media Buzza. Zachowano ich oryginalne odwołania."), "faint");
    mediaNote->setWordWrap(true);
    form->addWidget(mediaNote);
    musicNote = label(T("Muzyka"), "small");
    auto *musicRow = new QHBoxLayout;
    musicRow->addWidget(musicNote, 1);
    musicPlay = button(T("Odtwórz"));
    musicPlay->setObjectName("previewMusic");
    connect(musicPlay, &QPushButton::clicked, this, [this] { previewMedia("music", T("Muzyka")); });
    busyDisabled << musicPlay;
    musicRow->addWidget(musicPlay);
    form->addLayout(musicRow);
    form->addWidget(label(T("Lektor pytania"), "caption"));
    form->addLayout(speechRow);
    issues = label({}, "issues");
    issues->setWordWrap(true);
    form->addWidget(issues);
    idText = label({}, "faint");
    form->addWidget(idText);
    form->addStretch();
    scroll->setWidget(body);
    editorPages->addWidget(scroll);
    // Preview.
    auto *previewWidget = new PreviewWidget;
    previewWidget->question = [this] { return current(); };
    preview = previewWidget;
    editorPages->addWidget(preview);
    // Nothing selected.
    emptyState = new QWidget;
    auto *emptyCol = new QVBoxLayout(emptyState);
    emptyCol->addStretch();
    auto *emptyBox = new QWidget;
    emptyBox->setFixedWidth(330);
    auto *emptyInner = new QVBoxLayout(emptyBox);
    auto *emptyIcon = new QLabel;
    emptyIcon->setPixmap(QIcon(":/studio.png").pixmap(56, 56));
    emptyInner->addWidget(emptyIcon);
    auto *emptyHeading = label(T("Miejsce na dobre pytania"), "packTitle");
    emptyHeading->setStyleSheet("font-size:24px;");
    emptyHeading->setWordWrap(true);
    emptyInner->addWidget(emptyHeading);
    auto *emptyInfo = label(T("Wybierz pytanie z biblioteki, żeby zmienić treść, odpowiedzi i media. Wszystko "
                              "pozostaje na Twoim komputerze."),
                            "muted");
    emptyInfo->setWordWrap(true);
    emptyInner->addWidget(emptyInfo);
    emptyCol->addWidget(emptyBox, 0, Qt::AlignHCenter);
    emptyCol->addStretch();
    editorPages->addWidget(emptyState);
    col->addWidget(editorPages, 1);
    warningBar = new QFrame;
    warningBar->setObjectName("warningBar");
    warningBar->setFixedHeight(44);
    auto *warn = new QHBoxLayout(warningBar);
    warn->setContentsMargins(20, 0, 10, 0);
    warningText = label({});
    warningText->setStyleSheet("color:#ffcf63;background:transparent;");
    warn->addWidget(warningText, 1);
    importWarningsButton = button(T("Pokaż"), "quiet");
    connect(importWarningsButton, &QPushButton::clicked, this, &MainWindow::showWarnings);
    warn->addWidget(importWarningsButton);
    recoverMediaButton = button(T("Odzyskaj media z gry"));
    recoverMediaButton->setObjectName("recoverSourceMedia");
    connect(recoverMediaButton, &QPushButton::clicked, this, [this] {
        if (studio.hasRecoverableSourceMedia())
            recoverSourceMedia();
        else
            showUnresolvedMedia(mediaRecoveryErrors);
    });
    warn->addWidget(recoverMediaButton);
    col->addWidget(warningBar);
    return panel;
}

QWidget *MainWindow::buildFooter() {
    auto *footer = new QFrame;
    footer->setObjectName("footer");
    footer->setFixedHeight(34);
    auto *row = new QHBoxLayout(footer);
    row->setContentsMargins(22, 0, 22, 0);
    row->setSpacing(9);
    auto *dot = new QLabel(QStringLiteral("●"));
    dot->setStyleSheet("color:#8ddeb2;font-size:8px;");
    row->addWidget(dot);
    statusText = label(studio.status(), "small");
    row->addWidget(statusText, 1);
    footerCount = label({}, "faint");
    row->addWidget(footerCount);
    return footer;
}

void MainWindow::buildOverlay(QWidget *root) {
    overlay = new QWidget(root);
    overlay->setObjectName("overlay");
    overlay->setAttribute(Qt::WA_StyledBackground);
    auto *col = new QVBoxLayout(overlay);
    col->addStretch();
    auto *box = new QWidget;
    box->setFixedWidth(560);
    auto *inner = new QVBoxLayout(box);
    inner->setSpacing(15);
    overlayText = label(studio.status(), "overlayText");
    overlayText->setWordWrap(true);
    overlayText->setAlignment(Qt::AlignCenter);
    inner->addWidget(overlayText);
    overlayProgress = new QProgressBar;
    overlayProgress->setRange(0, 100);
    overlayProgress->setTextVisible(false);
    inner->addWidget(overlayProgress);
    overlayCancel = button(T("Anuluj"));
    connect(overlayCancel, &QPushButton::clicked, this, [this] {
        if (studio.sourceMediaRecoveryActive()) {
            buildAfterMediaRecovery = false;
            studio.cancelSourceMediaRecovery();
        } else
            studio.cancelBuild();
    });
    inner->addWidget(overlayCancel, 0, Qt::AlignHCenter);
    col->addWidget(box, 0, Qt::AlignHCenter);
    col->addStretch();
    overlay->hide();
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (overlay)
        overlay->setGeometry(centralWidget()->rect());
}

void MainWindow::refreshContent() {
    if (!packTitle)
        return;
    if (!reviewLanguage.isEmpty() && (reviewLanguage != studio.packLanguage() || studio.totalCount() == 0))
        clearBuildReview();
    packTitle->setText(studio.packTitle());
    languageButton->setText(studio.packLanguage().toUpper());
    footerCount->setText("UTF-8   ·   " + QString::number(studio.totalCount()) + T(" pytań   ·   ") +
#ifdef Q_OS_WIN
                         "Windows");
#else
                         "Linux");
#endif
    const int counts[] = {studio.totalCount(), studio.imageCount(), studio.musicCount(), studio.audioCount(), -1};
    for (int i = 0; i < filterButtons.size(); ++i)
        if (auto *count = filterButtons[i]->findChild<QLabel *>("count"))
            count->setText(counts[i] >= 0 ? QString::number(counts[i]) : QString());
    categories->clear();
    for (const auto &value : studio.categories()) {
        const auto c = value.toMap();
        auto *item = new QListWidgetItem(QStringLiteral("•  %1   %2").arg(c["name"].toString()).arg(c["count"].toInt()));
        item->setData(Qt::UserRole, c["name"]);
        item->setToolTip(c["name"].toString());
        categories->addItem(item);
        if (c["name"].toString() == categoryFilter)
            item->setSelected(true);
    }
    countText->setText(QString::number(studio.visibleCount()) + T(" pytań") +
                       (categoryFilter.isEmpty() ? QString() : " · " + categoryFilter));
    clearButton->setVisible(!search->text().isEmpty() || !categoryFilter.isEmpty() || !kindFilter.isEmpty());
    emptyTitle->setText(studio.totalCount() == 0 ? T("Twój quiz zaczyna się tutaj") : T("Brak pasujących pytań"));
    emptyText->setText(studio.totalCount() == 0
                           ? T("Dodaj pierwsze pytanie albo otwórz gotową bibliotekę. Możesz też poznać edytor "
                               "na przykładowej paczce.")
                           : T("Zmień wyszukiwanie lub wybierz inną kategorię."));
    const auto warnings = studio.importWarnings();
    const bool needsMedia = studio.hasUnresolvedSourceMedia();
    warningBar->setVisible(needsMedia || !warnings.isEmpty());
    warningText->setText(warnings.isEmpty() ? T("Pytania wymagają mediów.")
                                           : T("Uwagi importu (") + QString::number(warnings.size()) + ")");
    importWarningsButton->setVisible(!warnings.isEmpty());
    recoverMediaButton->setVisible(needsMedia);
    if (needsMedia)
        recoverMediaButton->setText(studio.hasRecoverableSourceMedia() ? T("Odzyskaj media z gry")
                                                                      : T("Pytania wymagające mediów"));
    buildButton->setEnabled(!studio.busy() && studio.totalCount() > 0);
    refreshFilterButtons();
    refreshHistory();
}

void MainWindow::refreshFilterButtons() {
    for (auto *b : filterButtons)
        b->setChecked(categoryFilter.isEmpty() && b->property("kind").toString() == kindFilter);
}

void MainWindow::refreshHistory() {
    if (!saveState)
        return;
    setWindowTitle((studio.dirty() ? QStringLiteral("● ") : QString()) + studio.packTitle() +
                   QStringLiteral(" — Buzz! Question Studio"));
    saveState->setText(studio.dirty() ? T("Niezapisane zmiany")
                       : studio.projectPath().isEmpty() ? T("Projekt lokalny")
                                                        : T("Wszystkie zmiany zapisane"));
    saveState->setObjectName(studio.dirty() ? "dirty" : "small");
    restyle(saveState);
    undoButton->setEnabled(studio.canUndo() && !studio.busy());
    redoButton->setEnabled(studio.canRedo() && !studio.busy());
}

void MainWindow::refreshSelection() {
    if (!questionText)
        return;
    const auto q = current();
    const bool has = q.contains("id");
    const bool reviewing = refreshBuildReview(q["id"].toString());
    editorPages->setCurrentIndex(has ? editorTab : 2);
    if (!has)
        return;
    loading = true;
    const auto question = q["question"].toString();
    if (questionText->toPlainText() != question)
        questionText->setPlainText(question);
    const auto answers = q["answers"].toList();
    for (int i = 0; i < 4; ++i) {
        const auto text = answers.value(i).toString();
        if (answerEdits[i]->text() != text)
            answerEdits[i]->setText(text);
        const bool correct = q["correct_index"].toInt() == i;
        correctButtons[i]->setChecked(correct);
        answerRows[i]->setProperty("correct", correct);
        restyle(answerRows[i]);
    }
    // Quiz World subjects, then the project's own.
    const auto own = studio.packCategories();
    fillCategorySubjects(subjectCombo, own);
    const int subject = q["subject"].toInt() ? subjectCombo->findData(q["subject"].toInt()) : -1;
    subjectCombo->setCurrentIndex(subject);
    if (subject < 0)
        subjectCombo->setPlaceholderText(q["category_name"].toString().isEmpty() ? T("Wybierz kategorię")
                                                                                 : q["category_name"].toString());
    difficultyCombo->setCurrentIndex(std::clamp(q.value("difficulty", 2).toInt(), 1, 3) - 1);
    fillCategoryChildren(subCombo, q["subject"].toInt(), own);
    subCombo->setPlaceholderText(T("Zachowaj kategorię źródłową"));
    subCombo->setCurrentIndex(subCombo->findData(q["category"].toInt()));
    auto problems = q["problems"].toStringList();
    const bool canRecoverVideo = q["canRecoverVideo"].toBool();
    if (canRecoverVideo) {
        const auto missing = QCoreApplication::translate(
            "Document", "Pytanie korzysta z nierozwiązanych mediów z gry. Przypisz obrazek lub film.");
        if (problems.removeAll(missing))
            problems << T("Kliknij Odtwórz przy wideo, aby odzyskać je z gry.");
    }
    readyChip->setText(problems.isEmpty() ? T("GOTOWE") : T("DO UZUPEŁNIENIA"));
    readyChip->setObjectName(problems.isEmpty() ? "ready" : "todo");
    restyle(readyChip);
    readyChip->setVisible(!reviewing);
    sourceText->setText(q["sourceLabel"].toString().isEmpty() ? T("Własne") : q["sourceLabel"].toString());
    QStringList rounds;
    for (const auto &value : q["source_rounds"].toList()) {
        const auto round = value.toMap()["round"].toString();
        if (!rounds.contains(round))
            rounds << round;
    }
    sourceText->setToolTip(rounds.join('\n'));
    const auto image = q["image"].toString();
    imageName->setText(image.isEmpty() ? T("Brak obrazu") : fileName(image));
    imageName->setToolTip(image);
    QImageReader reader(image);
    reader.setAutoTransform(true);
    const auto picture = image.isEmpty() ? QImage() : reader.read();
    imageButton->setIcon(picture.isNull() ? QIcon() : QIcon(QPixmap::fromImage(picture)));
    imageButton->setText(picture.isNull() ? T("+ obrazek") : QString());
    if (auto *change = imageButton->parentWidget()->findChild<QPushButton *>("imageChange"))
        change->setText(image.isEmpty() ? T("Dodaj obrazek") : T("Zmień obrazek"));
    imageRemove->setVisible(!image.isEmpty());
    const auto speech = q["speech"].toString();
    const bool canRecoverNarration = q["canRecoverNarration"].toBool();
    speechName->setText(canRecoverNarration ? QString() :
                        speech.isEmpty() ? T("Brak nagrania lektora") : fileName(speech));
    speechName->setToolTip(speech);
    speechPlay->setVisible(!speech.isEmpty() || canRecoverNarration);
    speechRemove->setVisible(!speech.isEmpty());
    const auto video = q["video"].toString();
    videoName->setText(canRecoverVideo ? T("WIDEO") : video.isEmpty() ? T("Brak filmu") : fileName(video));
    videoName->setToolTip(video);
    videoPlay->setVisible(!video.isEmpty() || canRecoverVideo);
    videoRemove->setVisible(!video.isEmpty());
    mediaNote->setVisible(!canRecoverVideo && !q["source_media"].toList().isEmpty());
    musicNote->setVisible(q["usesMusic"].toBool());
    musicPlay->setVisible(q["hasMusicClip"].toBool() || q["canRecoverMusic"].toBool());
    decodeMediaButton->setVisible(q["canDecodeSourceMedia"].toBool() && !canRecoverVideo);
    issues->setText(problems.join('\n'));
    issues->setVisible(!reviewing && !problems.isEmpty());
    idText->setText("ID  " + q["id"].toString());
    preview->update();
    loading = false;
}

void MainWindow::refreshBusy() {
    if (!overlay)
        return;
    const bool busy = studio.busy();
    overlay->setGeometry(centralWidget()->rect());
    overlay->setVisible(busy);
    overlay->raise();
    overlayText->setText(studio.status());
    const bool recovering = studio.sourceMediaRecoveryActive();
    overlayProgress->setVisible(studio.buildActive() || recovering);
    overlayProgress->setValue(recovering ? studio.sourceMediaRecoveryPercent() : studio.buildProgress());
    overlayCancel->setVisible(studio.buildActive() || recovering);
    for (auto *b : busyDisabled)
        b->setEnabled(!busy);
    buildButton->setEnabled(!busy && studio.totalCount() > 0);
    editorPages->setEnabled(!busy);
    refreshHistory();
}

void MainWindow::applyFilter() {
    studio.setFilter(search->text(), categoryFilter, kindFilter);
    refreshFilterButtons();
    clearButton->setVisible(!search->text().isEmpty() || !categoryFilter.isEmpty() || !kindFilter.isEmpty());
}

void MainWindow::setEditorTab(int tab) {
    editorTab = tab;
    for (int i = 0; i < tabButtons.size(); ++i)
        tabButtons[i]->setChecked(i == tab);
    if (editorPages)
        editorPages->setCurrentIndex(current().contains("id") ? tab : 2);
    if (preview)
        preview->update();
}

void MainWindow::commitEdits() {
    if (loading || !questionText || !current().contains("id"))
        return;
    const auto q = current();
    if (questionText->toPlainText() != q["question"].toString())
        studio.updateCurrent({{"question", questionText->toPlainText()}});
    const auto answers = q["answers"].toList();
    for (int i = 0; i < answerEdits.size(); ++i)
        if (answerEdits[i]->text() != answers.value(i).toString())
            studio.updateCurrent({{"answer_index", i}, {"answer_text", answerEdits[i]->text()}});
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (watched == questionText && event->type() == QEvent::FocusOut)
        commitEdits();
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::guard(const std::function<void()> &action) {
    commitEdits();
    if (studio.busy())
        return;
    if (!studio.dirty()) {
        action();
        return;
    }
    QMessageBox box(importWizard && importWizard->isVisible() ? static_cast<QWidget *>(importWizard.data()) : this);
    box.setWindowTitle(T("Zapisać zmiany?"));
    box.setText(T("Bieżąca paczka zawiera niezapisane zmiany."));
    auto *cancel = box.addButton(T("Anuluj"), QMessageBox::RejectRole);
    auto *discard = box.addButton(T("Pomiń"), QMessageBox::DestructiveRole);
    auto *keep = box.addButton(T("Zapisz"), QMessageBox::AcceptRole);
    keep->setObjectName("primary");
    box.setDefaultButton(keep);
    box.exec();
    if (box.clickedButton() == discard)
        action();
    else if (box.clickedButton() == keep)
        save(action);
    Q_UNUSED(cancel)
}

void MainWindow::save(const std::function<void()> &done) {
    commitEdits();
    if (studio.projectPath().isEmpty()) {
        saveAs(done);
        return;
    }
    if (studio.saveFile() && done)
        done();
}

void MainWindow::saveAs(const std::function<void()> &done) {
    commitEdits();
    auto *parent = importWizard && importWizard->isVisible() ? static_cast<QWidget *>(importWizard.data()) : this;
    auto file = QFileDialog::getSaveFileName(parent, T("Zapisz projekt"), studio.projectPath(),
                                             T("Projekt Question Studio (*.buzz.json)"));
    if (file.isEmpty())
        return;
    if (!file.endsWith(".buzz.json", Qt::CaseInsensitive))
        file += file.endsWith(".buzz", Qt::CaseInsensitive) ? ".json" : ".buzz.json";
    if (studio.saveFile(QUrl::fromLocalFile(file)) && done)
        done();
}

void MainWindow::chooseProject() {
    if (studio.busy())
        return;
    const auto filter = ImportWizard::tr("Projekty Question Studio") + " (*.buzz.json *.buzzproject);;" +
                        ImportWizard::sourceFilter();
    const auto file = QFileDialog::getOpenFileName(this, T("Otwórz bibliotekę"), studio.projectPath(), filter);
    if (file.isEmpty())
        return;
    if (file.endsWith(".buzz.json", Qt::CaseInsensitive) || file.endsWith(".buzzproject", Qt::CaseInsensitive))
        guard([this, file] { studio.openFile(QUrl::fromLocalFile(file)); });
    else
        openImportWizard(file, false);
}

void MainWindow::openImportWizard(const QString &sourcePath, bool preferAppend) {
    if (studio.busy() || (importWizard && importWizard->isVisible()))
        return;
    commitEdits();
    auto *wizard = new ImportWizard(studio, preferAppend, [this](const auto &action) { guard(action); }, this);
    importWizard = wizard;
    wizard->setSourcePath(sourcePath);
    connect(wizard, &ImportWizard::assignCategoriesRequested, this, [this, wizard] {
        openImportedCategories(true, wizard);
    });
    connect(wizard, &ImportWizard::recoverMediaRequested, this, [this, wizard](const QString &language) {
        const auto media = studio.sourceMediaStatus(language, true);
        if (media.visualRecoverable && studio::configuredMediaFfmpeg().isEmpty() && !chooseMediaFfmpeg(wizard) &&
            !media.musicRecoverable) {
            wizard->recoveryNotStarted();
            return;
        }
        studio.recoverSourceMedia(language, true);
    });
    connect(wizard, &ImportWizard::buildPackRequested, this, &MainWindow::openBuild);
    showDialog(wizard);
}

void MainWindow::showDialog(QDialog *dialog) {
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    visibleDialog = dialog;
    dialog->open();
}

void MainWindow::showError(const QString &message) {
    auto *box = new QMessageBox(QMessageBox::Warning, T("Nie udało się wykonać operacji"), message, QMessageBox::NoButton,
                                this);
    box->addButton(T("OK"), QMessageBox::AcceptRole);
    showDialog(box);
}

void MainWindow::showWarnings() {
    auto *box = new QMessageBox(QMessageBox::Information, T("Uwagi importu"), studio.importWarnings().join("\n\n"),
                                QMessageBox::NoButton, this);
    box->addButton(T("OK"), QMessageBox::AcceptRole);
    showDialog(box);
}

void MainWindow::recoverSourceMedia() {
    commitEdits();
    studio.recoverSourceMedia();
}

void MainWindow::previewMedia(const char *field, const QString &title, bool video) {
    commitEdits();
    const auto question = current();
    if (QByteArrayView(field) == "music" && question["canRecoverMusic"].toBool()) {
        studio.recoverCurrentMusic();
        return;
    }
    if (QByteArrayView(field) == "speech" && question["canRecoverNarration"].toBool()) {
        studio.recoverCurrentMusic(true);
        return;
    }
    if (video && question["canRecoverVideo"].toBool()) {
        if (!studio::configuredMediaFfmpeg().isEmpty() || chooseMediaFfmpeg())
            studio.decodeSourceMedia(true);
        return;
    }
    playMedia(question.value(field).toString(), title, video);
}

void MainWindow::playMedia(const QString &path, const QString &title, bool video) {
    if (path.isEmpty())
        return;
    auto ffmpeg = studio::configuredMediaFfmpeg();
    if (MediaPreviewDialog::needsConversion(path) && ffmpeg.isEmpty()) {
        if (!chooseMediaFfmpeg())
            return;
        ffmpeg = studio::configuredMediaFfmpeg();
    }
    showDialog(new MediaPreviewDialog(path, title, video, ffmpeg, this));
}

bool MainWindow::chooseMediaFfmpeg(QWidget *parent) {
    if (!parent)
        parent = this;
    if (!studio::configuredMediaFfmpeg().isEmpty())
        return browseMediaFfmpeg(parent);

    const auto installation = studio::ffmpegInstallation();
    QMessageBox setup(QMessageBox::Information, P("Silnik mediów (FFmpeg)"),
                      T("Do przetwarzania mediów potrzebny jest FFmpeg."), QMessageBox::NoButton, parent);
    setup.setObjectName("mediaFfmpegSetup");
    auto *browse = setup.addButton(P("Wybierz FFmpeg"), QMessageBox::ActionRole);
    auto *download = setup.addButton(T("Strona pobierania FFmpeg"), QMessageBox::ActionRole);
    auto *cancel = setup.addButton(T("Anuluj"), QMessageBox::RejectRole);
    setup.setEscapeButton(cancel);
    QPushButton *install = nullptr, *copy = nullptr;
    if (!installation.program.isEmpty()) {
        install = setup.addButton(T("Zainstaluj przez %1").arg(installation.manager), QMessageBox::AcceptRole);
        setup.setDefaultButton(install);
    } else if (!installation.terminalCommand.isEmpty()) {
        copy = setup.addButton(T("Kopiuj polecenie instalacji"), QMessageBox::ActionRole);
    }
    setup.exec();
    const auto *selected = setup.clickedButton();
    if (install && selected == install)
        return installMediaFfmpeg(installation, parent);
    if (selected == browse)
        return browseMediaFfmpeg(parent);
    if (selected == download)
        QDesktopServices::openUrl(QUrl("https://ffmpeg.org/download.html"));
    if (copy && selected == copy)
        copyFfmpegInstallCommand(parent, installation.terminalCommand);
    return false;
}

bool MainWindow::browseMediaFfmpeg(QWidget *parent) {
    if (!parent)
        parent = this;
    const auto file = QFileDialog::getOpenFileName(parent, P("Wybierz FFmpeg"), studio::configuredMediaFfmpeg());
    if (file.isEmpty())
        return false;
    if (!studio::verifyMediaFfmpeg(file)) {
        QMessageBox::warning(parent, P("Silnik mediów (FFmpeg)"), T("Wybierz działający plik wykonywalny FFmpeg."));
        return false;
    }
    QSettings().setValue("build/ffmpeg", file);
    return true;
}

bool MainWindow::installMediaFfmpeg(const studio::FfmpegInstallation &installation, QWidget *parent) {
    class InstallDialog final : public QDialog {
      public:
        using QDialog::QDialog;
        QProcess process;
        void reject() override {
            if (process.state() == QProcess::NotRunning)
                QDialog::reject();
        }
    } dialog(parent);
    dialog.setObjectName("mediaFfmpegInstall");
    dialog.setWindowTitle(P("Silnik mediów (FFmpeg)"));
    dialog.resize(560, 310);
    auto *column = new QVBoxLayout(&dialog);
    auto *status = label(T("Instalowanie FFmpeg przez %1…").arg(installation.manager));
    status->setWordWrap(true);
    column->addWidget(status);
    auto *progress = new QProgressBar;
    progress->setRange(0, 0);
    column->addWidget(progress);
    auto *log = new QPlainTextEdit;
    log->setReadOnly(true);
    log->setMaximumBlockCount(150);
    column->addWidget(log, 1);
    auto *buttons = new QDialogButtonBox;
    auto *browse = buttons->addButton(P("Wybierz FFmpeg"), QDialogButtonBox::ActionRole);
    auto *close = buttons->addButton(T("Zamknij"), QDialogButtonBox::RejectRole);
    QPushButton *copy = nullptr;
    if (!installation.terminalCommand.isEmpty()) {
        copy = buttons->addButton(T("Kopiuj polecenie instalacji"), QDialogButtonBox::ActionRole);
        copy->setEnabled(false);
        connect(copy, &QPushButton::clicked, &dialog, [&] {
            copyFfmpegInstallCommand(&dialog, installation.terminalCommand);
        });
    }
    browse->setEnabled(false);
    close->setEnabled(false);
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(browse, &QPushButton::clicked, &dialog, [this, &dialog] {
        if (browseMediaFfmpeg(&dialog))
            dialog.accept();
    });
    auto &process = dialog.process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    const auto appendOutput = [&] {
        auto output = QString::fromLocal8Bit(process.readAll().right(16000)).replace('\r', '\n').trimmed();
        if (!output.isEmpty())
            log->appendPlainText(output);
    };
    const auto showFailure = [&](const QString &message) {
        status->setText(message);
        progress->hide();
        browse->setEnabled(true);
        close->setEnabled(true);
        if (copy)
            copy->setEnabled(true);
    };
    connect(&process, &QProcess::readyReadStandardOutput, &dialog, appendOutput);
    connect(&process, &QProcess::errorOccurred, &dialog, [&](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            log->appendPlainText(process.errorString());
            showFailure(T("Instalacja FFmpeg nie powiodła się."));
        }
    });
    connect(&process, &QProcess::finished, &dialog, [&](int code, QProcess::ExitStatus exitStatus) {
        appendOutput();
        if (exitStatus != QProcess::NormalExit || code != 0) {
            showFailure(T("Instalacja FFmpeg nie powiodła się."));
            return;
        }
        const auto ffmpeg = studio::findMediaFfmpeg();
        if (!studio::verifyMediaFfmpeg(ffmpeg)) {
            showFailure(T("Instalacja zakończona. Wybierz plik wykonywalny FFmpeg."));
            return;
        }
        QSettings().setValue("build/ffmpeg", ffmpeg);
        dialog.accept();
    });
    QTimer::singleShot(0, &dialog, [&] {
        process.start(installation.program, installation.arguments);
        process.closeWriteChannel();
    });
    return dialog.exec() == QDialog::Accepted;
}

void MainWindow::editReportedQuestion(const QString &id) {
    categoryFilter.clear();
    kindFilter.clear();
    search->clear();
    applyFilter();
    studio.select(id);
    setEditorTab(0);
    const auto *model = questionList->model();
    const auto matching = model->match(model->index(0, 0), QuestionModel::Uid, id, 1, Qt::MatchExactly);
    if (!matching.isEmpty())
        questionList->scrollTo(matching.first());
}

QWidget *MainWindow::buildReviewControls() {
    auto *panel = new QFrame;
    panel->setObjectName("buildReviewPanel");
    buildReviewPanel = panel;
    auto *column = new QVBoxLayout(panel);
    column->setContentsMargins(22, 10, 22, 10);
    auto *heading = new QHBoxLayout;
    buildReviewTitle = label({}, "accent");
    buildReviewTitle->setWordWrap(true);
    heading->addWidget(buildReviewTitle, 1);
    auto *back = button(P("Wróć do budowania"));
    back->setObjectName("resumeBuildAfterReview");
    connect(back, &QPushButton::clicked, this, &MainWindow::openBuild);
    heading->addWidget(back);
    column->addLayout(heading);
    buildReviewReason = label({}, "buildReviewReason");
    buildReviewReason->setTextFormat(Qt::PlainText);
    buildReviewReason->setWordWrap(true);
    column->addWidget(buildReviewReason);
    auto *navigation = new QHBoxLayout;
    previousReviewButton = button(P("Poprzednie pytanie"));
    nextReviewButton = button(P("Następne pytanie"));
    previousReviewButton->setObjectName("previousBuildIssue");
    nextReviewButton->setObjectName("nextBuildIssue");
    navigation->addWidget(previousReviewButton);
    navigation->addWidget(nextReviewButton);
    navigation->addStretch();
    column->addLayout(navigation);
    connect(previousReviewButton, &QPushButton::clicked, this, [this] { moveBuildReview(-1); });
    connect(nextReviewButton, &QPushButton::clicked, this, [this] { moveBuildReview(1); });
    panel->hide();
    return panel;
}

int MainWindow::nextReviewIndex(int index, int direction) const {
    for (int i = index + direction; i >= 0 && i < reviewQuestionIds.size(); i += direction)
        if (studio.hasQuestion(reviewQuestionIds.at(i)))
            return i;
    return -1;
}

bool MainWindow::refreshBuildReview(const QString &selectedId) {
    if (reviewQuestionIds.isEmpty()) {
        buildReviewPanel->hide();
        return false;
    }
    buildReviewPanel->show();
    const int currentIndex = reviewQuestionIds.indexOf(selectedId);
    if (currentIndex >= 0)
        reviewIndex = currentIndex;
    auto title = P("Pytania do poprawy");
    if (currentIndex >= 0)
        title += " · " + P("%1 z %2").arg(currentIndex + 1).arg(reviewQuestionIds.size());
    buildReviewTitle->setText(title);
    const auto reason = currentIndex < 0 ? QString() : studio.currentBuildProblem();
    buildReviewReason->setText(reason.isEmpty() ? P("Poprawione") : reason);
    buildReviewReason->setVisible(currentIndex >= 0);
    const int previousFrom = currentIndex < 0 ? reviewIndex + 1 : reviewIndex;
    previousReviewButton->setEnabled(nextReviewIndex(previousFrom, -1) >= 0);
    nextReviewButton->setEnabled(nextReviewIndex(reviewIndex, 1) >= 0);
    return currentIndex >= 0;
}

void MainWindow::moveBuildReview(int direction) {
    commitEdits();
    const int activeIndex = reviewQuestionIds.indexOf(current()["id"].toString());
    const int from = activeIndex < 0 && direction < 0 ? reviewIndex + 1 : reviewIndex;
    const int next = nextReviewIndex(from, direction);
    if (next >= 0)
        editReportedQuestion(reviewQuestionIds.at(next));
}

void MainWindow::clearBuildReview() {
    reviewQuestionIds.clear();
    reviewLanguage.clear();
    reviewIndex = -1;
    reviewBuildOptions.clear();
    if (buildReviewPanel)
        buildReviewPanel->hide();
}

void MainWindow::showBuildIssues(const QVector<studio::BuildQuestionIssue> &questionsToFix) {
    auto *dialog = new QDialog(this);
    dialog->setObjectName("buildIssuesDialog");
    dialog->setWindowTitle(P("Pytania do poprawy"));
    dialog->setMinimumWidth(550);
    dialog->resize(640, 360);
    auto *column = new QVBoxLayout(dialog);
    auto *list = new QListWidget;
    list->setObjectName("buildIssuesList");
    list->setAccessibleName(dialog->windowTitle());
    for (const auto &issue : questionsToFix) {
        const auto text = issue.text.simplified();
        list->addItem(text.isEmpty() ? issue.id : text);
    }
    column->addWidget(list, 1);
    auto *reason = label({}, "buildIssueDetails");
    reason->setTextFormat(Qt::PlainText);
    reason->setWordWrap(true);
    column->addWidget(reason);
    auto *buttons = new QDialogButtonBox;
    buttons->addButton(P("Wróć do budowania"), QDialogButtonBox::RejectRole);
    auto *edit = buttons->addButton(P("Otwórz w edytorze"), QDialogButtonBox::AcceptRole);
    edit->setObjectName("editBuildIssue");
    edit->setProperty("primary", true);
    column->addWidget(buttons);
    connect(list, &QListWidget::currentRowChanged, dialog, [reason, edit, questionsToFix](int row) {
        edit->setEnabled(row >= 0);
        reason->setText(row >= 0 ? questionsToFix.at(row).reason : QString());
    });
    const auto open = [this, dialog, list, questionsToFix] {
        const int row = list->currentRow();
        if (row < 0 || !dialog->isVisible())
            return;
        for (const auto &issue : questionsToFix)
            reviewQuestionIds << issue.id;
        reviewIndex = row;
        reviewLanguage = studio.packLanguage();
        dialog->accept();
        editReportedQuestion(questionsToFix.at(row).id);
    };
    connect(buttons, &QDialogButtonBox::accepted, dialog, open);
    connect(list, &QListWidget::itemActivated, dialog, open);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::rejected, this, &MainWindow::openBuild);
    list->setCurrentRow(0);
    showDialog(dialog);
    list->setFocus();
}

void MainWindow::showUnresolvedMedia(const QStringList &errors) {
    const auto questions = studio.unresolvedMediaQuestions();
    if (questions.isEmpty()) {
        if (!errors.isEmpty())
            showError(errors.mid(0, 5).join("\n\n"));
        return;
    }
    auto *dialog = new QDialog(this);
    dialog->setObjectName("unresolvedMediaDialog");
    dialog->setWindowTitle(T("Pytania wymagające mediów"));
    dialog->resize(650, 420);
    auto *column = new QVBoxLayout(dialog);
    auto *note = label(T("Wybierz pytanie, aby sprawdzić, których mediów brakuje (%1).").arg(questions.size()));
    note->setWordWrap(true);
    column->addWidget(note);
    auto *list = new QListWidget;
    for (const auto &value : questions) {
        const auto question = value.toMap();
        const auto id = question["id"].toString();
        const auto text = question["text"].toString().simplified();
        auto title = text.isEmpty() ? id : text;
        if (question["missingMusic"].toBool())
            title = T("Brak muzyki: %1").arg(title);
        auto *item = new QListWidgetItem(title, list);
        item->setData(Qt::UserRole, id);
        item->setToolTip(id);
    }
    column->addWidget(list, 1);
    const auto open = [this, dialog](QListWidgetItem *item) {
        if (!dialog->isVisible())
            return;
        const auto id = item->data(Qt::UserRole).toString();
        dialog->accept();
        editReportedQuestion(id);
    };
    connect(list, &QListWidget::itemClicked, dialog, open);
    connect(list, &QListWidget::itemActivated, dialog, open);
    if (!errors.isEmpty()) {
        auto *details = new QPlainTextEdit(errors.mid(0, 5).join("\n\n"));
        details->setReadOnly(true);
        details->setMaximumHeight(80);
        column->addWidget(details);
    }
    auto *buttons = new QDialogButtonBox;
    buttons->addButton(T("Zamknij"), QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    column->addWidget(buttons);
    showDialog(dialog);
}

void MainWindow::showAbout() {
    auto *box = new QMessageBox(
        QMessageBox::NoIcon, "Buzz! Question Studio " + QCoreApplication::applicationVersion(),
        T("Natywny edytor pytań dla Windows i Linux.\n\nImportuje JSON, KiP oraz pytania z gier Buzz! na PS2, PSP, "
          "PS3 (płyty, paczki i DLC z licencją .rap) i PS4. Generuje WAV przez głosy Windows lub eSpeak NG. Buduje "
          "paczki EDAT z wybraną wersją językową, obrazkami i dźwiękiem oraz instaluje je z kopią zapasową.\n\n"
          "Projekt to folder z plikiem .buzz.json i jego mediami; całość można wyeksportować jako .buzzproject."),
        QMessageBox::NoButton, this);
    box->setIconPixmap(QIcon(":/studio.png").pixmap(56, 56));
    box->addButton(T("OK"), QMessageBox::AcceptRole);
    showDialog(box);
}

void MainWindow::renamePack() {
    bool ok = false;
    const auto name =
        QInputDialog::getText(this, T("Nazwa paczki"),
                              T("Nazwa paczki w wersji %1 (każdy język ma własną):")
                                  .arg(languageLabel(studio.packLanguage())),
                              QLineEdit::Normal, studio.packTitle(), &ok);
    if (ok && !name.trimmed().isEmpty())
        studio.setPackTitle(name.trimmed());
}

void MainWindow::confirmDelete() {
    QMessageBox box(this);
    box.setWindowTitle(T("Usunąć pytanie?"));
    box.setText(T("Pytanie zostanie usunięte z tej paczki. Możesz cofnąć tę zmianę."));
    box.addButton(T("Anuluj"), QMessageBox::RejectRole);
    auto *remove = box.addButton(T("Usuń"), QMessageBox::AcceptRole);
    remove->setObjectName("danger");
    box.exec();
    if (box.clickedButton() == remove) {
        commitEdits();
        studio.deleteCurrent();
    }
}

void MainWindow::askLicense(const QUrl &source, bool append, const QString &contentId, const QString &message) {
    auto *box = new QMessageBox(QMessageBox::Information, T("Licencja paczki PS3"),
                                message + "\n\n" +
                                    T("Plik .rap znajdziesz w folderze exdata konsoli lub emulatora (np. "
                                      "dev_hdd0/home/00000001/exdata). Studio zapamięta jego położenie."),
                                QMessageBox::NoButton, this);
    box->setObjectName("licenseDialog");
    box->addButton(T("Anuluj"), QMessageBox::RejectRole);
    auto *choose = box->addButton(T("Wskaż plik .rap…"), QMessageBox::AcceptRole);
    choose->setObjectName("primary");
    connect(box, &QMessageBox::buttonClicked, this, [this, choose, source, append, contentId](QAbstractButton *b) {
        if (b != choose)
            return;
        const auto rap = QFileDialog::getOpenFileName(this, T("Wskaż plik licencji .rap"), {},
                                                      T("Licencja PS3 (*.rap *.RAP)"));
        if (rap.isEmpty())
            return;
        const auto error = studio.addLicense(contentId, QUrl::fromLocalFile(rap));
        if (!error.isEmpty())
            showError(error);
        else
            studio.prepareImport(source, append);
    });
    showDialog(box);
}

void MainWindow::showImportLanguages(const QVariantList &choices, const QString &preferred) {
    auto *dialog = new QDialog(this);
    dialog->setObjectName("importLanguageDialog");
    dialog->setWindowTitle(T("Wybierz język importu"));
    dialog->setMinimumWidth(500);
    auto *col = new QVBoxLayout(dialog);
    col->setSpacing(12);
    col->addWidget(label(T("Języki wykryte w wybranym źródle:")));
    auto *combo = new QComboBox;
    combo->setObjectName("importLanguageCombo");
    for (const auto &value : choices)
        combo->addItem(value.toMap()["name"].toString(), value.toMap()["code"]);
    combo->setCurrentIndex(std::max(0, combo->findData(preferred)));
    col->addWidget(combo);
    for (const char *text : {"Pytania bez wybranego języka zostaną pominięte.",
                             "Import JSON zachowuje także pozostałe tłumaczenia. W paczkach Buzza wczytany zostanie "
                             "wybrany język; kolejne można zaimportować później."}) {
        auto *note = label(T(text), "muted");
        note->setWordWrap(true);
        col->addWidget(note);
    }
    auto *buttons = new QDialogButtonBox;
    buttons->addButton(T("Anuluj"), QDialogButtonBox::RejectRole);
    auto *ok = buttons->addButton(T("Importuj"), QDialogButtonBox::AcceptRole);
    ok->setObjectName("primary");
    col->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::accepted, this, [this, combo] { studio.finishImport(combo->currentData().toString()); });
    connect(dialog, &QDialog::rejected, &studio, &StudioController::cancelImport);
    showDialog(dialog);
}

void MainWindow::openProjectLanguages() {
    commitEdits();
    auto *dialog = new QDialog(this);
    dialog->setObjectName("projectLanguageDialog");
    dialog->setWindowTitle(T("Języki paczki"));
    dialog->setMinimumWidth(520);
    auto *col = new QVBoxLayout(dialog);
    col->setSpacing(12);
    col->addWidget(label(T("Edytowana wersja pytań"), "muted"));
    auto *combo = new QComboBox;
    combo->setObjectName("projectLanguageCombo");
    auto fill = [this, combo] {
        combo->clear();
        for (const auto &value : studio.editingLanguageOptions())
            combo->addItem(value.toMap()["name"].toString(), value.toMap()["code"]);
        combo->setCurrentIndex(combo->findData(studio.packLanguage()));
    };
    fill();
    connect(&studio, &StudioController::contentChanged, combo, fill);
    connect(combo, &QComboBox::activated, this, [this, combo] { studio.setPackLanguage(combo->currentData().toString()); });
    col->addWidget(combo);
    auto *info = label(T("Każdy język ma własną treść, cztery odpowiedzi i nagranie. Brakujące tłumaczenia pozostają "
                         "szkicami. Zmiana języka interfejsu nie zmienia pytań."),
                       "muted");
    info->setWordWrap(true);
    col->addWidget(info);
    auto *other = new QLineEdit;
    other->setPlaceholderText(T("Inny kod języka, np. en-GB — Enter"));
    connect(other, &QLineEdit::returnPressed, this, [this, other] {
        studio.setPackLanguage(other->text());
        other->clear();
    });
    col->addWidget(other);
    auto *note = label(T("Eksport dla buzzpack zapisuje tylko aktualnie wybrany język. Projekt .buzz.json zachowuje "
                         "wszystkie wersje."),
                       "muted");
    note->setWordWrap(true);
    col->addWidget(note);
    auto *buttons = new QDialogButtonBox;
    buttons->addButton(T("Zamknij"), QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    col->addWidget(buttons);
    showDialog(dialog);
}

QString MainWindow::languageLabel(const QString &code) const {
    for (const auto &value : studio.projectLanguages())
        if (value.toMap()["code"].toString() == code)
            return value.toMap()["name"].toString();
    return code;
}

void MainWindow::openImportedCategories(bool latestOnly, QWidget *parent) {
    commitEdits();
    if (studio.busy())
        return;
    const auto sections = studio.importedSections(latestOnly);
    if (sections.isEmpty())
        return;
    auto *dialog = new QDialog(parent);
    dialog->setObjectName("importedCategoriesDialog");
    dialog->setWindowTitle(T("Przypisz kategorie działom gry"));
    dialog->setMinimumWidth(640);
    dialog->resize(680, 500);
    auto *column = new QVBoxLayout(dialog);
    column->addWidget(label(latestOnly ? T("Zakres: ostatni import") : T("Zakres: cały projekt"), "accent"));
    auto *hint = label(T("Wybierz dział gry i kategorię docelową. Wybór działu obejmuje także jego poddziały."), "muted");
    hint->setWordWrap(true);
    column->addWidget(hint);

    constexpr int nameRole = Qt::UserRole, gameRole = Qt::UserRole + 1, pathRole = Qt::UserRole + 2;
    auto *tree = new QTreeWidget;
    tree->setObjectName("importedCategorySections");
    tree->setAccessibleName(T("Dział źródłowy"));
    tree->setHeaderLabels({T("Dział źródłowy"), T("Pytania")});
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    auto child = [](QTreeWidgetItem *parent, const QString &name, const QString &shown) {
        for (int i = 0; i < parent->childCount(); ++i)
            if (parent->child(i)->data(0, nameRole).toString() == name)
                return parent->child(i);
        auto *item = new QTreeWidgetItem(parent, QStringList{shown});
        item->setData(0, nameRole, name);
        return item;
    };
    for (const auto &section : sections) {
        auto *game = child(tree->invisibleRootItem(), section.game, section.game);
        game->setFlags(game->flags() & ~Qt::ItemIsSelectable);
        auto *item = game;
        QStringList path;
        for (const auto &part : section.path) {
            path << part;
            item = child(item, part, part.isEmpty() ? T("Bez działu źródłowego") : part);
            item->setData(0, gameRole, section.game);
            item->setData(0, pathRole, path);
            item->setData(1, Qt::DisplayRole, item->data(1, Qt::DisplayRole).toInt() + section.questions);
        }
    }
    tree->sortItems(0, Qt::AscendingOrder);
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        tree->topLevelItem(i)->setExpanded(true);
    column->addWidget(tree, 1);

    const auto own = studio.packCategories();
    auto *form = new QFormLayout;
    auto *subject = new QComboBox;
    subject->setObjectName("importedCategorySubject");
    fillCategorySubjects(subject, own);
    subject->setPlaceholderText(T("Wybierz kategorię"));
    subject->setCurrentIndex(-1);
    form->addRow(T("Kategoria docelowa"), subject);
    auto *category = new QComboBox;
    category->setObjectName("importedCategoryChild");
    category->setPlaceholderText(T("Wybierz podkategorię"));
    category->setEnabled(false);
    form->addRow(T("Podkategoria docelowa"), category);
    column->addLayout(form);
    auto *note = label(T("Przypisanie zastąpi kategorię pytań z tego działu. Zmianę można cofnąć."), "muted");
    note->setWordWrap(true);
    column->addWidget(note);
    auto *feedback = label({}, "accent");
    feedback->setWordWrap(true);
    column->addWidget(feedback);
    auto *buttons = new QDialogButtonBox;
    buttons->addButton(T("Zamknij"), QDialogButtonBox::RejectRole);
    auto *apply = buttons->addButton(T("Przypisz kategorię (%1)").arg(0), QDialogButtonBox::ActionRole);
    apply->setObjectName("applyImportedCategory");
    apply->setProperty("primary", true);
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    const auto refresh = [=] {
        const auto *item = tree->currentItem();
        const int count = item ? item->data(1, Qt::DisplayRole).toInt() : 0;
        apply->setText(T("Przypisz kategorię (%1)").arg(count));
        apply->setEnabled(count > 0 && category->currentData().toInt() > 0);
        feedback->clear();
    };
    connect(tree, &QTreeWidget::currentItemChanged, dialog, refresh);
    connect(subject, &QComboBox::currentIndexChanged, dialog, [=] {
        fillCategoryChildren(category, subject->currentData().toInt(), own);
        category->setEnabled(category->count() > 0);
        category->setCurrentIndex(category->count() == 1 ? 0 : -1);
        refresh();
    });
    connect(category, &QComboBox::currentIndexChanged, dialog, refresh);
    connect(apply, &QPushButton::clicked, dialog, [=] {
        const auto *item = tree->currentItem();
        if (!item)
            return;
        const int changes = studio.assignImportedSection(item->data(0, gameRole).toString(),
            item->data(0, pathRole).toStringList(), subject->currentData().toInt(),
            category->currentData().toInt(), subject->currentText(), latestOnly);
        feedback->setText(changes ? T("Pytania ze zmienioną kategorią: %1").arg(changes)
                                  : T("Te pytania mają już wybraną kategorię."));
    });
    tree->setCurrentItem(tree->topLevelItem(0)->child(0));
    showDialog(dialog);
}

void MainWindow::openPackCategories() {
    commitEdits();
    auto *dialog = new QDialog(this);
    dialog->setObjectName("packCategoriesDialog");
    dialog->setWindowTitle(T("Kategorie paczki"));
    dialog->setMinimumSize(680, 500);
    auto *col = new QVBoxLayout(dialog);
    col->setSpacing(12);
    auto *info = label(T("Własne tematy (np. „Harry Potter”) i ich podkategorie trafią do paczki obok kategorii "
                         "Quiz World. Gracze zobaczą je przy wyborze kategorii. Każdy temat ma co najmniej jedną "
                         "podkategorię. Wpisz nazwy w każdym języku, w którym zbudujesz paczkę."),
                       "muted");
    info->setWordWrap(true);
    col->addWidget(info);
    QStringList codes, headers;
    const auto active = studio.packLanguage();
    for (const auto &value : studio.projectLanguages()) {
        // The edited language comes first.
        const bool first = value.toMap()["code"].toString() == active;
        codes.insert(first ? 0 : codes.size(), value.toMap()["code"].toString());
        headers.insert(first ? 0 : headers.size(), value.toMap()["name"].toString());
    }
    const int activeColumn = std::max(0, int(codes.indexOf(active)));
    auto *tree = new QTreeWidget;
    tree->setObjectName("packCategoriesTree");
    tree->setColumnCount(codes.size());
    tree->setHeaderLabels(headers);
    tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked |
                          QAbstractItemView::EditKeyPressed);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setItemDelegate(new LimitedTextDelegate(tree));
    // New ids continue after the existing ones; the dialog owns the counter.
    auto nextId = std::make_shared<int>(10001);
    auto makeItem = [codes, nextId](int id, const QVariantMap &names) {
        auto *item = new QTreeWidgetItem;
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setData(0, Qt::UserRole, id);
        for (int i = 0; i < codes.size(); ++i)
            item->setText(i, names.value(codes[i]).toString());
        *nextId = std::max(*nextId, id + 1);
        return item;
    };
    QMap<int, QTreeWidgetItem *> subjectItems;
    for (const auto &value : studio.packCategories()) {
        const auto c = value.toMap();
        auto *item = makeItem(c["id"].toInt(), c["names"].toMap());
        if (const int parent = c["parent"].toInt(); parent && subjectItems.contains(parent)) {
            subjectItems[parent]->addChild(item);
        } else {
            tree->addTopLevelItem(item);
            subjectItems[c["id"].toInt()] = item;
        }
    }
    tree->expandAll();
    for (int i = 0; i < codes.size(); ++i)
        tree->setColumnWidth(i, std::max(160, 620 / std::max(1, int(codes.size()))));
    col->addWidget(tree, 1);
    auto *row = new QHBoxLayout;
    auto *addSubject = button(T("Nowy temat"));
    addSubject->setObjectName("addSubjectButton");
    auto *addSub = button(T("Nowa podkategoria"));
    addSub->setObjectName("addSubcategoryButton");
    auto *remove = button(T("Usuń"), "danger");
    const auto newSubject = T("Nowy temat"), general = T("Ogólne"), newSub = T("Nowa podkategoria");
    connect(addSubject, &QPushButton::clicked, dialog, [=] {
        auto *subject = makeItem(*nextId, {{active, newSubject}});
        subject->addChild(makeItem(*nextId, {{active, general}}));
        tree->addTopLevelItem(subject);
        subject->setExpanded(true);
        tree->setCurrentItem(subject, activeColumn);
        tree->editItem(subject, activeColumn);
    });
    connect(addSub, &QPushButton::clicked, dialog, [=] {
        auto *current = tree->currentItem();
        if (!current)
            return;
        auto *subject = current->parent() ? current->parent() : current;
        auto *sub = makeItem(*nextId, {{active, newSub}});
        subject->addChild(sub);
        subject->setExpanded(true);
        tree->setCurrentItem(sub, activeColumn);
        tree->editItem(sub, activeColumn);
    });
    connect(remove, &QPushButton::clicked, dialog, [tree] {
        auto *current = tree->currentItem();
        // A subject keeps at least one subcategory; removing the last one removes the subject.
        if (current && current->parent() && current->parent()->childCount() == 1)
            current = current->parent();
        delete current;
    });
    auto refreshButtons = [tree, addSub, remove] {
        addSub->setEnabled(tree->currentItem());
        remove->setEnabled(tree->currentItem());
    };
    connect(tree, &QTreeWidget::currentItemChanged, dialog, refreshButtons);
    refreshButtons();
    row->addWidget(addSubject);
    row->addWidget(addSub);
    row->addStretch(1);
    row->addWidget(remove);
    col->addLayout(row);
    auto *note = label(T("Do 40 znaków na nazwę. Pytania z usuniętego tematu wracają bez kategorii; zmianę można "
                         "cofnąć."),
                       "muted");
    note->setWordWrap(true);
    col->addWidget(note);
    auto *buttons = new QDialogButtonBox;
    buttons->addButton(T("Anuluj"), QDialogButtonBox::RejectRole);
    auto *ok = buttons->addButton(T("Zapisz kategorie"), QDialogButtonBox::AcceptRole);
    ok->setObjectName("primary");
    col->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
        auto names = [&](QTreeWidgetItem *item) {
            QVariantMap out;
            for (int i = 0; i < codes.size(); ++i)
                if (!item->text(i).simplified().isEmpty())
                    out[codes[i]] = item->text(i).simplified();
            return out;
        };
        const auto missing = T("Każda kategoria potrzebuje nazwy w edytowanym języku (%1).").arg(languageLabel(active));
        QVariantList subjectsList, subsList;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto *subject = tree->topLevelItem(i);
            const int id = subject->data(0, Qt::UserRole).toInt();
            if (subject->text(activeColumn).simplified().isEmpty())
                return showError(missing);
            subjectsList << QVariantMap{{"id", id}, {"parent", 0}, {"names", names(subject)}};
            for (int j = 0; j < subject->childCount(); ++j) {
                auto *sub = subject->child(j);
                if (sub->text(activeColumn).simplified().isEmpty())
                    return showError(missing);
                subsList << QVariantMap{{"id", sub->data(0, Qt::UserRole)}, {"parent", id}, {"names", names(sub)}};
            }
        }
        if (subjectsList.size() + subsList.size() > 99)
            return showError(T("Paczka może mieć najwyżej 99 kategorii własnych."));
        studio.setPackCategories(subjectsList + subsList);
        dialog->accept();
    });
    showDialog(dialog);
}

void MainWindow::openRoundPlans() {
    if (studio.busy())
        return;
    commitEdits();
    auto *dialog = new QDialog(this);
    dialog->setObjectName("roundPlansDialog");
    dialog->setWindowTitle(T("Plan rund"));
    dialog->setMinimumSize(500, 380);
    auto *layout = new QVBoxLayout(dialog);
    auto *modes = new QTabBar;
    modes->setObjectName("roundPlanMode");
    modes->setAccessibleName(T("Tryb gry"));
    modes->setExpanding(false);
    modes->setTabData(modes->addTab(T("Solo")), "1p");
    modes->setTabData(modes->addTab(T("Do 4 graczy")), "4p");
    modes->setTabData(modes->addTab(T("Do 8 graczy")), "8p");
    layout->addWidget(modes);

    auto *list = new QListWidget;
    list->setObjectName("roundPlanList");
    list->setAccessibleName(T("Plan rund"));
    auto *add = button(T("Dodaj rundę"));
    auto *remove = button(T("Usuń rundę"));
    auto *up = button(T("W górę"));
    auto *down = button(T("W dół"));
    add->setObjectName("roundPlanAdd");
    remove->setObjectName("roundPlanRemove");
    up->setObjectName("roundPlanMoveUp");
    down->setObjectName("roundPlanMoveDown");
    auto *items = new QHBoxLayout;
    items->addWidget(list, 1);
    auto *controls = new QVBoxLayout;
    controls->addWidget(add);
    controls->addWidget(remove);
    controls->addSpacing(8);
    controls->addWidget(up);
    controls->addWidget(down);
    controls->addStretch();
    items->addLayout(controls);
    layout->addLayout(items, 1);

    auto *type = new QComboBox;
    type->setObjectName("roundPlanType");
    auto *questionCount = new QSpinBox;
    questionCount->setObjectName("roundPlanQuestionCount");
    questionCount->setRange(0, studio::maxRoundQuestionCount);
    questionCount->setSpecialValueText(T("Domyślna"));
    auto *fields = new QFormLayout;
    fields->addRow(T("Typ rundy"), type);
    fields->addRow(T("Liczba pytań"), questionCount);
    layout->addLayout(fields);
    auto *buttons = new QDialogButtonBox;
    auto *reset = buttons->addButton(T("Przywróć domyślne"), QDialogButtonBox::ResetRole);
    reset->setObjectName("roundPlanReset");
    buttons->addButton(T("Anuluj"), QDialogButtonBox::RejectRole);
    auto *save = buttons->addButton(T("Zapisz"), QDialogButtonBox::AcceptRole);
    save->setObjectName("primary");
    layout->addWidget(buttons);

    const auto plans = std::make_shared<studio::RoundPlans>(studio.roundPlans());
    const auto mode = [modes] { return modes->tabData(modes->currentIndex()).toString(); };
    const auto roundLabel = [](const studio::RoundPlanEntry &round) {
        const auto label = roundTypeLabel(round.type);
        return round.questionCount == 0 ? label : QStringLiteral("%1 (%L2)").arg(label).arg(round.questionCount);
    };
    const auto updateReset = [=] {
        reset->setEnabled(plans->value(mode()) != studio::defaultRoundPlans().value(mode()));
    };
    const auto updateSelection = [=] {
        const QSignalBlocker changingType(type), changingCount(questionCount);
        const int row = list->currentRow();
        const auto round = plans->value(mode()).value(row);
        type->setEnabled(row >= 0);
        type->setCurrentIndex(type->findData(round.type));
        questionCount->setEnabled(row >= 0);
        questionCount->setValue(round.questionCount);
        remove->setEnabled(row >= 0 && list->count() > 1);
        up->setEnabled(row > 0);
        down->setEnabled(row >= 0 && row + 1 < list->count());
        updateReset();
    };
    const auto loadMode = [=] {
        const QSignalBlocker changingList(list), changingType(type);
        type->clear();
        for (const auto &choice : studio::roundTypesForMode(mode()))
            type->addItem(roundTypeLabel(choice), choice);
        list->clear();
        for (const auto &round : plans->value(mode()))
            list->addItem(roundLabel(round));
        list->setCurrentRow(0);
        updateSelection();
    };
    connect(modes, &QTabBar::currentChanged, dialog, loadMode);
    connect(list, &QListWidget::currentRowChanged, dialog, updateSelection);
    connect(add, &QPushButton::clicked, dialog, [=] {
        const int row = list->currentRow();
        const int position = row >= 0 ? row + 1 : list->count();
        const int choice = row >= 0 ? type->currentIndex() : 0;
        const QSignalBlocker changingList(list);
        const studio::RoundPlanEntry round{type->itemData(choice).toString()};
        (*plans)[mode()].insert(position, round);
        list->insertItem(position, roundLabel(round));
        list->setCurrentRow(position);
        updateSelection();
    });
    connect(remove, &QPushButton::clicked, dialog, [=] {
        const int row = list->currentRow();
        if (row < 0 || list->count() <= 1)
            return;
        const QSignalBlocker changingList(list);
        (*plans)[mode()].removeAt(row);
        delete list->takeItem(row);
        list->setCurrentRow(std::min(row, list->count() - 1));
        updateSelection();
    });
    connect(type, &QComboBox::currentIndexChanged, dialog, [=](int index) {
        const int row = list->currentRow();
        if (row >= 0 && index >= 0) {
            auto &round = (*plans)[mode()][row];
            round.type = type->itemData(index).toString();
            list->item(row)->setText(roundLabel(round));
            updateReset();
        }
    });
    connect(questionCount, &QSpinBox::valueChanged, dialog, [=](int count) {
        const int row = list->currentRow();
        if (row >= 0) {
            auto &round = (*plans)[mode()][row];
            round.questionCount = count;
            list->item(row)->setText(roundLabel(round));
            updateReset();
        }
    });
    const auto move = [=](int offset) {
        const int row = list->currentRow(), destination = row + offset;
        if (row < 0 || destination < 0 || destination >= list->count())
            return;
        const QSignalBlocker changingList(list);
        (*plans)[mode()].swapItemsAt(row, destination);
        list->insertItem(destination, list->takeItem(row));
        list->setCurrentRow(destination);
        updateSelection();
    };
    connect(up, &QPushButton::clicked, dialog, [=] { move(-1); });
    connect(down, &QPushButton::clicked, dialog, [=] { move(1); });
    connect(reset, &QPushButton::clicked, dialog, [=] {
        (*plans)[mode()] = studio::defaultRoundPlans().value(mode());
        loadMode();
    });
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, dialog, [this, plans, dialog] {
        studio.setRoundPlans(*plans);
        dialog->accept();
    });
    for (auto *button : {add, remove, up, down, reset})
        button->setAutoDefault(false);
    loadMode();
    showDialog(dialog);
    list->setFocus();
}

void MainWindow::openTts() {
    commitEdits();
    const auto q = current();
    auto *dialog = new QDialog(this);
    dialog->setObjectName("ttsDialog");
    dialog->setWindowTitle(T("Lektor pytania"));
    dialog->setMinimumWidth(560);
    auto *col = new QVBoxLayout(dialog);
    col->setSpacing(12);
#ifdef Q_OS_WIN
    col->addWidget(label(T("Głosy zainstalowane w Windows · generowanie lokalne"), "muted"));
#else
    col->addWidget(label(T("eSpeak NG · generowanie lokalne"), "muted"));
#endif
    auto *voices = new QComboBox;
    int preferredVoice = -1;
    for (const auto &value : studio.ttsVoices()) {
        const auto v = value.toMap();
        voices->addItem(v["name"].toString(), v["id"]);
        // The first voice of the project's content language.
        if (preferredVoice < 0 &&
            v["language"].toString().section('-', 0, 0) == studio.packLanguage().section('-', 0, 0))
            preferredVoice = voices->count() - 1;
    }
    voices->setCurrentIndex(std::max(0, preferredVoice));
    col->addWidget(voices);
    if (voices->count() == 0) {
#ifdef Q_OS_WIN
        auto *missing = label(T("Nie znaleziono głosów SAPI. Dodaj głos systemowy w ustawieniach Windows."));
#else
        auto *missing = label(T("Zainstaluj pakiet espeak-ng i uruchom edytor ponownie."));
#endif
        missing->setStyleSheet("color:#ffcf63;");
        missing->setWordWrap(true);
        col->addWidget(missing);
    }
    col->addWidget(label(T("Tekst lektora — możesz zmienić zapis wymowy"), "muted"));
    auto *text = new QPlainTextEdit;
    text->setObjectName("ttsText");
    text->setPlainText(q["tts_text"].toString().isEmpty() ? q["question"].toString() : q["tts_text"].toString());
    text->setFixedHeight(150);
    col->addWidget(text);
    auto *rateRow = new QHBoxLayout;
    rateRow->addWidget(label(T("Wolniej"), "muted"));
    auto *rate = new QSlider(Qt::Horizontal);
    rate->setRange(-5, 5);
    rateRow->addWidget(rate, 1);
    rateRow->addWidget(label(T("Szybciej"), "muted"));
    col->addLayout(rateRow);
    auto *note = label(T("Nagranie zostanie zapisane jako WAV i przypisane do pytania. Zmiana treści pytania później "
                         "wymaga ponownego wygenerowania głosu."),
                       "small");
    note->setWordWrap(true);
    col->addWidget(note);
    auto *buttons = new QDialogButtonBox;
    buttons->addButton(T("Anuluj"), QDialogButtonBox::RejectRole);
    auto *generate = buttons->addButton(T("Generuj WAV…"), QDialogButtonBox::AcceptRole);
    generate->setObjectName("primary");
    auto update = [generate, voices, text] {
        generate->setEnabled(voices->count() > 0 && !text->toPlainText().trimmed().isEmpty());
    };
    update();
    connect(text, &QPlainTextEdit::textChanged, dialog, update);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, dialog, [this, dialog, voices, text, rate] {
        const auto voice = voices->currentData().toString();
        const auto spoken = text->toPlainText();
        const int speed = rate->value();
        dialog->accept();
        auto file = QFileDialog::getSaveFileName(this, T("Zapisz wygenerowane nagranie"), {}, T("Nagranie WAV (*.wav)"));
        if (file.isEmpty())
            return;
        if (!file.endsWith(".wav", Qt::CaseInsensitive))
            file += ".wav";
        studio.generateAudio(QUrl::fromLocalFile(file), spoken, voice, speed);
    });
    col->addWidget(buttons);
    showDialog(dialog);
}

void MainWindow::openBuild() {
    commitEdits();
    if (studio.busy() || studio.totalCount() == 0)
        return;
    const auto reviewOptions = std::exchange(reviewBuildOptions, {});
    const bool wasReviewing = !reviewQuestionIds.isEmpty();
    clearBuildReview();
    if (wasReviewing)
        refreshSelection();
    const auto values = reviewOptions.isEmpty() ? studio.buildSettings() : reviewOptions;
    auto *dialog = new QDialog(this);
    dialog->setObjectName("buildPackDialog");
    dialog->setWindowTitle(P("Zbuduj paczkę quizów"));
    dialog->setMinimumWidth(680);
    auto *col = new QVBoxLayout(dialog);
    col->setSpacing(10);
    auto *summary = label(P("Język pytań: %1 · %2 pytań w projekcie").arg(studio.packLanguage()).arg(studio.totalCount()),
                          "accent");
    col->addWidget(summary);
    auto *checks = new QHBoxLayout;
    auto *readiness = label(P("Sprawdzanie pytań…"), "buildReadiness");
    readiness->setWordWrap(true);
    checks->addWidget(readiness);
    auto *review = button({});
    review->setObjectName("reviewBuildIssues");
    review->hide();
    checks->addWidget(review);
    auto *missing = label({}, "muted");
    missing->setWordWrap(true);
    missing->hide();
    checks->addWidget(missing);
    checks->addStretch();
    col->addLayout(checks);
    auto *info = label(P("To wstępna kontrola. Studio sprawdzi szablon i media podczas budowania, a pominięte pytania pokaże w raporcie."),
                       "muted");
    info->setWordWrap(true);
    col->addWidget(info);
    if (studio.hasRecoverableSourceMedia()) {
        auto *recover = button(T("Odzyskaj media z gry"));
        recover->setObjectName("buildRecoverMedia");
        col->addWidget(recover, 0, Qt::AlignLeft);
        connect(recover, &QPushButton::clicked, dialog, [this, dialog] {
            dialog->accept();
            buildAfterMediaRecovery = true;
            recoverSourceMedia();
        });
    }
    auto browseFile = [this](const QString &title, const QString &filter) {
        return [this, title, filter](QLineEdit *edit) {
            const auto file = QFileDialog::getOpenFileName(this, title, edit->text(), filter);
            if (!file.isEmpty())
                edit->setText(QDir::toNativeSeparators(file));
        };
    };
    auto browseFolder = [this](const QString &title) {
        return [this, title](QLineEdit *edit) {
            const auto folder = QFileDialog::getExistingDirectory(this, title, edit->text());
            if (!folder.isEmpty())
                edit->setText(QDir::toNativeSeparators(folder));
        };
    };
    auto *templatePath = pathRow(col, P("Wybierz szablon Quiz World PACK0100"), "buildTemplatePath",
                                 browseFile(P("Wybierz szablon Quiz World PACK0100"), "PAK / EDAT (*.PAK *.pak *.edat)"));
    auto *slotNote = label({}, "muted");
    slotNote->setWordWrap(true);
    slotNote->hide();
    col->addWidget(slotNote);
    connect(templatePath, &QLineEdit::textChanged, dialog, [this, slotNote](const QString &path) {
        const auto content = studio.packLanguage();
        const auto slot = studio::templateGameLanguage(path, content);
        slotNote->setVisible(!slot.isEmpty() && slot != content);
        if (!slot.isEmpty() && slot != content)
            slotNote->setText(P("Aby zagrać, wybierz w Quiz World język %1. Język pytań i nagrań: %2.")
                                  .arg(studio::languageName(slot), studio::languageName(content)));
    });
    templatePath->setText(values["templatePath"].toString());
    auto *numberRow = new QHBoxLayout;
    numberRow->addWidget(label(P("Numer paczki"), "muted"));
    auto *number = new QSpinBox;
    number->setObjectName("buildPackNumber");
    number->setRange(101, 9999);
    number->setValue(values["number"].toInt());
    numberRow->addWidget(number);
    numberRow->addStretch();
    numberRow->addWidget(label(P("Wersja gry"), "muted"));
    auto *title = new QComboBox;
    title->addItems(titleIds);
    title->setCurrentIndex(std::max(0, int(titleIds.indexOf(values["titleId"].toString()))));
    numberRow->addWidget(title);
    col->addLayout(numberRow);
    auto *output = pathRow(col, P("Katalog gotowych paczek"), "buildOutputPath",
                           browseFolder(P("Wybierz katalog gotowych paczek")));
    output->setText(values["outputDirectory"].toString());
    auto *runtime = pathRow(col, P("Środowisko gry (runtime) — do instalacji"), "buildRuntimePath",
                            browseFolder(P("Wybierz środowisko gry (runtime)")));
    runtime->setText(values["runtimeRoot"].toString());
    auto *runtimeNote = label(P("Instalacja zapisze paczkę w runtime i zachowa kopię poprzedniej wersji."), "muted");
    runtimeNote->setWordWrap(true);
    col->addWidget(runtimeNote);
    auto *advanced = new QCheckBox(P("Ustawienia zaawansowane"));
    advanced->setObjectName("buildAdvanced");
    col->addWidget(advanced);
    auto *advancedBox = new QWidget;
    auto *advancedCol = new QVBoxLayout(advancedBox);
    advancedCol->setContentsMargins(0, 0, 0, 0);
    auto *baseRow = new QHBoxLayout;
    baseRow->addWidget(label(P("Baza kategorii"), "muted"));
    auto *base = new QSpinBox;
    base->setObjectName("buildCategoryBase");
    base->setRange(500, 999);
    base->setValue(values["categoryBase"].toInt());
    baseRow->addWidget(base);
    baseRow->addWidget(label(P("Instalator sprawdzi kolizje."), "small"));
    baseRow->addStretch();
    advancedCol->addLayout(baseRow);
    auto *ffmpeg = pathRow(advancedCol, P("Silnik mediów (FFmpeg)"), "buildFfmpeg",
                           browseFile(P("Wybierz FFmpeg"), {}));
    ffmpeg->setText(values["ffmpeg"].toString());
    advancedBox->hide();
    connect(advanced, &QCheckBox::toggled, dialog, [dialog, advancedBox](bool on) {
        advancedBox->setVisible(on);
        dialog->adjustSize();
    });
    col->addWidget(advancedBox);
    connect(number, &QSpinBox::valueChanged, base, [base](int value) {
        if (value >= 101 && value <= 105)
            base->setValue(500 + (value - 101) * 100);
    });
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    auto *cancel = button(P("Anuluj"), "quiet");
    connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    auto *buildOnly = button(P("Zbuduj .edat"));
    buildOnly->setObjectName("startPackBuild");
    auto *buildInstall = button(P("Zbuduj i zainstaluj"), "primary");
    for (auto *b : {cancel, buildOnly, buildInstall})
        buttons->addWidget(b);
    col->addLayout(buttons);
    auto validate = [=] {
        const bool ready = !templatePath->text().isEmpty() && !output->text().isEmpty() && studio.totalCount() > 0;
        buildOnly->setEnabled(ready);
        buildInstall->setEnabled(ready && !runtime->text().isEmpty());
    };
    validate();
    for (auto *edit : {templatePath, output, runtime})
        connect(edit, &QLineEdit::textChanged, dialog, validate);
    const auto readOptions = [=] {
        return QVariantMap{{"number", number->value()},         {"categoryBase", base->value()},
                           {"templatePath", templatePath->text()}, {"outputDirectory", output->text()},
                           {"runtimeRoot", runtime->text()},      {"titleId", title->currentText()},
                           {"ffmpeg", ffmpeg->text()},             {"skipInvalid", true}};
    };
    auto *checker = new QFutureWatcher<studio::BuildReadiness>(dialog);
    connect(checker, &QFutureWatcher<studio::BuildReadiness>::finished, dialog, [=] {
        const auto counts = checker->result();
        readiness->setText(P("Gotowe: %1").arg(counts.ready));
        review->setText(P("Do poprawy: %1").arg(counts.issues.size()));
        review->setEnabled(!counts.issues.isEmpty());
        review->show();
        missing->setText(P("Puste w tym języku: %1").arg(counts.missingLanguage));
        missing->setVisible(counts.missingLanguage > 0);
        if (!counts.issues.isEmpty()) {
            connect(review, &QPushButton::clicked, dialog, [this, dialog, advanced, readOptions, questionsToFix = counts.issues] {
                reviewBuildOptions = readOptions();
                reviewBuildOptions["advanced"] = advanced->isChecked();
                dialog->accept();
                showBuildIssues(questionsToFix);
            });
        }
        checker->deleteLater();
    });
    checker->setFuture(studio.checkBuildReadiness());
    auto start = [=](bool install) {
        if (ffmpeg->text().isEmpty() && studio.packNeedsFfmpeg()) {
            if (studio::configuredMediaFfmpeg().isEmpty() && !chooseMediaFfmpeg())
                return;
            ffmpeg->setText(studio::configuredMediaFfmpeg());
        }
        const auto options = readOptions();
        dialog->accept();
        studio.buildPack(options, install);
    };
    connect(buildOnly, &QPushButton::clicked, dialog, [start] { start(false); });
    connect(buildInstall, &QPushButton::clicked, dialog, [start] { start(true); });
    advanced->setChecked(values["advanced"].toBool());
    showDialog(dialog);
}

void MainWindow::openInstall(const QString &source) {
    const auto values = studio.buildSettings();
    auto *dialog = new QDialog(this);
    dialog->setObjectName("installPackDialog");
    dialog->setWindowTitle(P("Zainstaluj paczkę"));
    dialog->setMinimumWidth(600);
    auto *col = new QVBoxLayout(dialog);
    col->setSpacing(12);
    auto *file = label(QDir::toNativeSeparators(source), "accent");
    file->setWordWrap(true);
    col->addWidget(file);
    auto *runtime = pathRow(col, P("Środowisko gry (runtime)"), "installRuntime", [this](QLineEdit *edit) {
        const auto folder = QFileDialog::getExistingDirectory(this, P("Wybierz środowisko gry (runtime)"), edit->text());
        if (!folder.isEmpty())
            edit->setText(QDir::toNativeSeparators(folder));
    });
    runtime->setText(values["runtimeRoot"].toString());
    auto *row = new QHBoxLayout;
    row->addWidget(label(P("Wersja gry"), "muted"));
    auto *title = new QComboBox;
    title->addItems(titleIds);
    title->setCurrentIndex(std::max(0, int(titleIds.indexOf(values["titleId"].toString()))));
    row->addWidget(title);
    row->addStretch();
    col->addLayout(row);
    auto *note = label(P("Sprawdzimy paczkę i kolizje kategorii. Jeśli zastępujesz wcześniejszą wersję, najpierw "
                         "powstanie jej kopia zapasowa. Po instalacji uruchom grę ponownie."),
                       "muted");
    note->setWordWrap(true);
    col->addWidget(note);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    auto *cancel = button(P("Anuluj"), "quiet");
    connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    auto *install = button(P("Zainstaluj"), "primary");
    install->setEnabled(!runtime->text().isEmpty());
    connect(runtime, &QLineEdit::textChanged, install, [install](const QString &t) { install->setEnabled(!t.isEmpty()); });
    connect(install, &QPushButton::clicked, dialog, [=] {
        dialog->accept();
        studio.installPack(QUrl::fromLocalFile(source), runtime->text(), title->currentText());
    });
    buttons->addWidget(cancel);
    buttons->addWidget(install);
    col->addLayout(buttons);
    showDialog(dialog);
}

void MainWindow::showPackResult(const QVariantMap &result) {
    auto *dialog = new QDialog(this);
    dialog->setObjectName("packResultDialog");
    const bool installed = !result["installedPath"].toString().isEmpty();
    const bool built = !result["path"].toString().isEmpty();
    dialog->setWindowTitle(installed ? P("Paczka zainstalowana")
                                   : built ? P("Paczka zbudowana") : P("Paczka nie została zbudowana"));
    dialog->setMinimumWidth(640);
    auto *col = new QVBoxLayout(dialog);
    col->setSpacing(12);
    if (built)
        col->addWidget(label(P("Język: %1 · Pytania: %2 · Kategorie: %3")
                                 .arg(result["language"].toString())
                                 .arg(result["questions"].toInt())
                                 .arg(result["categories"].toInt()),
                             "accent"));
    if (!result["error"].toString().isEmpty()) {
        auto *error = label(result["error"].toString(), "issues");
        error->setTextFormat(Qt::PlainText);
        error->setWordWrap(true);
        col->addWidget(error);
    }
    if (result["skipped"].toInt() > 0)
        col->addWidget(label(P("Pominięto %1 pytań bez wybranego języka.").arg(result["skipped"].toInt()), "muted"));
    const auto excluded = result["excluded"].toList();
    if (!excluded.isEmpty()) {
        auto *intro = label(P("Pominięte pytania (%1). Wybierz pytanie, aby otworzyć je w edytorze.")
                                .arg(excluded.size()), "muted");
        intro->setWordWrap(true);
        col->addWidget(intro);
        auto *list = new QListWidget;
        list->setObjectName("buildExcludedQuestions");
        list->setAccessibleName(intro->text());
        list->setFixedHeight(160);
        for (const auto &value : excluded) {
            const auto row = value.toMap();
            const auto id = row["id"].toString();
            const auto question = row["text"].toString().simplified();
            const auto reason = row["reason"].toString();
            auto *item = new QListWidgetItem((question.isEmpty() ? id : question) + "  ·  " + reason.simplified(), list);
            item->setData(Qt::UserRole, id);
            item->setToolTip("<p>" + reason.toHtmlEscaped().replace('\n', "<br>") + "</p>");
        }
        const auto open = [this, dialog](QListWidgetItem *item) {
            if (dialog->isVisible()) {
                dialog->accept();
                editReportedQuestion(item->data(Qt::UserRole).toString());
            }
        };
        connect(list, &QListWidget::itemClicked, dialog, open);
        connect(list, &QListWidget::itemActivated, dialog, open);
        col->addWidget(list);
    }
    if (built) {
        QString text = result["path"].toString();
        if (installed)
            text += "\n\n" + P("Instalacja:") + "\n" + result["installedPath"].toString();
        if (!result["backupPath"].toString().isEmpty())
            text += "\n\n" + P("Kopia poprzedniej wersji:") + "\n" + result["backupPath"].toString();
        auto *paths = new QPlainTextEdit(text);
        paths->setReadOnly(true);
        paths->setFixedHeight(130);
        col->addWidget(paths);
    }
    if (installed)
        col->addWidget(label(P("Uruchom grę ponownie i wybierz paczkę w grze z pakietami quizów."), "muted"));
    auto *buttons = new QHBoxLayout;
    const auto url = QUrl(result["fileUrl"].toString());
    if (built) {
        auto *reveal = button(P("Pokaż folder"));
        connect(reveal, &QPushButton::clicked, this, [this, url] { studio.revealFile(url); });
        buttons->addWidget(reveal);
    }
    buttons->addStretch();
    if (built && !installed) {
        auto *install = button(P("Zainstaluj…"));
        connect(install, &QPushButton::clicked, dialog, [this, dialog, url] {
            dialog->accept();
            openInstall(url.toLocalFile());
        });
        buttons->addWidget(install);
    }
    auto *done = button(P("Gotowe"), "primary");
    connect(done, &QPushButton::clicked, dialog, &QDialog::accept);
    buttons->addWidget(done);
    col->addLayout(buttons);
    showDialog(dialog);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (allowClose) {
        event->accept();
        return;
    }
    event->ignore();
    guard([this] {
        allowClose = true;
        QTimer::singleShot(0, this, &QWidget::close);
    });
}

QImage MainWindow::capture() {
    QImage image = grab().toImage();
    if (visibleDialog && visibleDialog->isVisible()) {
        QPainter p(&image);
        p.fillRect(image.rect(), QColor(21, 21, 39, 200));
        const QImage dialog = visibleDialog->grab().toImage();
        p.drawImage((image.width() - dialog.width()) / 2, (image.height() - dialog.height()) / 2, dialog);
    }
    return image;
}
