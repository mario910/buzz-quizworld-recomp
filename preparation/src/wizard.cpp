#include "wizard.h"
#include "crypto.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QIcon>
#include <QLibrary>
#include <QLocale>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#endif
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QSysInfo>
#include <QSettings>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>
#include <QWidget>

namespace {
struct KeySource {
    const char* url;
    qsizetype size;
    const char* sha256;
};

constexpr KeySource systemKeys{
    "https://raw.githubusercontent.com/bucanero/ArtemisPS3/d79b41e525e20251bd0d919100039ac0a6433956/ArtemisPS3-PRX/data/keys",
    24549, "61ad6e1fb93504886b6e490fef749eed7eb409adc79089311d9a41dec67bf484"};
constexpr KeySource firmwareHmac{
    "https://raw.githubusercontent.com/Sonyk/ps3keys/52b76dbc2f4a45d7898e68ec6ff5b512b67d658f/pup-hmac",
    64, "0ac9eb5ba3b1aa9ea2fbe9578552dd537b4ed20b348a3d6d4f612413426f4727"};
constexpr auto downloadedKeysPath = "runtime/keys/downloaded/ps3.keys";

bool hasSources(const QDir& dir) {
    return dir.exists("tools/prepare-game.py") && dir.exists("scripts/Setup-Buzz.ps1");
}

QString findCheckout() {
    for (const auto& start : {QCoreApplication::applicationDirPath(), QDir::currentPath()}) {
        QDir dir(start);
        do {
            const QDir bundled(dir.filePath("build-files"));
            if (bundled.exists("release-sources.json") && hasSources(bundled))
                return bundled.absolutePath();
            if (hasSources(dir))
                return dir.absolutePath();
        } while (dir.cdUp());
    }
    return {};
}

void hideConsole(QProcess& process) {
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#else
    Q_UNUSED(process)
#endif
}

QLabel* label(const QString& text, const QString& role) {
    auto* widget = new QLabel(text);
    widget->setObjectName(role);
    widget->setTextFormat(Qt::PlainText);
    widget->setWordWrap(true);
    return widget;
}

QVBoxLayout* card(QVBoxLayout& parent, const QString& role) {
    auto* frame = new QFrame;
    frame->setObjectName(role);
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(18, 13, 18, 13);
    layout->setSpacing(7);
    parent.addWidget(frame);
    return layout;
}

class Wizard final : public QWidget {
    QLabel status;
    QLabel *guidance{}, *prepareTitle{}, *heroDescription{};
    QWidget* inputs{};
    QProgressBar progress;
    QPlainTextEdit log;
    QCheckBox details{"Show activity log"};
    QNetworkAccessManager keyDownloads;
    QProcess process;
    QTimer recheck;
    QTemporaryDir controlFiles;
    QSpinBox jobs;
    QCheckBox upscaler{"Download the optional Real-ESRGAN texture upscaler"};
    QLineEdit *project{}, *game{}, *firmware{}, *keys{}, *update{}, *resume{};
    QPushButton *check{}, *setup{}, *submodules{}, *prepare{}, *play{}, *openGame{}, *pythonInstall{}, *gitInstall{}, *vsInstall{}, *linuxInstall{}, *cancel{};
    QPushButton *vsDownload{}, *recheckSystem{};
    QPushButton* downloadKeysButton{};
    QString powershell, python, git, winget, vsInstaller;
    QString packageManager;
    QStringList packageArgs;
    const bool linuxHost = QSysInfo::kernelType() == "linux";
    const bool windowsHost = QSysInfo::kernelType() == "winnt";
    bool supported = false;
    bool busy = false;
    bool localToolsReady = false;
    bool prepareAfterSetup = false;

    bool verifyPython(const QString& program, const QString& code) const {
        if (program.isEmpty()) return false;
        QProcess probe;
        hideConsole(probe);
        probe.setProcessEnvironment(process.processEnvironment());
        if (QFileInfo(root()).isDir()) probe.setWorkingDirectory(root());
        probe.start(program, {"-c", code});
        return probe.waitForFinished(3000) && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
    }

    QLineEdit* pathRow(QFormLayout& form, const QString& name, const QString& filter, bool folders,
                       const QString& placeholder = {}, const QString& fileLabel = "Browse…") {
        auto* row = new QWidget;
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* path = new QLineEdit;
        path->setPlaceholderText(placeholder);
        path->setAccessibleName(name);
        layout->addWidget(path, 1);
        if (!filter.isEmpty()) {
            auto* file = new QPushButton(fileLabel);
            file->setAccessibleName("Choose file for " + name);
            layout->addWidget(file);
            connect(file, &QPushButton::clicked, this, [this, path, name, filter] {
                const auto start = path->text().isEmpty()
                    ? QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) : path->text();
                const auto selected = QFileDialog::getOpenFileName(this, name, start, filter);
                if (!selected.isEmpty()) path->setText(selected);
            });
        }
        if (folders) {
            auto* folder = new QPushButton("Folder…");
            folder->setAccessibleName("Choose folder for " + name);
            layout->addWidget(folder);
            connect(folder, &QPushButton::clicked, this, [this, path, name] {
                const auto selected = QFileDialog::getExistingDirectory(this, name, path->text());
                if (!selected.isEmpty()) path->setText(selected);
            });
        }
        form.addRow(name, row);
        return path;
    }

