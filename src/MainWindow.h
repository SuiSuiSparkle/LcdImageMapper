#pragma once

#define QT_DISABLE_NOEXCEPT_FOR_EQUALITY

#include <QByteArray>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QVector>

class QLabel;
class QPushButton;
class QRadioButton;
class QSlider;
class QTextEdit;
class QTimer;
class QWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void openImage();
    void openFolder();
    void togglePlayback();
    void navigatePrevious();
    void navigateNext();
    void onThresholdChanged(int value);
    void generateBitmap();
    void copyBitmap();
    void saveBitmapHeader();

private:
    enum class ScanMode
    {
        Horizontal,
        Vertical
    };

    enum class BitOrder
    {
        MsbFirst,
        LsbFirst
    };

    class PreviewWidget;

    void buildUi();
    void loadImage(const QString &filePath);
    void updateThresholdLabel();
    void updateNavButtonStates();
    void updateBinaryAndPreview();
    QVector<quint8> buildBinaryImage(int &paddedWidth, int &paddedHeight) const;
    QByteArray buildPackedBytes(int paddedWidth, int paddedHeight, const QVector<quint8> &binary) const;
    QString buildArrayText(const QByteArray &bytes, int paddedWidth, int paddedHeight) const;
    QString buildHeaderText(const QByteArray &bytes, int paddedWidth, int paddedHeight) const;
    static QString formatBytesPerLine(const QByteArray &bytes);
    int paddedWidth() const;
    int paddedHeight() const;
    int imageIndex(int x, int y, int width) const;

    QLabel *originalImageLabel_ = nullptr;
    PreviewWidget *previewWidget_ = nullptr;
    QSlider *thresholdSlider_ = nullptr;
    QLabel *thresholdValueLabel_ = nullptr;
    QRadioButton *horizontalRadio_ = nullptr;
    QRadioButton *verticalRadio_ = nullptr;
    QRadioButton *msbRadio_ = nullptr;
    QRadioButton *lsbRadio_ = nullptr;
    QPushButton *openFolderButton_ = nullptr;
    QPushButton *generateButton_ = nullptr;
    QPushButton *copyButton_ = nullptr;
    QPushButton *saveButton_ = nullptr;
    QPushButton *playButton_ = nullptr;
    QPushButton *prevButton_ = nullptr;
    QPushButton *nextButton_ = nullptr;
    QTimer *playbackTimer_ = nullptr;
    QTextEdit *outputEdit_ = nullptr;

    QString currentFilePath_;
    QStringList imageFiles_;
    int currentImageIndex_ = -1;
    QVector<quint8> grayscalePixels_;
    QVector<quint8> binaryPixels_;
    int imageWidth_ = 0;
    int imageHeight_ = 0;
    int generatedWidth_ = 0;
    int generatedHeight_ = 0;
};
