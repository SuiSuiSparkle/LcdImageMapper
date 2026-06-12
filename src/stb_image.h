#pragma once

#include <QImage>
#include <QImageReader>
#include <QString>

#include <cstdlib>
#include <cstring>

using stbi_uc = unsigned char;

inline void stbi_image_free(void *data)
{
    std::free(data);
}

inline stbi_uc *stbi_load(const char *filename, int *x, int *y, int *channels_in_file, int desired_channels)
{
    if (!filename || !x || !y) {
        return nullptr;
    }

    QImageReader reader(QString::fromUtf8(filename));
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        return nullptr;
    }

    if (desired_channels == 1) {
        image = image.convertToFormat(QImage::Format_Grayscale8);
    } else {
        image = image.convertToFormat(QImage::Format_RGBA8888);
        desired_channels = 4;
    }

    *x = image.width();
    *y = image.height();
    if (channels_in_file) {
        *channels_in_file = image.isGrayscale() ? 1 : 4;
    }

    const int bytesPerPixel = desired_channels;
    const int totalBytes = image.width() * image.height() * bytesPerPixel;
    stbi_uc *buffer = static_cast<stbi_uc *>(std::malloc(static_cast<size_t>(totalBytes)));
    if (!buffer) {
        return nullptr;
    }

    if (desired_channels == 1) {
        for (int row = 0; row < image.height(); ++row) {
            const auto *source = reinterpret_cast<const stbi_uc *>(image.constScanLine(row));
            std::memcpy(buffer + row * image.width(), source, static_cast<size_t>(image.width()));
        }
    } else {
        for (int row = 0; row < image.height(); ++row) {
            const auto *source = reinterpret_cast<const stbi_uc *>(image.constScanLine(row));
            std::memcpy(buffer + row * image.width() * 4, source, static_cast<size_t>(image.width() * 4));
        }
    }

    return buffer;
}
