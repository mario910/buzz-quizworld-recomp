#pragma once
#include <QDialog>
#include <memory>

class QAudioOutput;
class QLabel;
class QMediaPlayer;
class QProcess;
class QPushButton;
class QSlider;
class QTemporaryDir;

class MediaPreviewDialog final : public QDialog {
    Q_OBJECT
  public:
    static bool needsConversion(const QString &path);
    MediaPreviewDialog(const QString &path, const QString &title, bool video, const QString &ffmpeg, QWidget *parent);
    ~MediaPreviewDialog() override;

  private:
    QString path_, ffmpeg_;
    bool video_, converting_ = false;
    std::unique_ptr<QTemporaryDir> temporary_;
    QMediaPlayer *player_ = nullptr;
    QProcess *process_ = nullptr;
    QLabel *status_ = nullptr, *time_ = nullptr;
    QSlider *seek_ = nullptr;
    QPushButton *play_ = nullptr;

    void playFile(const QString &path);
    void preparePreview();
    void showFailure();
    void updatePosition();
};
