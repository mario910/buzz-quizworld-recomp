#include "media_tools.h"
#include "pack_format.h"
#include <QElapsedTimer>
#include <QProcess>

namespace studio::pack {
void checkCancelled(const std::atomic_bool *cancel) {
    if (cancel && cancel->load())
        fail(QCoreApplication::translate("PackBuilder", "Budowanie anulowane."));
}
void runMedia(const QString &executable, const QStringList &arguments, const std::atomic_bool *cancel,
              const QString &limitedOutput) {
    checkCancelled(cancel);
    if (executable.isEmpty())
        fail(missingMediaFfmpeg());
    QProcess process;
    process.start(executable, arguments);
    if (!process.waitForStarted(5000))
        fail(process.errorString());
    QElapsedTimer timer;
    timer.start();
    QByteArray errors;
    while (!process.waitForFinished(100)) {
        errors = (errors + process.readAllStandardError()).right(8000);
        if ((cancel && cancel->load()) || timer.elapsed() > 180000 ||
            (!limitedOutput.isEmpty() && QFileInfo(limitedOutput).size() > 48000ll * 2 * 600)) {
            process.kill();
            process.waitForFinished(5000);
            checkCancelled(cancel);
            fail(QCoreApplication::translate(
                "PackBuilder", "Silnik mediów przekroczył limit czasu lub długości nagrania (10 minut)."));
        }
    }
    errors = (errors + process.readAllStandardError()).right(8000);
    checkCancelled(cancel);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        fail(QCoreApplication::translate("PackBuilder", "FFmpeg: ") + QString::fromUtf8(errors));
}
} // namespace studio::pack
