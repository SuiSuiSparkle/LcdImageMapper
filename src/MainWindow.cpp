#include "MainWindow.h"

#include "stb_image.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPushButton>
#include <QRadioButton>
#include <QSizePolicy>
#include <QSlider>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
QString toHexByte(quint8 value)
{
    return QStringLiteral("0x%1").arg(static_cast<int>(value), 2, 16, QLatin1Char('0')).toUpper();
}
}

class MainWindow::PreviewWidget : public QFrame
{
public:
    explicit PreviewWidget(QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setMinimumSize(360, 360);
        setFrameShape(QFrame::StyledPanel);
        setFrameShadow(QFrame::Sunken);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setBinaryImage(int width, int height, const QVector<quint8> &data)
    {
        width_ = width;
        height_ = height;
        data_ = data;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.fillRect(rect(), QColor(248, 248, 248));

        if (width_ <= 0 || height_ <= 0 || data_.isEmpty()) {
            painter.setPen(QColor(120, 120, 120));
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No preview"));
            return;
        }

        const int margin = 12;
        const QRect contentRect = rect().adjusted(margin, margin, -margin, -margin);
        const double cellWidth = static_cast<double>(contentRect.width()) / width_;
        const double cellHeight = static_cast<double>(contentRect.height()) / height_;
        const double cellSize = std::max(1.0, std::floor(std::min(cellWidth, cellHeight)));

        const int drawWidth = static_cast<int>(cellSize * width_);
        const int drawHeight = static_cast<int>(cellSize * height_);
        const int startX = contentRect.x() + (contentRect.width() - drawWidth) / 2;
        const int startY = contentRect.y() + (contentRect.height() - drawHeight) / 2;

        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(QPen(QColor(225, 225, 225), 1));

        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const int index = y * width_ + x;
                const bool on = index >= 0 && index < data_.size() && data_.at(index) != 0;
                const QRectF cellRect(startX + x * cellSize, startY + y * cellSize, cellSize, cellSize);
                painter.fillRect(cellRect, on ? Qt::black : Qt::white);
                painter.drawRect(cellRect);
            }
        }
    }