    QPushButton* button(QLayout& layout, const QString& text, const std::function<void()>& action) {
        auto* result = new QPushButton(text);
        layout.addWidget(result);
        connect(result, &QPushButton::clicked, this, action);
        return result;
    }

    void openPage(const QUrl& url) {
        if (!QDesktopServices::openUrl(url)) {
            log.appendPlainText("Could not open the page in your browser. Open this address manually:\n" + url.toString());
            details.setChecked(true);
        }
    }

    QString root() const {
        if (project->text().isEmpty()) return {};
        const QDir dir(QFileInfo(project->text()).absoluteFilePath());
        const QDir bundled(dir.filePath("build-files"));
        return bundled.exists("release-sources.json") && hasSources(bundled)
            ? bundled.absolutePath() : dir.absolutePath();
    }
    QString cancelFile() const { return controlFiles.filePath("cancel"); }
    QString toolPython() const { return root() + (linuxHost ? "/toolenv/bin/python" : "/toolenv/Scripts/python.exe"); }
    QString gameExe() const { return root() + (linuxHost ? "/dist/BuzzQuizWorldRecomp" : "/dist/BuzzQuizWorldRecomp.exe"); }

    static bool succeeds(const QString& program, const QStringList& args) {
        if (program.isEmpty()) return false;
        QProcess probe;
        hideConsole(probe);
        probe.start(program, args);
        return probe.waitForFinished(2500) && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
    }

    bool hasInputs() const {
        return !game->text().isEmpty() && QFileInfo::exists(game->text()) &&
               !firmware->text().isEmpty() && QFileInfo::exists(firmware->text());
    }

    void showState(bool ready, const QString& problem) {
        inputs->setVisible(!ready);
        heroDescription->setText(ready ? "Your copy is ready to play on this PC."
                                       : "Set up and play your own copy on this PC.");
        prepareTitle->setText(ready ? "Ready to play" : "2   Prepare to play");
        play->setVisible(ready);
        openGame->setVisible(ready);
        prepare->setVisible(!ready);
        cancel->hide();
        progress.hide();
        downloadKeysButton->setEnabled(!root().isEmpty() && hasSources(QDir(root())));
        guidance->setText(ready ? "Play Buzz! or open the game folder to find your local copy."
            : !problem.isEmpty() ? problem
            : !hasInputs() ? "Choose your game disc and firmware above to continue."
            : !localToolsReady ? "Prepare game will install the local build dependencies first."
                               : "Ready to prepare your game. Keep this window open while it builds.");
    }

