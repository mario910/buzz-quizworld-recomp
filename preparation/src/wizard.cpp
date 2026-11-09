#include "wizard.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCheckBox>
#include <QClipboard>
#include <QLibrary>
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
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QSysInfo>
#include <QSettings>
#include <QTextCursor>
#include <QTimer>
#include <functional>
#include <QWidget>

namespace {
QString findCheckout() {
    for (const auto& start : {QCoreApplication::applicationDirPath(), QDir::currentPath()}) {
        QDir dir(start);
        do {
            if (dir.exists("tools/prepare-game.py") && dir.exists("scripts/Setup-Buzz.ps1"))
                return dir.absolutePath();
        } while (dir.cdUp());
    }
    return {};
}

class Wizard final : public QWidget {
    QFormLayout form{this};
    QLabel status;
    QPlainTextEdit log;
    QProcess process;
    QTimer recheck;
    QTemporaryDir controlFiles;
    QSpinBox jobs;
    QCheckBox upscaler{"Download the optional Real-ESRGAN texture upscaler"};
    QLineEdit *project{}, *game{}, *firmware{}, *keys{}, *update{}, *resume{};
    QPushButton *check{}, *setup{}, *submodules{}, *prepare{}, *play{}, *pythonInstall{}, *gitInstall{}, *vsInstall{}, *linuxInstall{}, *cancel{};
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
        probe.setProcessEnvironment(process.processEnvironment());
        if (QFileInfo(root()).isDir()) probe.setWorkingDirectory(root());
        probe.start(program, {"-c", code});
        return probe.waitForFinished(3000) && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
    }

    QLineEdit* pathRow(const QString& name, const QString& filter, bool folders) {
        auto* row = new QWidget;
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* path = new QLineEdit;
        layout->addWidget(path, 1);
        if (!filter.isEmpty()) {
            auto* file = new QPushButton("File…");
            layout->addWidget(file);
            connect(file, &QPushButton::clicked, this, [this, path, name, filter] {
                const auto selected = QFileDialog::getOpenFileName(this, name, path->text(), filter);
                if (!selected.isEmpty()) path->setText(selected);
            });
        }
        if (folders) {
            auto* folder = new QPushButton("Folder…");
            layout->addWidget(folder);
            connect(folder, &QPushButton::clicked, this, [this, path, name] {
                const auto selected = QFileDialog::getExistingDirectory(this, name, path->text());
                if (!selected.isEmpty()) path->setText(selected);
            });
        }
        form.addRow(name, row);
        return path;
    }

    QPushButton* button(const QString& text, const std::function<void()>& action) {
        auto* result = new QPushButton(text);
        form.addRow(result);
        connect(result, &QPushButton::clicked, this, action);
        return result;
    }

    QString root() const {
        return project->text().isEmpty() ? QString{} : QFileInfo(project->text()).absoluteFilePath();
    }
    QString cancelFile() const { return controlFiles.filePath("cancel"); }
    QString toolPython() const { return root() + (linuxHost ? "/toolenv/bin/python" : "/toolenv/Scripts/python.exe"); }
    QString gameExe() const { return root() + (linuxHost ? "/dist/BuzzQuizWorldRecomp" : "/dist/BuzzQuizWorldRecomp.exe"); }

