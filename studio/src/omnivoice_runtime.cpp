#include "omnivoice_runtime.h"
#include <QDir>
#include <QFile>
#include <QOperatingSystemVersion>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QSettings>
#ifdef Q_OS_WIN
#include <dxgi.h>
#endif

namespace studio::omnivoice {
QString backendId(Backend backend) {
    switch (backend) {
    case Backend::Cpu: return "cpu";
    case Backend::Cuda: return "cuda";
    case Backend::Rocm: return "rocm";
    }
    return {};
}

QStringList torchPackages(Backend backend) {
#ifdef Q_OS_WIN
    if (backend == Backend::Rocm) {
        // AMD's Windows ROCm 7.2.1 wheels come with their matching user-space SDK.
        const QString base = "https://repo.radeon.com/rocm/windows/rocm-rel-7.2.1/";
        return {base + "rocm_sdk_core-7.2.1-py3-none-win_amd64.whl",
                base + "rocm_sdk_devel-7.2.1-py3-none-win_amd64.whl",
                base + "rocm_sdk_libraries_custom-7.2.1-py3-none-win_amd64.whl",
                base + "rocm-7.2.1.tar.gz",
                base + "torch-2.9.1%2Brocm7.2.1-cp312-cp312-win_amd64.whl",
                base + "torchaudio-2.9.1%2Brocm7.2.1-cp312-cp312-win_amd64.whl"};
    }
#endif
    const QString wheel = backend == Backend::Rocm ? "rocm6.4" : backend == Backend::Cuda ? "cu128" : "cpu";
    return {"--torch-backend", wheel, "torch==2.8.0+" + wheel, "torchaudio==2.8.0+" + wheel};
}

bool supportsWindowsRadeon(const QString &adapter, const QString &processor) {
    // Match the models in AMD's Windows ROCm 7.2.1 Radeon and Ryzen matrices.
    static const QRegularExpression discrete(
        R"(\b(?:RX\s+(?:9070(?:\s+XT)?|9060\s+XT|7900\s+XTX|7700)|(?:AI\s+)?PRO\s+(?:R9700|W7900(?:\s+Dual\s+Slot)?))(?:\s+Graphics)?\s*$)",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression integrated(
        R"(\bRyzen\s+AI\s+(?:Max\+?\s+(?:395|390|385)|9\s+(?:HX\s+(?:375|370|475|470)|365|465))\b)",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression integratedAdapter(
        R"(\bRadeon(?:\(TM\)|™)?\s+(?:(?:80[456]0S|890M|880M)(?:\s+Graphics)?|Graphics)\s*$)",
        QRegularExpression::CaseInsensitiveOption);
    return discrete.match(adapter).hasMatch() ||
           (integratedAdapter.match(adapter).hasMatch() && integrated.match(processor).hasMatch());
}

bool supportsLinuxRadeon(const QByteArray &properties) {
    // KFD reports gfx_target_version as major * 10000 + minor * 100 + stepping.
    // Targets compiled into the PyTorch 2.8.0 Linux ROCm wheel, in the same format.
    static const QSet<int> supported{90000, 90006, 90008, 90010, 90402, 100300,
                                     110000, 110001, 110002, 120000, 120001};
    int arch = 0, vendor = 0;
    for (const auto &line : properties.split('\n')) {
        if (line.startsWith("gfx_target_version "))
            arch = line.mid(sizeof("gfx_target_version ") - 1).toInt();
        else if (line.startsWith("vendor_id "))
            vendor = line.mid(sizeof("vendor_id ") - 1).toInt();
    }
    return vendor == 0x1002 && supported.contains(arch);
}

bool hasRocmGpu() {
#ifdef Q_OS_WIN
    if (QOperatingSystemVersion::current() < QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 22000))
        return false;
    IDXGIFactory1 *factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return false;
    const auto release = qScopeGuard([factory] { factory->Release(); });
    const QSettings cpu("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", QSettings::NativeFormat);
    const auto processor = cpu.value("ProcessorNameString").toString();
    for (UINT index = 0;; ++index) {
        IDXGIAdapter1 *adapter = nullptr;
        if (FAILED(factory->EnumAdapters1(index, &adapter)))
            return false;
        DXGI_ADAPTER_DESC1 info{};
        const auto result = adapter->GetDesc1(&info);
        adapter->Release();
        if (SUCCEEDED(result) && info.VendorId == 0x1002 && !(info.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
            supportsWindowsRadeon(QString::fromWCharArray(info.Description), processor))
            return true;
    }
#elif defined(Q_OS_LINUX)
    QFile kfd("/dev/kfd");
    if (!kfd.open(QIODevice::ReadWrite | QIODevice::Unbuffered | QIODevice::ExistingOnly))
        return false;
    const QDir nodes("/sys/class/kfd/kfd/topology/nodes");
    for (const auto &node : nodes.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile properties(nodes.filePath(node + "/properties"));
        if (properties.open(QIODevice::ReadOnly) && supportsLinuxRadeon(properties.read(8192)))
            return true;
    }
#endif
    return false;
}
} // namespace studio::omnivoice