private:
    int width_ = 0;
    int height_ = 0;
    QVector<quint8> data_;
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();
    updateThresholdLabel();
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(16);

    auto *leftPanel = new QWidget(central);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setSpacing(12);

    auto *openButton = new QPushButton(QStringLiteral("Open Image"), leftPanel);
    openButton->setMinimumHeight(36);
    leftLayout->addWidget(openButton);

    auto *openFolderBtn = new QPushButton(QStringLiteral("Open Folder"), leftPanel);
    openFolderBtn->setMinimumHeight(36);
    leftLayout->addWidget(openFolderBtn);
    openFolderButton_ = openFolderBtn;

    originalImageLabel_ = new QLabel(leftPanel);
    originalImageLabel_->setMinimumSize(420, 420);
    originalImageLabel_->setAlignment(Qt::AlignCenter);
    originalImageLabel_->setFrameShape(QFrame::StyledPanel);
    originalImageLabel_->setText(QStringLiteral("Original image preview"));
    originalImageLabel_->setStyleSheet(QStringLiteral("background:#fafafa; color:#666;"));
    leftLayout->addWidget(originalImageLabel_, 1);

    // --- Navigation bar: Prev | Play | Next ---
    auto *navRow = new QHBoxLayout();
    navRow->setSpacing(8);

    prevButton_ = new QPushButton(QStringLiteral("◀ Prev"), leftPanel);
    prevButton_->setMinimumHeight(36);
    prevButton_->setEnabled(false);

    playButton_ = new QPushButton(QStringLiteral("▶ Play"), leftPanel);
    playButton_->setMinimumHeight(36);
    playButton_->setEnabled(false);

    nextButton_ = new QPushButton(QStringLiteral("Next ▶"), leftPanel);
    nextButton_->setMinimumHeight(36);
    nextButton_->setEnabled(false);

    navRow->addWidget(prevButton_);
    navRow->addWidget(playButton_);
    navRow->addWidget(nextButton_);
    leftLayout->addLayout(navRow);

    auto *rightPanel = new QWidget(central);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setSpacing(12);

    auto *thresholdRow = new QHBoxLayout();
    auto *thresholdLabel = new QLabel(QStringLiteral("Threshold"), rightPanel);
    thresholdSlider_ = new QSlider(Qt::Horizontal, rightPanel);
    thresholdSlider_->setRange(0, 255);
    thresholdSlider_->setValue(128);
    thresholdValueLabel_ = new QLabel(QStringLiteral("128"), rightPanel);
    thresholdValueLabel_->setMinimumWidth(48);
    thresholdValueLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    thresholdRow->addWidget(thresholdLabel);
    thresholdRow->addWidget(thresholdSlider_, 1);
    thresholdRow->addWidget(thresholdValueLabel_);
    rightLayout->addLayout(thresholdRow);

    auto *optionGrid = new QGridLayout();
    optionGrid->setHorizontalSpacing(24);
    optionGrid->setVerticalSpacing(10);

    auto *scanGroup = new QGroupBox(QStringLiteral("Scan Direction"), rightPanel);
    auto *scanLayout = new QVBoxLayout(scanGroup);
    horizontalRadio_ = new QRadioButton(QStringLiteral("Horizontal"), scanGroup);
    verticalRadio_ = new QRadioButton(QStringLiteral("Vertical"), scanGroup);
    horizontalRadio_->setChecked(true);
    scanLayout->addWidget(horizontalRadio_);
    scanLayout->addWidget(verticalRadio_);

    auto *bitGroup = new QGroupBox(QStringLiteral("Bit Order"), rightPanel);
    auto *bitLayout = new QVBoxLayout(bitGroup);
    msbRadio_ = new QRadioButton(QStringLiteral("MSB first"), bitGroup);
    lsbRadio_ = new QRadioButton(QStringLiteral("LSB first"), bitGroup);
    msbRadio_->setChecked(true);
    bitLayout->addWidget(msbRadio_);
    bitLayout->addWidget(lsbRadio_);

    optionGrid->addWidget(scanGroup, 0, 0);
    optionGrid->addWidget(bitGroup, 0, 1);
    rightLayout->addLayout(optionGrid);

    previewWidget_ = new PreviewWidget(rightPanel);
    previewWidget_->setMinimumHeight(340);
    rightLayout->addWidget(previewWidget_, 1);

    auto *actionRow = new QHBoxLayout();
    generateButton_ = new QPushButton(QStringLiteral("Generate"), rightPanel);
    copyButton_ = new QPushButton(QStringLiteral("Copy"), rightPanel);
    saveButton_ = new QPushButton(QStringLiteral("Save as .h"), rightPanel);
    exportAllButton_ = new QPushButton(QStringLiteral("Export All as .h"), rightPanel);
    exportAllButton_->setMinimumHeight(36);
    actionRow->addWidget(generateButton_);
    actionRow->addWidget(copyButton_);
    actionRow->addWidget(saveButton_);
    actionRow->addWidget(exportAllButton_);
    rightLayout->addLayout(actionRow);

    outputEdit_ = new QTextEdit(rightPanel);
    outputEdit_->setPlaceholderText(QStringLiteral("Generated C array will appear here."));
    outputEdit_->setFontFamily(QStringLiteral("Consolas"));
    outputEdit_->setLineWrapMode(QTextEdit::NoWrap);
    rightLayout->addWidget(outputEdit_, 1);

    mainLayout->addWidget(leftPanel, 1);
    mainLayout->addWidget(rightPanel, 1);
    setCentralWidget(central);

    connect(openButton, &QPushButton::clicked, this, &MainWindow::openImage);
    connect(openFolderBtn, &QPushButton::clicked, this, &MainWindow::openFolder);
    connect(prevButton_, &QPushButton::clicked, this, &MainWindow::navigatePrevious);
    connect(playButton_, &QPushButton::clicked, this, &MainWindow::togglePlayback);
    connect(nextButton_, &QPushButton::clicked, this, &MainWindow::navigateNext);
    connect(thresholdSlider_, &QSlider::valueChanged, this, &MainWindow::onThresholdChanged);
    connect(generateButton_, &QPushButton::clicked, this, &MainWindow::generateBitmap);
    connect(copyButton_, &QPushButton::clicked, this, &MainWindow::copyBitmap);
    connect(saveButton_, &QPushButton::clicked, this, &MainWindow::saveBitmapHeader);
    connect(exportAllButton_, &QPushButton::clicked, this, &MainWindow::exportAllBitmapsToHeader);
    connect(horizontalRadio_, &QRadioButton::toggled, this, &MainWindow::updateBinaryAndPreview);
    connect(verticalRadio_, &QRadioButton::toggled, this, &MainWindow::updateBinaryAndPreview);
    connect(msbRadio_, &QRadioButton::toggled, this, &MainWindow::updateBinaryAndPreview);
    connect(lsbRadio_, &QRadioButton::toggled, this, &MainWindow::updateBinaryAndPreview);

    setWindowTitle(QStringLiteral("ImageTool - Bitmap Generator"));
}