    static bool succeeds(const QString& program, const QStringList& args) {
        if (program.isEmpty()) return false;
        QProcess probe;
        probe.start(program, args);
        return probe.waitForFinished(2500) && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
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
                            !git.isEmpty() && !existingDist && controlFiles.isValid() &&
                            QFileInfo::exists(game->text()) && QFileInfo::exists(firmware->text()));
        cancel->setEnabled(false);
        play->setEnabled(supported && checkout && QFileInfo(gameExe()).isExecutable());
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
        const bool checkout = !root().isEmpty() && dir.exists("tools/prepare-game.py") && dir.exists("scripts/Setup-Buzz.ps1");
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
            probe.start(vswhere, {"-latest", "-products", "*", "-requires",
                                  "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"});
            if (probe.waitForFinished(2000))
                compiler = probe.exitCode() == 0 && !probe.readAllStandardOutput().trimmed().isEmpty();
        }
        bool sdk = false;
        const QDir kits(environment.value("ProgramFiles(x86)") + "/Windows Kits/10/Include");
        for (const auto& version : kits.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            sdk |= kits.exists(version + "/um/Windows.h") && kits.exists(version + "/ucrt/stdio.h");
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
        if (supported && winget.isEmpty()) status.setText(status.text() + "\nWinGet is unavailable. Install missing system tools manually.");
        if (!bundled && git.isEmpty()) status.setText(status.text() + "\nGit is required for source checkouts.");
        if (bundled && !sources) status.setText(status.text() + "\nExtract the complete BuzzPrepare ZIP before running it.");
        if (existingDist) status.setText(status.text() + "\nMove the existing dist folder to prepare another copy.");
        pythonInstall->setEnabled(supported && !winget.isEmpty() && !basePython);
        gitInstall->setVisible(windowsHost && !bundled);
        submodules->setVisible(!bundled);
        gitInstall->setEnabled(supported && !winget.isEmpty() && git.isEmpty());
        vsInstall->setText(vsInstaller.isEmpty() ? "Install Visual Studio C++ Build Tools and Windows SDK"
                                                 : "Open Visual Studio Installer for C++ tools and Windows SDK");
        vsInstall->setEnabled(supported && (!winget.isEmpty() || !vsInstaller.isEmpty()) && !(compiler && sdk));
        submodules->setEnabled(supported && gitCheckout && !sources && !git.isEmpty());
        setup->setEnabled(supported && sources && basePython && !powershell.isEmpty());
        prepare->setEnabled(supported && (bundled || (gitCheckout && !git.isEmpty())) && sources &&
                            (tools || setup->isEnabled()) && compiler && sdk && cpu && vulkan && !existingDist &&
                            controlFiles.isValid() && !powershell.isEmpty() &&
                            QFileInfo::exists(game->text()) && QFileInfo::exists(firmware->text()));
        cancel->setEnabled(false);
        play->setEnabled(supported && checkout && QFileInfo::exists(gameExe()));
    }

    void start(const QString& program, const QStringList& args) {
        if (busy) return;
        busy = true;
        for (auto* control : findChildren<QPushButton*>()) control->setEnabled(false);
        for (auto* field : {project, game, firmware, keys, update, resume}) field->setEnabled(false);
        upscaler.setEnabled(false);
        jobs.setEnabled(false);
        process.setWorkingDirectory(QFileInfo(root()).isDir() ? root() : QDir::homePath());
        process.start(program, args);
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
            else
                log.appendPlainText("Preparation could not start. Check the dependencies and input paths above.");
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
        start(toolPython(), args);
        cancel->setEnabled(busy);
    }

    void install(const QString& id, const QStringList& extra = {}) {
        start(winget, QStringList{"install", "--exact", "--source", "winget", "--id", id} + extra);
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
        setWindowTitle("Buzz! Quiz World — Prepare");
        resize(830, 760);
        supported = (windowsHost || linuxHost) && QSysInfo::currentCpuArchitecture() == "x86_64";
        status.setWordWrap(true);
        form.addRow(&status);
        project = pathRow("Build sources", {}, true);
        project->setText(findCheckout());
        check = button("Recheck dependencies", [this] { refresh(); });
        pythonInstall = button("Install Python with WinGet", [this] { install("Python.Python.3.12"); });
        gitInstall = button("Install Git with WinGet", [this] { install("Git.Git"); });
        vsInstall = button("Install Visual Studio C++ Build Tools and Windows SDK", [this] {
            if (vsInstaller.isEmpty())
                install("Microsoft.VisualStudio.2022.BuildTools",
                        {"--override", "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"});
            else if (!QProcess::startDetached(vsInstaller, {}))
                log.appendPlainText("Could not open Visual Studio Installer.");
        });
        linuxInstall = button("Install Linux system dependencies", [this] {
            const auto pkexec = QStandardPaths::findExecutable("pkexec", {"/usr/bin", "/bin"});
            auto shellArgs = packageArgs;
            shellArgs.replaceInStrings("pkgconfig(sdl2)", "'pkgconfig(sdl2)'");
            const QString command = QStringList{"sudo", packageManager}.join(' ') + ' ' + shellArgs.join(' ');
            if (pkexec.isEmpty()) {
                QApplication::clipboard()->setText(command);
                log.appendPlainText("Run the copied command in a terminal, then recheck:\n" + command);
            } else {
                log.appendPlainText("If the graphical installer is unavailable, run in a terminal:\n" + command);
                start(pkexec, QStringList{packageManager} + packageArgs);
            }
        });
        for (auto* control : {pythonInstall, gitInstall, vsInstall}) control->setVisible(windowsHost);
        linuxInstall->setVisible(linuxHost);
        submodules = button("Download source submodules", [this] {
            start(git, {"submodule", "update", "--init", "--recursive"});
        });
        setup = button("Install or repair local build dependencies", [this] { startSetup(); });
        game = pathRow("Game ISO or dump", "Disc images (*.iso)", true);
        firmware = pathRow("Firmware PUP or dev_flash", "PS3 firmware (*.pup)", true);
        keys = pathRow("Local keys (if encrypted)", "All files (*)", false);
        const auto defaultKeys = root() + "/runtime/keys/ps3.keys";
        if (!root().isEmpty() && QFileInfo(defaultKeys).isFile()) keys->setText(defaultKeys);
        update = pathRow("Game update (optional)", "PS3 updates (*.pkg *.bin *.elf)", false);
        resume = pathRow("Resume existing session (optional)", {}, true);
        jobs.setRange(1, 64);
        jobs.setValue(6);
        form.addRow("Parallel build jobs", &jobs);
        form.addRow(&upscaler);
        prepare = button("Prepare dist", [this] {
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
        cancel = button("Cancel preparation", [this] {
            QFile marker(cancelFile());
            if (marker.open(QIODevice::WriteOnly)) {
                cancel->setEnabled(false);
                log.appendPlainText("Cancellation requested. Waiting for preparation to stop.");
            } else {
                log.appendPlainText("Could not request cancellation.");
            }
        });
        play = button("Play from dist", [this] {
            if (!QProcess::startDetached(gameExe(), {}, root() + "/dist"))
                log.appendPlainText("Could not start the prepared game.");
        });
        log.setReadOnly(true);
        log.setMaximumBlockCount(3000);
        form.addRow(&log);
        recheck.setSingleShot(true);
        recheck.setInterval(250);
        connect(&recheck, &QTimer::timeout, this, [this] { refresh(); });
        for (auto* field : {project, game, firmware})
            connect(field, &QLineEdit::textChanged, this, [this] { recheck.start(); });
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
    Wizard wizard;
    wizard.show();
    return app.exec();
}