    void refreshLinux(QProcessEnvironment environment) {
        const auto homeBin = QDir::homePath() + "/.local/bin";
        const auto search = (environment.value("PATH") + ':' + homeBin).split(':', Qt::SkipEmptyParts);
        const auto locate = [&search](const QString& name) { return QStandardPaths::findExecutable(name, search); };
        environment.insert("PATH", search.join(':'));
        environment.insert("PYTHONIOENCODING", "utf-8");
        process.setProcessEnvironment(environment);
        python = locate("python3");
        git = locate("git");
        const bool basePython = verifyPython(python, "import sys, venv, ensurepip; assert sys.version_info >= (3, 10)");
        const QDir dir(root());
        const bool checkout = !root().isEmpty() && dir.exists(".git") && dir.exists("tools/build-linux.py");
        const bool sources = checkout && dir.exists("ps3recomp/tools/ppu_loader.py") && dir.exists("third_party/zlib/CMakeLists.txt");
        const bool tools = sources && dir.exists("toolenv/bin/cmake") && dir.exists("toolenv/bin/ninja") &&
            verifyPython(toolPython(),
            "import sys, capstone, construct, pycdlib, tabulate, tqdm; from Crypto.Cipher import AES; "
            "from importlib.metadata import version; from pathlib import Path; assert sys.version_info >= (3, 10); "
            "assert all(version(name.strip()) == pin.split(';', 1)[0].strip() for name, pin in "
            "(line.split('==', 1) for line in Path('requirements.txt').read_text().splitlines() "
            "if '==' in line and ';' not in line and not line.lstrip().startswith('#')))");
        localToolsReady = tools;
        const bool compiler = !locate("clang").isEmpty() && !locate("clang++").isEmpty() && !locate("c++").isEmpty();
        const bool buildTools = !locate("patch").isEmpty();
        const bool sdl = succeeds(locate("pkg-config"), {"--exists", "sdl2 >= 2.0.18"});
        QLibrary vulkan("vulkan", 1);
        const bool loader = vulkan.load();
        bool cpu = false;
#if defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
        __builtin_cpu_init();
        cpu = __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
#endif
        const QStringList systemPaths{"/usr/bin", "/bin"};
        packageManager = QStandardPaths::findExecutable("apt-get", systemPaths);
        if (!packageManager.isEmpty()) {
            packageArgs = QStringList{"install", "-y", "build-essential", "clang", "cmake", "ninja-build",
                "patch", "git", "python3-venv", "pkg-config", "libsdl2-dev", "libvulkan1", "mesa-vulkan-drivers", "vulkan-tools"};
        } else if (!(packageManager = QStandardPaths::findExecutable("dnf", systemPaths)).isEmpty()) {
            packageArgs = QStringList{"install", "-y", "gcc-c++", "clang", "cmake", "ninja-build",
                "patch", "git", "python3", "pkgconf-pkg-config", "pkgconfig(sdl2)", "vulkan-loader", "mesa-vulkan-drivers", "vulkan-tools"};
        } else if (!(packageManager = QStandardPaths::findExecutable("pacman", systemPaths)).isEmpty()) {
            packageArgs = QStringList{"-S", "--needed", "--noconfirm", "base-devel", "clang", "cmake", "ninja",
                "git", "python", "sdl2-compat", "vulkan-icd-loader", "vulkan-tools"};
        }
        const auto mark = [](bool ok) { return ok ? "OK" : "missing"; };
        const bool existingDist = checkout && dir.exists("dist");
        status.setText(QString("%1 | AVX2/FMA %2 | Git %3 | Python/venv %4 | Clang %5 | patch %6\n"
                               "Source checkout %7 | submodules %8 | local Python environment %9")
                           .arg(QSysInfo::prettyProductName(), mark(cpu), mark(!git.isEmpty()), mark(basePython),
                                mark(compiler), mark(buildTools), mark(checkout), mark(sources), mark(tools)) +
                       QString("\nSDL2 %1 | Vulkan loader %2").arg(mark(sdl), mark(loader)));
        if (!supported) status.setText(status.text() + "\nThe game build requires Linux x86-64.");
        if (!loader) status.setText(status.text() + "\nInstall the Vulkan loader and a compatible driver for your GPU.");
        if (existingDist) status.setText(status.text() + "\nMove the existing dist folder to prepare another copy.");
        linuxInstall->setEnabled(supported && !packageManager.isEmpty());
        submodules->setEnabled(supported && checkout && !sources && !git.isEmpty());
        setup->setEnabled(supported && sources && basePython);
        prepare->setEnabled(supported && checkout && sources && (tools || setup->isEnabled()) &&
                            cpu && compiler && buildTools && sdl && loader &&
                            !git.isEmpty() && !existingDist && controlFiles.isValid() && hasInputs());
        const bool ready = supported && checkout && QFileInfo(gameExe()).isExecutable();
        linuxInstall->setVisible(!ready && !(basePython && compiler && buildTools && sdl && loader && !git.isEmpty()));
        QString problem;
        if (!supported || !cpu)
            problem = "Preparation requires a Linux x64 PC with AVX2 and FMA.";
        else if (!checkout || !sources)
            problem = "Choose your checkout and initialize its submodules in Advanced options.";
        else if (existingDist && !ready)
            problem = "Move the incomplete dist folder aside before preparing another copy.";
        else if (!controlFiles.isValid())
            problem = "BuzzSetup needs a writable temporary folder.";
        else if (!(basePython && compiler && buildTools && sdl && loader && !git.isEmpty()))
            problem = "Install the missing system tools below, then recheck in Advanced options.";
        showState(ready, problem);
    }