void MainWindow::openImage()
{
    const QString filter = QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp)");
    const QString filePath = QFileDialog::getOpenFileName(this, QStringLiteral("Open Image"), QString(), filter);
    if (filePath.isEmpty()) {
        return;
    }

    loadImage(filePath);
}

void MainWindow::loadImage(const QString &filePath)
{
    int width = 0;
    int height = 0;
    int channels = 0;

    stbi_uc *rawPixels = stbi_load(filePath.toUtf8().constData(), &width, &height, &channels, 1);
    if (!rawPixels) {
        QMessageBox::critical(this, QStringLiteral("Load Failed"), QStringLiteral("Unable to load image with stbi_load."));
        return;
    }

    currentFilePath_ = filePath;
    imageWidth_ = width;
    imageHeight_ = height;

    grayscalePixels_.resize(width * height);
    std::copy(rawPixels, rawPixels + width * height, grayscalePixels_.begin());
    stbi_image_free(rawPixels);

    QImage image(grayscalePixels_.constData(), imageWidth_, imageHeight_, imageWidth_, QImage::Format_Grayscale8);
    originalImageLabel_->setPixmap(QPixmap::fromImage(image.copy()).scaled(
        originalImageLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    updateBinaryAndPreview();
}

void MainWindow::openFolder()
{
    const QString dirPath = QFileDialog::getExistingDirectory(this, QStringLiteral("Open Folder"), QString());
    if (dirPath.isEmpty()) {
        return;
    }

    QDir dir(dirPath);
    QStringList filters;
    filters << QStringLiteral("*.png") << QStringLiteral("*.jpg") << QStringLiteral("*.jpeg") << QStringLiteral("*.bmp");
    dir.setNameFilters(filters);
    dir.setSorting(QDir::Name);

    imageFiles_.clear();
    const QFileInfoList entries = dir.entryInfoList(QDir::Files);
    for (const QFileInfo &info : entries) {
        imageFiles_.append(info.absoluteFilePath());
    }

    if (imageFiles_.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("No Images"),
            QStringLiteral("No supported image files found in the selected folder."));
        prevButton_->setEnabled(false);
        playButton_->setEnabled(false);
        nextButton_->setEnabled(false);
        return;
    }

    // Stop any current playback
    if (playbackTimer_ && playbackTimer_->isActive()) {
        playbackTimer_->stop();
        playButton_->setText(QStringLiteral("▶ Play"));
    }

    currentImageIndex_ = 0;
    loadImage(imageFiles_.at(currentImageIndex_));

    prevButton_->setEnabled(true);
    playButton_->setEnabled(true);
    nextButton_->setEnabled(true);
    updateNavButtonStates();
}

void MainWindow::togglePlayback()
{
    if (imageFiles_.isEmpty() || imageFiles_.size() < 2) {
        return;
    }

    if (!playbackTimer_) {
        playbackTimer_ = new QTimer(this);
        playbackTimer_->setInterval(1500);
        connect(playbackTimer_, &QTimer::timeout, this, &MainWindow::navigateNext);
    }

    if (playbackTimer_->isActive()) {
        playbackTimer_->stop();
        playButton_->setText(QStringLiteral("▶ Play"));
    } else {
        playbackTimer_->start();
        playButton_->setText(QStringLiteral("⏸ Pause"));
    }
}

void MainWindow::navigatePrevious()
{
    if (imageFiles_.isEmpty()) {
        return;
    }

    // Stop playback on manual navigation
    if (playbackTimer_ && playbackTimer_->isActive()) {
        playbackTimer_->stop();
        playButton_->setText(QStringLiteral("▶ Play"));
    }

    if (currentImageIndex_ > 0) {
        currentImageIndex_--;
        loadImage(imageFiles_.at(currentImageIndex_));
    }
    updateNavButtonStates();
}

void MainWindow::navigateNext()
{
    if (imageFiles_.isEmpty()) {
        return;
    }

    // Stop playback only on manual button click, not timer-triggered
    const bool manualClick = (sender() == nextButton_);

    int newIndex = currentImageIndex_;
    if (currentImageIndex_ < imageFiles_.size() - 1) {
        newIndex = currentImageIndex_ + 1;
    } else {
        // Loop back to first image
        newIndex = 0;
    }

    if (manualClick && playbackTimer_ && playbackTimer_->isActive()) {
        playbackTimer_->stop();
        playButton_->setText(QStringLiteral("▶ Play"));
    }

    if (newIndex != currentImageIndex_) {
        currentImageIndex_ = newIndex;
        loadImage(imageFiles_.at(currentImageIndex_));
    }
    updateNavButtonStates();
}

void MainWindow::updateNavButtonStates()
{
    const bool hasFiles = !imageFiles_.isEmpty();
    prevButton_->setEnabled(hasFiles && currentImageIndex_ > 0);
    nextButton_->setEnabled(hasFiles && imageFiles_.size() > 1);
}

void MainWindow::onThresholdChanged(int value)
{
    thresholdValueLabel_->setText(QString::number(value));
    updateBinaryAndPreview();
}

void MainWindow::updateThresholdLabel()
{
    thresholdValueLabel_->setText(QString::number(thresholdSlider_->value()));
}

int MainWindow::paddedWidth() const
{
    if (imageWidth_ <= 0) {
        return 0;
    }
    return ((imageWidth_ + 7) / 8) * 8;
}

int MainWindow::paddedHeight() const
{
    if (imageHeight_ <= 0) {
        return 0;
    }
    return ((imageHeight_ + 7) / 8) * 8;
}

int MainWindow::imageIndex(int x, int y, int width) const
{
    return y * width + x;
}

QVector<quint8> MainWindow::buildBinaryImage(int &paddedWidthOut, int &paddedHeightOut) const
{
    QVector<quint8> binary;
    paddedWidthOut = paddedWidth();
    paddedHeightOut = paddedHeight();

    if (paddedWidthOut <= 0 || paddedHeightOut <= 0) {
        return binary;
    }

    binary.resize(paddedWidthOut * paddedHeightOut);
    binary.fill(0);

    const int threshold = thresholdSlider_->value();
    for (int y = 0; y < imageHeight_; ++y) {
        for (int x = 0; x < imageWidth_; ++x) {
            const int sourceIndex = y * imageWidth_ + x;
            const bool on = grayscalePixels_.at(sourceIndex) >= threshold;
            binary[imageIndex(x, y, paddedWidthOut)] = on ? 1 : 0;
        }
    }

    return binary;
}

QByteArray MainWindow::buildPackedBytes(int paddedWidthValue, int paddedHeightValue, const QVector<quint8> &binary) const
{
    QByteArray bytes;
    if (paddedWidthValue <= 0 || paddedHeightValue <= 0 || binary.isEmpty()) {
        return bytes;
    }

    const ScanMode scanMode = horizontalRadio_->isChecked() ? ScanMode::Horizontal : ScanMode::Vertical;
    const BitOrder bitOrder = msbRadio_->isChecked() ? BitOrder::MsbFirst : BitOrder::LsbFirst;

    const int totalBytes = scanMode == ScanMode::Horizontal
        ? (paddedWidthValue / 8) * paddedHeightValue
        : (paddedHeightValue / 8) * paddedWidthValue;
    bytes.resize(totalBytes);
    bytes.fill(0);

    int outputIndex = 0;
    if (scanMode == ScanMode::Horizontal) {
        for (int y = 0; y < paddedHeightValue; ++y) {
            for (int blockX = 0; blockX < paddedWidthValue; blockX += 8) {
                quint8 packedByte = 0;
                for (int bit = 0; bit < 8; ++bit) {
                    const int x = blockX + bit;
                    if (binary.at(imageIndex(x, y, paddedWidthValue)) == 0) {
                        continue;
                    }
                    const int bitIndex = bitOrder == BitOrder::MsbFirst ? (7 - bit) : bit;
                    packedByte |= static_cast<quint8>(1u << bitIndex);
                }
                bytes[outputIndex++] = static_cast<char>(packedByte);
            }
        }
    } else {
        for (int x = 0; x < paddedWidthValue; ++x) {
            for (int blockY = 0; blockY < paddedHeightValue; blockY += 8) {
                quint8 packedByte = 0;
                for (int bit = 0; bit < 8; ++bit) {
                    const int y = blockY + bit;
                    if (binary.at(imageIndex(x, y, paddedWidthValue)) == 0) {
                        continue;
                    }
                    const int bitIndex = bitOrder == BitOrder::MsbFirst ? (7 - bit) : bit;
                    packedByte |= static_cast<quint8>(1u << bitIndex);
                }
                bytes[outputIndex++] = static_cast<char>(packedByte);
            }
        }
    }

    return bytes;
}

QString MainWindow::formatBytesPerLine(const QByteArray &bytes)
{
    QString result;
    for (int index = 0; index < bytes.size(); index += 16) {
        if (!result.isEmpty()) {
            result += QStringLiteral("\n");
        }

        result += QStringLiteral("    /* 0x%1 */ ").arg(index, 4, 16, QLatin1Char('0')).toUpper();
        const int lineEnd = (index + 16 < bytes.size()) ? (index + 16) : bytes.size();
        for (int byteIndex = index; byteIndex < lineEnd; ++byteIndex) {
            result += toHexByte(static_cast<quint8>(bytes.at(byteIndex)));
            if (byteIndex + 1 != lineEnd) {
                result += QStringLiteral(", ");
            }
        }

        if (lineEnd != bytes.size()) {
            result += QStringLiteral(",");
        }
    }
    return result;
}

QString MainWindow::buildArrayText(const QByteArray &bytes, int paddedWidthValue, int paddedHeightValue) const
{
    QString text;
    text += QStringLiteral("#define IMG_WIDTH %1\n").arg(paddedWidthValue);
    text += QStringLiteral("#define IMG_HEIGHT %1\n").arg(paddedHeightValue);
    text += QStringLiteral("#define IMG_BYTES %1\n\n").arg(bytes.size());
    text += QStringLiteral("const uint8_t bitmap[] = {\n");
    text += formatBytesPerLine(bytes);
    text += QStringLiteral("\n};\n");
    return text;
}

QString MainWindow::buildHeaderText(const QByteArray &bytes, int paddedWidthValue, int paddedHeightValue) const
{
    QString header;
    header += QStringLiteral("#pragma once\n\n");
    header += QStringLiteral("#define IMG_WIDTH %1\n").arg(paddedWidthValue);
    header += QStringLiteral("#define IMG_HEIGHT %1\n").arg(paddedHeightValue);
    header += QStringLiteral("#define IMG_BYTES %1\n\n").arg(bytes.size());
    header += QStringLiteral("const uint8_t bitmap[] = {\n");
    header += formatBytesPerLine(bytes);
    header += QStringLiteral("\n};\n");
    return header;
}

void MainWindow::updateBinaryAndPreview()
{
    if (grayscalePixels_.isEmpty() || imageWidth_ <= 0 || imageHeight_ <= 0) {
        binaryPixels_.clear();
        generatedWidth_ = 0;
        generatedHeight_ = 0;
        previewWidget_->setBinaryImage(0, 0, {});
        return;
    }

    binaryPixels_ = buildBinaryImage(generatedWidth_, generatedHeight_);
    previewWidget_->setBinaryImage(generatedWidth_, generatedHeight_, binaryPixels_);

    QImage image(grayscalePixels_.constData(), imageWidth_, imageHeight_, imageWidth_, QImage::Format_Grayscale8);
    originalImageLabel_->setPixmap(QPixmap::fromImage(image.copy()).scaled(
        originalImageLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::generateBitmap()
{
    if (binaryPixels_.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("No Image"), QStringLiteral("Please open an image first."));
        return;
    }

    const QByteArray bytes = buildPackedBytes(generatedWidth_, generatedHeight_, binaryPixels_);
    outputEdit_->setPlainText(buildArrayText(bytes, generatedWidth_, generatedHeight_));
}

void MainWindow::copyBitmap()
{
    if (outputEdit_->toPlainText().isEmpty()) {
        generateBitmap();
    }

    if (outputEdit_->toPlainText().isEmpty()) {
        return;
    }

    QApplication::clipboard()->setText(outputEdit_->toPlainText());
}

void MainWindow::saveBitmapHeader()
{
    if (binaryPixels_.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("No Image"), QStringLiteral("Please open an image first."));
        return;
    }

    const QByteArray bytes = buildPackedBytes(generatedWidth_, generatedHeight_, binaryPixels_);
    const QString headerText = buildHeaderText(bytes, generatedWidth_, generatedHeight_);

    const QString defaultName = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/bitmap.h");
    const QString filePath = QFileDialog::getSaveFileName(this, QStringLiteral("Save as .h"), defaultName, QStringLiteral("Header Files (*.h)"));
    if (filePath.isEmpty()) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, QStringLiteral("Save Failed"), QStringLiteral("Unable to write the selected file."));
        return;
    }

    file.write(headerText.toUtf8());
    file.close();
}

