#include "media_preview.h"
#include "pack_media.h"
#include <QAudioOutput>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaPlayer>
#include <QProcess>
#include <QPushButton>
#include <QSlider>
#include <QTemporaryDir>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

namespace {
QString timeLabel(qint64 milliseconds) {
    const auto seconds = milliseconds / 1000;
    return QString("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
}

bool MediaPreviewDialog::needsConversion(const QString &path) {
    return path.endsWith(".rsf", Qt::CaseInsensitive) || path.endsWith(".wem", Qt::CaseInsensitive) ||
           path.endsWith(".vag", Qt::CaseInsensitive);
}

MediaPreviewDialog::MediaPreviewDialog(const QString &path, const QString &title, bool video,
                                       const QString &ffmpeg, QWidget *parent)
    : QDialog(parent), path_(path), ffmpeg_(ffmpeg), video_(video) {
    setObjectName("mediaPreview");
    setWindowTitle(title);
    resize(video ? 720 : 420, video ? 475 : 140);
    auto *layout = new QVBoxLayout(this);
    player_ = new QMediaPlayer(this);
    player_->setAudioOutput(new QAudioOutput(player_));
    if (video) {
        auto *screen = new QVideoWidget;
        screen->setMinimumSize(320, 180);
        player_->setVideoOutput(screen);
        layout->addWidget(screen, 1);
    }
    status_ = new QLabel;
    status_->setWordWrap(true);
    status_->hide();
    layout->addWidget(status_);
    auto *controls = new QHBoxLayout;
    play_ = new QPushButton(tr("Odtwórz"));
    play_->setObjectName("previewPlay");
    play_->setEnabled(false);
    controls->addWidget(play_);
    seek_ = new QSlider(Qt::Horizontal);
    seek_->setAccessibleName(tr("Pozycja odtwarzania"));
    seek_->setEnabled(false);
    controls->addWidget(seek_, 1);
    time_ = new QLabel;
    controls->addWidget(time_);
    auto *close = new QPushButton(tr("Zamknij"));
    close->setAutoDefault(false);
    controls->addWidget(close);
    layout->addLayout(controls);
    updatePosition();

    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(this, &QDialog::finished, player_, &QMediaPlayer::stop);
    connect(play_, &QPushButton::clicked, this, [this] {
        if (player_->playbackState() == QMediaPlayer::PlayingState)
            player_->pause();
        else
            player_->play();
    });
    connect(player_, &QMediaPlayer::playbackStateChanged, this, [this](auto state) {
        play_->setText(state == QMediaPlayer::PlayingState ? tr("Pauza") : tr("Odtwórz"));
    });
    connect(player_, &QMediaPlayer::durationChanged, this, &MediaPreviewDialog::updatePosition);
    connect(player_, &QMediaPlayer::positionChanged, this, &MediaPreviewDialog::updatePosition);
    connect(player_, &QMediaPlayer::seekableChanged, seek_, &QSlider::setEnabled);
    connect(seek_, &QSlider::sliderReleased, this, [this] { player_->setPosition(seek_->value() * 1000ll); });
    connect(seek_, &QSlider::actionTriggered, this, [this](int action) {
        if (action != QAbstractSlider::SliderMove)
            player_->setPosition(seek_->sliderPosition() * 1000ll);
    });
    connect(player_, &QMediaPlayer::errorOccurred, this, [this] {
        if (!converting_ && !ffmpeg_.isEmpty() && QFileInfo(path_).isFile())
            preparePreview();
        else
            showFailure();
    });
    if (!QFileInfo(path_).isFile())
        showFailure();
    else if (needsConversion(path_))
        preparePreview();
    else
        playFile(path_);
}

MediaPreviewDialog::~MediaPreviewDialog() {
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->disconnect(this);
        process_->kill();
        process_->waitForFinished(1000);
    }
    delete player_;
}

void MediaPreviewDialog::playFile(const QString &path) {
    status_->hide();
    play_->setEnabled(true);
    player_->setSource(QUrl::fromLocalFile(path));
    if (play_->isEnabled())
        player_->play();
}

void MediaPreviewDialog::preparePreview() {
    converting_ = true;
    if (ffmpeg_.isEmpty()) {
        showFailure();
        return;
    }
    temporary_ = std::make_unique<QTemporaryDir>();
    if (!temporary_->isValid()) {
        showFailure();
        return;
    }
    status_->setText(tr("Przygotowywanie podglądu…"));
    status_->show();
    play_->setEnabled(false);
    auto input = path_;
    if (path_.endsWith(".rsf", Qt::CaseInsensitive) || path_.endsWith(".wem", Qt::CaseInsensitive)) {
        try {
            input = temporary_->filePath("source.wav");
            QFile file(input);
            const auto wave = studio::pack::previewAudioWave(path_);
            if (!file.open(QIODevice::WriteOnly) || file.write(wave) != wave.size()) {
                showFailure();
                return;
            }
        } catch (const std::exception &) {
            showFailure();
            return;
        }
    }
    const auto output = temporary_->filePath(video_ ? "preview.mp4" : "preview.wav");
    QStringList args{"-v", "error", "-nostdin", "-y", "-protocol_whitelist", "file,pipe", "-i", input};
    if (video_)
        args << "-map" << "0:v:0" << "-map" << "0:a:0?" << "-c:v" << "libx264" << "-pix_fmt" << "yuv420p"
             << "-c:a" << "aac";
    else
        args << "-vn" << "-c:a" << "pcm_s16le";
    args << output;
    process_ = new QProcess(this);
    process_->setStandardOutputFile(QProcess::nullDevice());
    process_->setStandardErrorFile(QProcess::nullDevice());
    connect(process_, &QProcess::errorOccurred, this, [this](auto error) {
        if (error == QProcess::FailedToStart)
            showFailure();
    });
    connect(process_, &QProcess::finished, this, [this, output](int code, QProcess::ExitStatus status) {
        if (status == QProcess::NormalExit && code == 0 && QFileInfo(output).size() > 0)
            playFile(output);
        else
            showFailure();
    });
    process_->start(ffmpeg_, args);
}

void MediaPreviewDialog::showFailure() {
    play_->setEnabled(false);
    seek_->setEnabled(false);
    status_->setText(tr("Nie udało się odtworzyć tego pliku."));
    status_->show();
}

void MediaPreviewDialog::updatePosition() {
    const auto position = player_->position();
    const auto duration = player_->duration();
    seek_->setRange(0, int(duration / 1000));
    if (!seek_->isSliderDown())
        seek_->setValue(int(position / 1000));
    time_->setText(timeLabel(position) + " / " + timeLabel(duration));
}