    void refresh() {
        if (busy) return;
        auto environment = QProcessEnvironment::systemEnvironment();
        if (linuxHost) {
            refreshLinux(environment);
            return;
        }
        auto search = environment.value("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
        for (const auto& scope : {QStringLiteral("User"), QStringLiteral("Machine")}) {
            const auto key = scope == "User"
                ? "HKEY_CURRENT_USER\\Environment"
                : "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";
            QSettings settings(key, QSettings::NativeFormat);
            search.append(settings.value("Path").toString().split(QDir::listSeparator(), Qt::SkipEmptyParts));
        }
        search.append(environment.value("LOCALAPPDATA") + "/Programs/Python/Python312");
        search.append(environment.value("ProgramFiles") + "/Git/cmd");
        auto locate = [&search](const QString& name) { return QStandardPaths::findExecutable(name, search); };
        powershell = locate("powershell.exe");
        git = locate("git.exe");
        winget = locate("winget.exe");
        python = locate("python.exe");
        const QString installedPython = environment.value("LOCALAPPDATA") + "/Programs/Python/Python312/python.exe";
        if (QFileInfo(installedPython).isFile()) python = installedPython;
        if (python.contains("/WindowsApps/", Qt::CaseInsensitive)) python.clear();
        environment.insert("PATH", search.join(QDir::listSeparator()));
        environment.remove("BUZZ_TOOL_ENV");
        environment.insert("PYTHONIOENCODING", "utf-8");
        process.setProcessEnvironment(environment);

        const QDir dir(root());
        const bool checkout = !root().isEmpty() && hasSources(dir);
        const bool sources = checkout && dir.exists("ps3recomp/tools/ppu_loader.py") && dir.exists("third_party/zlib/CMakeLists.txt");
        const bool basePython = supported && verifyPython(python, "import sys, venv; assert sys.version_info >= (3, 11) and sys.maxsize > 2**32");
        const bool tools = supported && sources && dir.exists("toolenv/Scripts/python.exe") &&
            dir.exists("toolenv/Scripts/cmake.exe") && dir.exists("toolenv/Scripts/ninja.exe") &&
            verifyPython(toolPython(), "import sys, capstone, clang_tool_chain, construct, pycdlib, tabulate, tqdm; "
                                       "from Crypto.Cipher import AES; from importlib.metadata import version; "
                                       "from pathlib import Path; assert sys.version_info >= (3, 11) and sys.maxsize > 2**32; "
                                       "assert all(version(name.strip()) == pin.split(';', 1)[0].strip() for name, pin in "
                                       "(line.split('==', 1) for line in Path('requirements.txt').read_text().splitlines() "
                                       "if '==' in line and not line.lstrip().startswith('#')))");
        const bool gitCheckout = checkout && QFileInfo::exists(dir.filePath(".git"));
        const bool bundled = checkout && !gitCheckout && dir.exists("release-sources.json");
        localToolsReady = tools;
        const auto vsDirectory = environment.value("ProgramFiles(x86)") + "/Microsoft Visual Studio/Installer/";
        const auto vswhere = vsDirectory + "vswhere.exe";
        vsInstaller = QFileInfo(vsDirectory + "setup.exe").isFile() ? vsDirectory + "setup.exe" : QString{};
        bool compiler = false;
        if (supported && QFileInfo(vswhere).isFile()) {
            QProcess probe;
            hideConsole(probe);
            probe.start(vswhere, {"-latest", "-products", "*", "-requires",
                                  "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"});
            if (probe.waitForFinished(2000))
                compiler = probe.exitCode() == 0 && !probe.readAllStandardOutput().trimmed().isEmpty();
        }
        bool sdk = false;
        const QDir kits(environment.value("ProgramFiles(x86)") + "/Windows Kits/10");
        for (const auto& version : QDir(kits.filePath("Include")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const auto include = "Include/" + version;
            const auto lib = "Lib/" + version;
            sdk |= kits.exists(include + "/um/Windows.h") && kits.exists(include + "/ucrt/stdio.h") &&
                   kits.exists(lib + "/um/x64/kernel32.lib") && kits.exists(lib + "/ucrt/x64/ucrt.lib");
        }
        const bool vulkan = QFileInfo(environment.value("SystemRoot") + "/System32/vulkan-1.dll").isFile();
        bool cpu = false;
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
        int registers[4];
        __cpuid(registers, 1);
        cpu = IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) && (registers[2] & (1 << 12));
#endif
        const bool existingDist = checkout && dir.exists("dist");
        const auto mark = [](bool ok) { return ok ? "OK" : "missing"; };
        status.setText(supported
            ? QString("Windows x64 | PowerShell %1 | Python %2 | C++ %3 | Windows SDK %4\n"
                      "Build sources %5 | local Python environment %6 | Vulkan loader %7")
                  .arg(mark(!powershell.isEmpty()), mark(basePython), mark(compiler), mark(sdk),
                       bundled && sources ? "included in this package" : mark(sources && gitCheckout),
                       mark(tools), mark(vulkan))
            : QString("%1 (%2). Full game preparation requires Windows or Linux x64.")
                  .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture()));
        if (supported && !vulkan) status.setText(status.text() + "\nInstall your GPU vendor’s Vulkan-capable driver, then recheck.");
        if (supported && !(compiler && sdk))
            status.setText(status.text() + "\nIn Visual Studio Installer, select Desktop development with C++ and a Windows SDK.");
        if (supported && !cpu) status.setText(status.text() + "\nThe game requires AVX2 and FMA.");
        if (supported && winget.isEmpty()) status.setText(status.text() + "\nUse the download pages for any missing tools.");
        if (!bundled && git.isEmpty()) status.setText(status.text() + "\nGit is required for source checkouts.");
        if (bundled && !sources) status.setText(status.text() + "\nExtract the complete BuzzPrepare ZIP before running it.");
        if (existingDist) status.setText(status.text() + "\nMove the existing dist folder to prepare another copy.");
        const bool needsPython = supported && !basePython && !tools;
        const bool needsBuildTools = supported && !(compiler && sdk);
        pythonInstall->setText(winget.isEmpty() ? "Download 64-bit Python" : "Install 64-bit Python");
        pythonInstall->setEnabled(needsPython);
        submodules->setVisible(!bundled);
        gitInstall->setEnabled(supported && !winget.isEmpty() && git.isEmpty());
        vsInstall->setText(vsInstaller.isEmpty() ? "Install C++ Build Tools and Windows SDK"
                                                 : "Open Visual Studio Installer");
        vsInstall->setEnabled(needsBuildTools && (!winget.isEmpty() || !vsInstaller.isEmpty()));
        submodules->setEnabled(supported && gitCheckout && !sources && !git.isEmpty());
        setup->setEnabled(supported && sources && basePython && !powershell.isEmpty());
        prepare->setEnabled(supported && (bundled || (gitCheckout && !git.isEmpty())) && sources &&
                            (tools || setup->isEnabled()) && compiler && sdk && cpu && vulkan && !existingDist &&
                            controlFiles.isValid() && !powershell.isEmpty() && hasInputs());
        const bool ready = supported && checkout && QFileInfo(gameExe()).isFile();
        pythonInstall->setVisible(!ready && needsPython);
        gitInstall->setVisible(!ready && !bundled && gitInstall->isEnabled());
        vsInstall->setVisible(!ready && vsInstall->isEnabled());
        vsDownload->setVisible(!ready && needsBuildTools);
        recheckSystem->setVisible(!ready && (needsPython || needsBuildTools));
        QString problem;
        if (!supported || !cpu)
            problem = "Preparation requires a Windows x64 PC with AVX2 and FMA.";
        else if (!checkout || (bundled && !sources))
            problem = "Extract the entire BuzzPrepare ZIP to a writable folder and reopen BuzzSetup.";
        else if (!sources || (!bundled && !gitCheckout))
            problem = "Choose your checkout and initialize its submodules in Advanced options.";
        else if (existingDist && !ready)
            problem = "Move the incomplete dist folder aside before preparing another copy.";
        else if (!controlFiles.isValid())
            problem = "BuzzSetup needs a writable temporary folder.";
        else if (needsBuildTools)
            problem = "C++ Build Tools or the Windows SDK are missing. In Visual Studio Installer, "
                      "select Desktop development with C++ and include a Windows SDK.";
        else if (needsPython)
            problem = "Install 64-bit Python 3.11 or later. If installing manually, add Python to PATH.";
        else if (!vulkan)
            problem = "Install a Vulkan-capable driver for your graphics card.";
        else if (powershell.isEmpty() || (!bundled && git.isEmpty()))
            problem = "A required system tool is missing. See the checklist in Advanced options.";
        showState(ready, problem);
    }