namespace
{
QString sanitizeIdentifier(const QString &name)
{
    QString result = name;
    // Replace non-alphanumeric characters (except underscore) with underscore
    static const QRegularExpression invalidChars(QStringLiteral("[^a-zA-Z0-9_]"));
    result.replace(invalidChars, QStringLiteral("_"));
    // If it starts with a digit, prepend an underscore
    if (!result.isEmpty() && result.at(0).isDigit()) {
        result.prepend(QLatin1Char('_'));
    }
    return result;
}
}

void MainWindow::exportAllBitmapsToHeader()
{
    // Must have a folder loaded with images
    if (imageFiles_.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("No Folder"),
            QStringLiteral("Please open a folder of images first using [Open Folder]."));
        return;
    }

    // Ask where to save the combined header
    const QString defaultName = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        + QStringLiteral("/all_bitmaps.h");
    const QString filePath = QFileDialog::getSaveFileName(this,
        QStringLiteral("Export All Bitmaps as Header"),
        defaultName,
        QStringLiteral("Header Files (*.h)"));
    if (filePath.isEmpty()) {
        return;
    }

    // Use a separate stbi_load call for each file so we don't disturb the currently displayed image.
    // Save current state so we can restore it afterward.
    const QString savedFilePath = currentFilePath_;
    const int savedIndex = currentImageIndex_;
    const QVector<quint8> savedGrayscale = grayscalePixels_;
    const QVector<quint8> savedBinary = binaryPixels_;
    const int savedW = imageWidth_;
    const int savedH = imageHeight_;
    const int savedGW = generatedWidth_;
    const int savedGH = generatedHeight_;

    // Collect the header content in memory
    QString output;
    output += QStringLiteral("#pragma once\n\n");
    output += QStringLiteral("#include <stdint.h>\n\n");
    output += QStringLiteral("// ============================================================\n");
    output += QStringLiteral("//  Auto-generated bitmap header from ImageTool\n");
    output += QStringLiteral("//  Contains %1 image(s)\n").arg(imageFiles_.size());
    output += QStringLiteral("//  Generated on: %1\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    output += QStringLiteral("// ============================================================\n\n");

    const int totalFiles = imageFiles_.size();
    int succeeded = 0;
    int failed = 0;
    QStringList errors;

    for (int i = 0; i < totalFiles; ++i) {
        const QString &imgPath = imageFiles_.at(i);
        const QFileInfo fi(imgPath);
        const QString baseName = fi.completeBaseName();
        const QString safeName = sanitizeIdentifier(baseName);

        // Load the image
        int w = 0, h = 0, ch = 0;
        stbi_uc *raw = stbi_load(imgPath.toUtf8().constData(), &w, &h, &ch, 1);
        if (!raw) {
            errors << QStringLiteral("  [%1] Failed to load: %2").arg(i + 1).arg(imgPath);
            ++failed;
            continue;
        }

        // Build binary image
        const int pw = ((w + 7) / 8) * 8;
        const int ph = ((h + 7) / 8) * 8;
        QVector<quint8> gray(w * h);
        std::copy(raw, raw + w * h, gray.begin());
        stbi_image_free(raw);

        const int threshold = thresholdSlider_->value();
        QVector<quint8> binary(pw * ph, 0);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const bool on = gray.at(y * w + x) >= threshold;
                binary[y * pw + x] = on ? 1 : 0;
            }
        }

        // Pack bytes (reuse buildPackedBytes logic inline to avoid modifying internal state)
        const ScanMode scanMode = horizontalRadio_->isChecked() ? ScanMode::Horizontal : ScanMode::Vertical;
        const BitOrder bitOrder = msbRadio_->isChecked() ? BitOrder::MsbFirst : BitOrder::LsbFirst;

        const int totalBytes = scanMode == ScanMode::Horizontal
            ? (pw / 8) * ph
            : (ph / 8) * pw;
        QByteArray packed(totalBytes, 0);

        int outIdx = 0;
        if (scanMode == ScanMode::Horizontal) {
            for (int y = 0; y < ph; ++y) {
                for (int bx = 0; bx < pw; bx += 8) {
                    quint8 byteVal = 0;
                    for (int bit = 0; bit < 8; ++bit) {
                        const int x = bx + bit;
                        if (binary.at(y * pw + x) == 0) continue;
                        const int bitIdx = bitOrder == BitOrder::MsbFirst ? (7 - bit) : bit;
                        byteVal |= static_cast<quint8>(1u << bitIdx);
                    }
                    packed[outIdx++] = static_cast<char>(byteVal);
                }
            }
        } else {
            for (int x = 0; x < pw; ++x) {
                for (int by = 0; by < ph; by += 8) {
                    quint8 byteVal = 0;
                    for (int bit = 0; bit < 8; ++bit) {
                        const int y = by + bit;
                        if (binary.at(y * pw + x) == 0) continue;
                        const int bitIdx = bitOrder == BitOrder::MsbFirst ? (7 - bit) : bit;
                        byteVal |= static_cast<quint8>(1u << bitIdx);
                    }
                    packed[outIdx++] = static_cast<char>(byteVal);
                }
            }
        }

        // Emit bitmap data for this image
        output += QStringLiteral("// ---- %1 ----\n").arg(baseName);
        output += QStringLiteral("#define %1_WIDTH  %2\n").arg(safeName).arg(pw);
        output += QStringLiteral("#define %1_HEIGHT %2\n").arg(safeName).arg(ph);
        output += QStringLiteral("#define %1_BYTES  %2\n\n").arg(safeName).arg(packed.size());
        output += QStringLiteral("static const uint8_t %1_bitmap[] = {\n").arg(safeName);
        output += formatBytesPerLine(packed);
        output += QStringLiteral("\n};\n\n");

        ++succeeded;
    }

    // Restore original state
    currentFilePath_ = savedFilePath;
    currentImageIndex_ = savedIndex;
    grayscalePixels_ = savedGrayscale;
    binaryPixels_ = savedBinary;
    imageWidth_ = savedW;
    imageHeight_ = savedH;
    generatedWidth_ = savedGW;
    generatedHeight_ = savedGH;

    // Summary
    output += QStringLiteral("// ============================================================\n");
    output += QStringLiteral("//  Summary: %1 succeeded, %2 failed\n").arg(succeeded).arg(failed);
    output += QStringLiteral("// ============================================================\n");

    // Write file
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, QStringLiteral("Export Failed"),
            QStringLiteral("Unable to write the selected file."));
        return;
    }
    file.write(output.toUtf8());
    file.close();

    // Show result
    QString msg = QStringLiteral("Export complete!\n\n  Succeeded: %1\n  Failed: %2\n\nSaved to:\n  %3")
        .arg(succeeded).arg(failed).arg(filePath);
    if (!errors.isEmpty()) {
        msg += QStringLiteral("\n\nErrors:\n") + errors.join(QStringLiteral("\n"));
    }
    QMessageBox::information(this, QStringLiteral("Export Complete"), msg);

    // Also show the full output in the text edit for inspection
    outputEdit_->setPlainText(output);
}