    void begin(const QString& activity) {
        busy = true;
        for (auto* control : findChildren<QPushButton*>()) control->setEnabled(false);
        for (auto* field : {project, game, firmware, keys, update, resume}) field->setEnabled(false);
        upscaler.setEnabled(false);
        jobs.setEnabled(false);
        guidance->setText(activity);
        progress.show();
    }

    void start(const QString& program, const QStringList& args, const QString& activity = "Installing dependencies…") {
        if (busy) return;
        begin(activity);
        process.setWorkingDirectory(QFileInfo(root()).isDir() ? root() : QDir::homePath());
        process.start(program, args);
    }

    void fetchKeySource(KeySource source, std::function<void(QByteArray)> next) {
        QNetworkRequest request{QUrl(source.url)};
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setRawHeader("Accept-Encoding", "identity");
        auto* reply = keyDownloads.get(request);
        reply->setReadBufferSize(source.size + 1);
        connect(reply, &QNetworkReply::readyRead, this, [reply, source] {
            if (reply->bytesAvailable() > source.size) reply->abort();
        });
        QTimer::singleShot(15000, reply, [reply] {
            if (!reply->isFinished()) reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, source, next = std::move(next)] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError ||
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
                done("Could not download the PS3 key sources. Retry or browse for a local key file.");
                return;
            }
            auto bytes = reply->readAll();
            if (bytes.size() != source.size ||
                QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() != source.sha256) {
                done("The PS3 key source did not match the verified download. Browse for a local key file.");
                return;
            }
            next(std::move(bytes));
        });
    }

    void downloadKeys() {
        if (busy || root().isEmpty() || !hasSources(QDir(root()))) return;
        begin("Downloading shared PS3 keys…");
        fetchKeySource(systemKeys, [this](QByteArray database) {
            fetchKeySource(firmwareHmac, [this, database = std::move(database)](QByteArray hmac) {
                try {
                    const auto output = QDir(root()).filePath(downloadedKeysPath);
                    prepare::atomicWrite(root(), output, prepare::systemKeyFile(database, hmac));
                    keys->setText(QDir::toNativeSeparators(output));
                    done("PS3 key file downloaded and selected.", true);
                } catch (const std::exception& error) {
                    done(QString::fromUtf8(error.what()));
                }
            });
        });
    }

    void done(const QString& result, bool success = false) {
        log.appendPlainText(result);
        busy = false;
        for (auto* control : findChildren<QPushButton*>()) control->setEnabled(true);
        for (auto* field : {project, game, firmware, keys, update, resume}) field->setEnabled(true);
        upscaler.setEnabled(true);
        jobs.setEnabled(true);
        refresh();
        const bool continuePreparation = prepareAfterSetup;
        prepareAfterSetup = false;
        if (continuePreparation && success) {
            if (localToolsReady && prepare->isEnabled())
                startPreparation();
            else {
                guidance->setText("Review the dependency checklist in Advanced options before trying again.");
                log.appendPlainText("Preparation could not start. Check the dependencies and input paths above.");
            }
        } else if (!success) {
            guidance->setText("This step stopped. See the activity log for details, then try again.");
            details.setChecked(true);
        } else if (play->isVisible()) {
            play->setFocus();
        }
    }

    void startSetup() {
        if (linuxHost) {
            start(python, {root() + "/tools/setup-linux.py"});
            return;
        }
        start(powershell, {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File",
                           root() + "/scripts/Setup-Buzz.ps1", "-Python", python, "-BuildOnly"});
    }

    void startPreparation() {
        QStringList args{root() + "/tools/prepare-game.py", "--dist", "--game",
                         QDir::toNativeSeparators(QFileInfo(game->text()).absoluteFilePath()),
                         "--firmware", QDir::toNativeSeparators(QFileInfo(firmware->text()).absoluteFilePath())};
        if (!keys->text().isEmpty()) args << "--keys" << QFileInfo(keys->text()).absoluteFilePath();
        if (!update->text().isEmpty()) args << "--update" << QFileInfo(update->text()).absoluteFilePath();
        if (upscaler.isChecked()) args << "--upscaler";
        if (!resume->text().isEmpty()) args << "--resume" << QFileInfo(resume->text()).absoluteFilePath();
        args << "--jobs" << QString::number(jobs.value()) << "--cancel-file" << cancelFile();
        if (QFile::exists(cancelFile()) && !QFile::remove(cancelFile())) {
            log.appendPlainText("Could not reset the cancellation marker.");
            return;
        }
        start(toolPython(), args, "Preparing your game. Keep this window open while the build finishes…");
        cancel->setVisible(busy);
        cancel->setEnabled(busy);
    }

    void install(const QString& id, const QStringList& extra = {}) {
        start(winget, QStringList{"install", "--exact", "--source", "winget", "--disable-interactivity", "--id", id} + extra);
    }

    void closeEvent(QCloseEvent* event) override {
        if (busy) {
            log.appendPlainText("Wait for the current operation to finish, or cancel preparation first.");
            event->ignore();
        } else {
            QWidget::closeEvent(event);
        }
    }

public:
    Wizard() {
        setObjectName("wizard");
        setWindowTitle("Buzz! Quiz World");
        resize(760, 620);
        setMinimumSize(470, 460);
        supported = (windowsHost || linuxHost) && QSysInfo::currentCpuArchitecture() == "x86_64";

        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        outer->addWidget(scroll);
        auto* body = new QWidget;
        body->setObjectName("body");
        auto* page = new QVBoxLayout(body);
        page->setContentsMargins(20, 18, 20, 18);
        page->setSpacing(12);
        scroll->setWidget(body);

        auto* hero = card(*page, "hero");
        auto* header = new QHBoxLayout;
        auto* emblem = new QLabel;
        emblem->setPixmap(windowIcon().pixmap(72, 72));
        header->addWidget(emblem, 0, Qt::AlignVCenter);
        header->setSpacing(15);
        auto* copy = new QVBoxLayout;
        copy->setSpacing(2);
        copy->addWidget(label("BUZZSETUP  /  PC EDITION", "brand"));
        copy->addWidget(label("Buzz! Quiz World", "title"));
        heroDescription = label({}, "muted");
        copy->addWidget(heroDescription);
        header->addLayout(copy, 1);
        hero->addLayout(header);

        auto* inputCard = card(*page, "card");
        inputs = inputCard->parentWidget();
        inputCard->addWidget(label("1   Choose your game files", "section"));
        inputCard->addWidget(label("Use your own PS3 disc image or game folder and PS3 firmware.", "muted"));
        auto* inputForm = new QFormLayout;
        inputForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
        inputForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        inputForm->setVerticalSpacing(8);
        inputCard->addLayout(inputForm);
        game = pathRow(*inputForm, "Game disc", "Disc images (*.iso)", true, "ISO or folder containing PS3_GAME", "ISO…");
        firmware = pathRow(*inputForm, "Firmware", "PS3 firmware (*.pup)", true, "PS3UPDAT.PUP or dev_flash folder", "PUP…");
        keys = pathRow(*inputForm, "Decryption keys", "All files (*)", false, "Required for encrypted files only");
        auto* inputHelp = new QHBoxLayout;
        inputCard->addLayout(inputHelp);
        button(*inputHelp, "Download PS3 firmware", [this] {
            const auto locale = QLocale::system().language() == QLocale::Polish ? "pl-pl" : "en-us";
            openPage(QUrl(QString("https://www.playstation.com/%1/support/hardware/ps3/system-software/").arg(locale)));
        });
        downloadKeysButton = button(*inputHelp, "Download key file", [this] { downloadKeys(); });
        inputHelp->addStretch();

        auto* prepareCard = card(*page, "card");
        prepareTitle = label({}, "section");
        guidance = label({}, "status");
        prepareCard->addWidget(prepareTitle);
        prepareCard->addWidget(guidance);
        progress.setRange(0, 0);
        progress.setTextVisible(false);
        progress.setAccessibleName("Preparation in progress");
        prepareCard->addWidget(&progress);
        auto* dependencies = new QVBoxLayout;
        prepareCard->addLayout(dependencies);
        auto* actions = new QHBoxLayout;
        prepareCard->addLayout(actions);

        auto* advanced = new QCheckBox("Advanced options");
        page->addWidget(advanced);
        auto* advancedForm = card(*page, "card");
        auto* options = new QFormLayout;
        options->setRowWrapPolicy(QFormLayout::WrapLongRows);
        options->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        advancedForm->addLayout(options);
        project = pathRow(*options, "Build sources", {}, true);
        project->setText(findCheckout());
        if (!root().isEmpty()) {
            const QDir source(root());
            const auto manual = source.filePath("runtime/keys/ps3.keys");
            const auto downloaded = source.filePath(downloadedKeysPath);
            if (QFileInfo(manual).isFile())
                keys->setText(manual);
            else if (QFileInfo(downloaded).isFile())
                keys->setText(downloaded);
        }
        update = pathRow(*options, "Game update", "PS3 updates (*.pkg *.bin *.elf)", false,
                         "Leave empty for the original 1.00 disc");
        resume = pathRow(*options, "Resume session", {}, true, "Folder from runtime/setup/sessions");
        jobs.setRange(1, 64);
        jobs.setValue(6);
        jobs.setAccessibleName("Parallel build jobs");
        options->addRow("Parallel build jobs", &jobs);
        options->addRow(&upscaler);
        status.setWordWrap(true);
        status.setTextFormat(Qt::PlainText);
        status.setObjectName("muted");
        advancedForm->addWidget(&status);
        check = button(*advancedForm, "Recheck system tools", [this] { refresh(); });
        submodules = button(*advancedForm, "Download source submodules", [this] {
            start(git, {"submodule", "update", "--init", "--recursive"});
        });
        setup = button(*advancedForm, "Install or repair local build dependencies", [this] { startSetup(); });
        advancedForm->parentWidget()->hide();
        connect(advanced, &QCheckBox::toggled, advancedForm->parentWidget(), &QWidget::setVisible);

        pythonInstall = button(*dependencies, "Install 64-bit Python", [this] {
            if (winget.isEmpty())
                openPage(QUrl("https://www.python.org/downloads/windows/"));
            else
                install("Python.Python.3.12");
        });
        gitInstall = button(*dependencies, "Install Git", [this] { install("Git.Git"); });
        vsInstall = button(*dependencies, "Install Visual Studio C++ Build Tools and Windows SDK", [this] {
            if (vsInstaller.isEmpty())
                install("Microsoft.VisualStudio.2022.BuildTools",
                        {"--override", "--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"});
            else if (!QProcess::startDetached(vsInstaller, {})) {
                log.appendPlainText("Could not open Visual Studio Installer.");
                details.setChecked(true);
            }
        });
        linuxInstall = button(*dependencies, "Install Linux system tools", [this] {
            const auto pkexec = QStandardPaths::findExecutable("pkexec", {"/usr/bin", "/bin"});
            auto shellArgs = packageArgs;
            shellArgs.replaceInStrings("pkgconfig(sdl2)", "'pkgconfig(sdl2)'");
            const QString command = QStringList{"sudo", packageManager}.join(' ') + ' ' + shellArgs.join(' ');
            if (pkexec.isEmpty()) {
                QApplication::clipboard()->setText(command);
                log.appendPlainText("Run the copied command in a terminal, then recheck:\n" + command);
                details.setChecked(true);
            } else {
                log.appendPlainText("If the graphical installer is unavailable, run in a terminal:\n" + command);
                start(pkexec, QStringList{packageManager} + packageArgs);
            }
        });
        auto* systemHelp = new QHBoxLayout;
        dependencies->addLayout(systemHelp);
        vsDownload = button(*systemHelp, "Download Build Tools from Microsoft", [this] {
            openPage(QUrl("https://learn.microsoft.com/en-us/visualstudio/releases/2022/release-history"));
        });
        recheckSystem = button(*systemHelp, "Recheck tools", [this] { refresh(); });
        systemHelp->addStretch();
        for (auto* control : {pythonInstall, gitInstall, vsInstall, vsDownload, recheckSystem})
            control->setVisible(windowsHost);
        linuxInstall->setVisible(linuxHost);

        prepare = button(*actions, "Prepare game", [this] {
            refresh();
            if (!prepare->isEnabled()) return;
            if (localToolsReady) {
                startPreparation();
            } else {
                prepareAfterSetup = true;
                log.appendPlainText("Installing local build dependencies before preparation…");
                startSetup();
            }
        });
        prepare->setObjectName("primary");
        cancel = button(*actions, "Cancel preparation", [this] {
            QFile marker(cancelFile());
            if (marker.open(QIODevice::WriteOnly)) {
                cancel->setEnabled(false);
                guidance->setText("Cancelling preparation…");
                log.appendPlainText("Cancellation requested. Waiting for preparation to stop.");
            } else {
                log.appendPlainText("Could not request cancellation.");
            }
        });
        play = button(*actions, "Play Buzz!", [this] {
            QProcess gameProcess;
            hideConsole(gameProcess);
            gameProcess.setProgram(gameExe());
            gameProcess.setWorkingDirectory(root() + "/dist");
            if (!gameProcess.startDetached()) {
                guidance->setText("Could not start the game. Open the game folder to check your copy.");
                log.appendPlainText("Could not start the prepared game.");
                details.setChecked(true);
            }
        });
        play->setObjectName("primary");
        openGame = button(*actions, "Open game folder", [this] {
            if (!QDesktopServices::openUrl(QUrl::fromLocalFile(root() + "/dist")))
                guidance->setText("Could not open the game folder.");
        });
        actions->addStretch();

        page->addWidget(&details);
        log.setReadOnly(true);
        log.setAccessibleName("Activity log");
        log.setMaximumBlockCount(3000);
        log.setMinimumHeight(135);
        log.hide();
        page->addWidget(&log);
        connect(&details, &QCheckBox::toggled, &log, &QWidget::setVisible);
        page->addStretch();
        recheck.setSingleShot(true);
        recheck.setInterval(250);
        connect(&recheck, &QTimer::timeout, this, [this] { refresh(); });
        connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive && recheckSystem->isVisible() && !busy)
                recheck.start();
        });
        for (auto* field : {project, game, firmware})
            connect(field, &QLineEdit::textChanged, this, [this] { recheck.start(); });
        hideConsole(process);
        process.setProcessChannelMode(QProcess::MergedChannels);
        connect(&process, &QProcess::readyRead, this, [this] {
            log.moveCursor(QTextCursor::End);
            log.insertPlainText(QString::fromUtf8(process.readAll()));
        });
        connect(&process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
            const bool success = exit == QProcess::NormalExit && code == 0;
            done(success ? "Completed." : QString("Operation failed (%1).").arg(code), success);
        });
        connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) done(process.errorString());
        });
        refresh();
    }
};
} // namespace

int runPreparationWizard(int argc, char** argv) {
#ifdef Q_OS_WIN
    DWORD consoleProcesses[2];
    if (GetConsoleProcessList(consoleProcesses, 2) == 1) FreeConsole();
#endif
    QApplication app(argc, argv);
    app.setApplicationDisplayName("BuzzSetup");
    app.setWindowIcon(QIcon(":/setup/buzz-setup.ico"));
    Wizard wizard;
    QFile stylesheet(":/setup/wizard.qss");
    if (stylesheet.open(QIODevice::ReadOnly))
        wizard.setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
    wizard.show();
    return app.exec();
}
