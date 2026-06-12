#!/usr/bin/env python3
"""ImageTool - 图像取模工具。"""

from __future__ import annotations

import os
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional

# Encourage Qt to use desktop OpenGL on Windows where possible.
os.environ.setdefault("QT_OPENGL", "desktop")

import numpy as np
from PIL import Image
from PyQt6.QtCore import QCoreApplication, QObject, QThread, Qt, pyqtSignal, pyqtSlot
from PyQt6.QtGui import QColor, QImage, QPainter, QPen, QPixmap
from PyQt6.QtWidgets import (
    QApplication,
    QFileDialog,
    QFrame,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QProgressBar,
    QPushButton,
    QRadioButton,
    QSlider,
    QSpinBox,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)


SUPPORTED_IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".bmp"}


@dataclass
class BatchResult:
    index: int
    source_path: str
    file_name: str
    result_text: str
    gray_pixels: np.ndarray
    original_width: int
    original_height: int
    padded_width: int
    padded_height: int
    bytes_count: int


class BatchNode:
    def __init__(self, data: BatchResult):
        self.data = data
        self.prev: Optional[BatchNode] = None
        self.next: Optional[BatchNode] = None


class DoublyLinkedResultList:
    def __init__(self):
        self.head: Optional[BatchNode] = None
        self.tail: Optional[BatchNode] = None
        self.length = 0

    def clear(self):
        self.head = None
        self.tail = None
        self.length = 0

    def append(self, data: BatchResult):
        node = BatchNode(data)
        if self.tail is None:
            self.head = node
            self.tail = node
        else:
            self.tail.next = node
            node.prev = self.tail
            self.tail = node
        self.length += 1
        return node

    def get(self, index: int) -> Optional[BatchResult]:
        if index < 1 or index > self.length:
            return None

        current = self.head
        current_index = 1
        while current is not None and current_index < index:
            current = current.next
            current_index += 1
        return current.data if current is not None else None

    def iter_results(self) -> Iterable[BatchResult]:
        current = self.head
        while current is not None:
            yield current.data
            current = current.next


def sanitize_identifier(text: str) -> str:
    cleaned = "".join(char if char.isalnum() else "_" for char in text).strip("_")
    if not cleaned:
        cleaned = "image"
    if cleaned[0].isdigit():
        cleaned = f"_{cleaned}"
    return cleaned


def compute_padded_size(width: int, height: int) -> tuple[int, int]:
    return ((width + 7) // 8) * 8, ((height + 7) // 8) * 8


def read_grayscale_image(file_path: str) -> tuple[np.ndarray, int, int]:
    image = Image.open(file_path).convert("L")
    gray_pixels = np.array(image, dtype=np.uint8).reshape(-1)
    return gray_pixels, image.width, image.height


def build_binary_matrix(gray_pixels: np.ndarray, image_width: int, image_height: int, threshold: int) -> tuple[int, int, np.ndarray]:
    padded_width, padded_height = compute_padded_size(image_width, image_height)
    if padded_width <= 0 or padded_height <= 0:
        return 0, 0, np.zeros((0, 0), dtype=np.uint8)

    # Vectorized thresholding avoids Python-level pixel loops.
    gray_2d = gray_pixels.reshape((image_height, image_width))
    binary = np.zeros((padded_height, padded_width), dtype=np.uint8)
    binary[:image_height, :image_width] = (gray_2d >= threshold).astype(np.uint8)
    return padded_width, padded_height, binary


def pack_binary_matrix(binary_matrix: np.ndarray, horizontal: bool, msb_first: bool) -> bytearray:
    if binary_matrix.size == 0:
        return bytearray()

    # np.packbits handles the byte packing in native C code, which is much
    # faster than a nested Python loop.
    work_matrix = binary_matrix if horizontal else binary_matrix.T
    bitorder = "big" if msb_first else "little"
    packed = np.packbits(work_matrix, axis=1, bitorder=bitorder)
    return bytearray(packed.reshape(-1).tolist())


def format_bytes_per_line(bytes_array: bytearray) -> str:
    lines = []
    for index in range(0, len(bytes_array), 16):
        line_end = min(index + 16, len(bytes_array))
        line_bytes = ", ".join(f"0x{value:02x}" for value in bytes_array[index:line_end])
        line = f"    /* 0x{index:04x} */ {line_bytes}"
        if line_end < len(bytes_array):
            line += ","
        lines.append(line)
    return "\n".join(lines)


def build_result_text(
    bytes_array: bytearray,
    padded_width: int,
    padded_height: int,
    array_name: str,
    width_macro: str,
    height_macro: str,
    bytes_macro: str,
    source_name: Optional[str] = None,
) -> str:
    lines = []
    if source_name:
        lines.append(f"/* Source: {source_name} */")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append(f"#define {width_macro} {padded_width}")
    lines.append(f"#define {height_macro} {padded_height}")
    lines.append(f"#define {bytes_macro} {len(bytes_array)}")
    lines.append("")
    lines.append(f"const uint8_t {array_name}[] = {{")
    formatted = format_bytes_per_line(bytes_array)
    if formatted:
        lines.append(formatted)
    lines.append("};")
    return "\n".join(lines)


def validate_image_folder(folder_path: str) -> tuple[Optional[str], list[Path]]:
    folder = Path(folder_path)
    if not folder.exists() or not folder.is_dir():
        return "Selected path is not a folder.", []

    entries = sorted(folder.iterdir(), key=lambda item: item.name.lower())
    if not entries:
        return "Folder is empty.", []

    image_files: list[Path] = []
    image_suffix = None
    for entry in entries:
        if not entry.is_file():
            return f"Folder contains a non-file entry: {entry.name}", []
        suffix = entry.suffix.lower()
        if suffix not in SUPPORTED_IMAGE_SUFFIXES:
            return f"Folder contains unsupported file: {entry.name}", []
        if image_suffix is None:
            image_suffix = suffix
        elif suffix != image_suffix:
            return "Folder contains mixed image file types. Please keep only one type in the folder.", []
        image_files.append(entry)

    return None, image_files


class PreviewWidget(QFrame):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(360, 360)
        self.setFrameShape(QFrame.Shape.StyledPanel)
        self.setFrameShadow(QFrame.Shadow.Sunken)
        self.setStyleSheet("background-color: #f8f8f8;")
        self._width = 0
        self._height = 0
        self._data: list[int] = []

    def set_binary_image(self, width: int, height: int, data: list[int]):
        self._width = width
        self._height = height
        self._data = data
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor(248, 248, 248))

        if self._width <= 0 or self._height <= 0 or not self._data:
            painter.setPen(QColor(120, 120, 120))
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "No preview")
            return

        margin = 12
        rect = self.rect().adjusted(margin, margin, -margin, -margin)
        cell_width = rect.width() / self._width
        cell_height = rect.height() / self._height
        cell_size = max(1.0, min(cell_width, cell_height))

        draw_width = int(cell_size * self._width)
        draw_height = int(cell_size * self._height)
        start_x = rect.x() + (rect.width() - draw_width) // 2
        start_y = rect.y() + (rect.height() - draw_height) // 2

        painter.setPen(QPen(QColor(225, 225, 225), 1))
        for y in range(self._height):
            for x in range(self._width):
                index = y * self._width + x
                on = index < len(self._data) and self._data[index] != 0
                cell_rect = (
                    int(start_x + x * cell_size),
                    int(start_y + y * cell_size),
                    int(cell_size),
                    int(cell_size),
                )
                painter.fillRect(*cell_rect, QColor(0, 0, 0) if on else QColor(255, 255, 255))
                painter.drawRect(*cell_rect)


class BatchWorker(QObject):
    progress = pyqtSignal(int, int, str)
    finished = pyqtSignal(object)
    failed = pyqtSignal(str)

    def __init__(self, folder_path: str, threshold: int, horizontal: bool, msb_first: bool):
        super().__init__()
        self.folder_path = folder_path
        self.threshold = threshold
        self.horizontal = horizontal
        self.msb_first = msb_first

    @pyqtSlot()
    def run(self):
        try:
            error_text, image_files = validate_image_folder(self.folder_path)
            if error_text:
                raise ValueError(error_text)

            results = DoublyLinkedResultList()
            total = len(image_files)
            for index, file_path in enumerate(image_files, start=1):
                gray_pixels, width, height = read_grayscale_image(str(file_path))
                padded_width, padded_height, binary = build_binary_matrix(
                    gray_pixels, width, height, self.threshold
                )
                bytes_array = pack_binary_matrix(binary, self.horizontal, self.msb_first)
                result_text = build_result_text(
                    bytes_array,
                    padded_width,
                    padded_height,
                    array_name=f"bitmap_{index:03d}_{sanitize_identifier(file_path.stem)}",
                    width_macro=f"IMG_WIDTH_{index:03d}",
                    height_macro=f"IMG_HEIGHT_{index:03d}",
                    bytes_macro=f"IMG_BYTES_{index:03d}",
                    source_name=file_path.name,
                )
                results.append(
                    BatchResult(
                        index=index,
                        source_path=str(file_path),
                        file_name=file_path.name,
                        result_text=result_text,
                        gray_pixels=gray_pixels,
                        original_width=width,
                        original_height=height,
                        padded_width=padded_width,
                        padded_height=padded_height,
                        bytes_count=len(bytes_array),
                    )
                )
                self.progress.emit(index, total, file_path.name)

            self.finished.emit(results)
        except Exception as exc:
            self.failed.emit(str(exc))


class ImageToolWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("ImageTool - Bitmap Generator")
        self.setGeometry(100, 100, 1360, 780)

        self.current_file_path = ""
        self.current_gray_pixels: Optional[np.ndarray] = None
        self.current_image_width = 0
        self.current_image_height = 0
        self.current_binary_pixels: Optional[np.ndarray] = None
        self.current_generated_width = 0
        self.current_generated_height = 0

        self.batch_results = DoublyLinkedResultList()
        self.batch_mode = False
        self.batch_folder = ""
        self.batch_processing = False
        self._batch_thread: Optional[QThread] = None
        self._batch_worker: Optional[BatchWorker] = None

        self.build_ui()

    def build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)

        main_layout = QHBoxLayout(central)
        main_layout.setContentsMargins(16, 16, 16, 16)
        main_layout.setSpacing(16)

        left_panel = QWidget()
        left_layout = QVBoxLayout(left_panel)
        left_layout.setSpacing(12)

        top_button_row = QHBoxLayout()
        open_image_button = QPushButton("Open Image")
        open_image_button.setMinimumHeight(36)
        open_image_button.clicked.connect(self.open_image)
        open_folder_button = QPushButton("Open Folder")
        open_folder_button.setMinimumHeight(36)
        open_folder_button.clicked.connect(self.open_folder)
        top_button_row.addWidget(open_image_button)
        top_button_row.addWidget(open_folder_button)
        left_layout.addLayout(top_button_row)

        self.source_info_label = QLabel("No image loaded")
        self.source_info_label.setWordWrap(True)
        self.source_info_label.setStyleSheet("color:#666;")
        left_layout.addWidget(self.source_info_label)

        self.original_image_label = QLabel()
        self.original_image_label.setMinimumSize(420, 420)
        self.original_image_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.original_image_label.setFrameShape(QFrame.Shape.StyledPanel)
        self.original_image_label.setText("Original image preview")
        self.original_image_label.setStyleSheet("background:#fafafa; color:#666;")
        left_layout.addWidget(self.original_image_label, 1)

        right_panel = QWidget()
        right_layout = QVBoxLayout(right_panel)
        right_layout.setSpacing(12)

        threshold_row = QHBoxLayout()
        threshold_label = QLabel("Threshold")
        self.threshold_slider = QSlider(Qt.Orientation.Horizontal)
        self.threshold_slider.setRange(0, 255)
        self.threshold_slider.setValue(128)
        self.threshold_slider.valueChanged.connect(self.on_threshold_changed)
        self.threshold_value_label = QLabel("128")
        self.threshold_value_label.setMinimumWidth(48)
        self.threshold_value_label.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        threshold_row.addWidget(threshold_label)
        threshold_row.addWidget(self.threshold_slider, 1)
        threshold_row.addWidget(self.threshold_value_label)
        right_layout.addLayout(threshold_row)

        option_grid = QGridLayout()
        option_grid.setHorizontalSpacing(24)
        option_grid.setVerticalSpacing(10)

        scan_group = QGroupBox("Scan Direction")
        scan_layout = QVBoxLayout(scan_group)
        self.horizontal_radio = QRadioButton("Horizontal")
        self.vertical_radio = QRadioButton("Vertical")
        self.horizontal_radio.setChecked(True)
        self.horizontal_radio.toggled.connect(self.on_render_option_changed)
        self.vertical_radio.toggled.connect(self.on_render_option_changed)
        scan_layout.addWidget(self.horizontal_radio)
        scan_layout.addWidget(self.vertical_radio)

        bit_group = QGroupBox("Bit Order")
        bit_layout = QVBoxLayout(bit_group)
        self.msb_radio = QRadioButton("MSB first")
        self.lsb_radio = QRadioButton("LSB first")
        self.msb_radio.setChecked(True)
        self.msb_radio.toggled.connect(self.on_render_option_changed)
        self.lsb_radio.toggled.connect(self.on_render_option_changed)
        bit_layout.addWidget(self.msb_radio)
        bit_layout.addWidget(self.lsb_radio)

        option_grid.addWidget(scan_group, 0, 0)
        option_grid.addWidget(bit_group, 0, 1)
        right_layout.addLayout(option_grid)

        self.preview_widget = PreviewWidget()
        self.preview_widget.setMinimumHeight(340)
        right_layout.addWidget(self.preview_widget, 1)

        batch_group = QGroupBox("Batch Processing")
        batch_layout = QVBoxLayout(batch_group)

        self.batch_status_label = QLabel("Batch: not loaded")
        self.batch_status_label.setWordWrap(True)
        batch_layout.addWidget(self.batch_status_label)

        self.batch_progress = QProgressBar()
        self.batch_progress.setRange(0, 100)
        self.batch_progress.setValue(0)
        self.batch_progress.setVisible(False)
        batch_layout.addWidget(self.batch_progress)

        batch_select_row = QHBoxLayout()
        batch_select_row.addWidget(QLabel("Sequence"))
        self.sequence_spin = QSpinBox()
        self.sequence_spin.setMinimum(1)
        self.sequence_spin.setMaximum(1)
        self.sequence_spin.setEnabled(False)
        self.sequence_spin.valueChanged.connect(self.on_batch_sequence_changed)
        self.batch_file_label = QLabel("-")
        self.batch_file_label.setWordWrap(True)
        batch_select_row.addWidget(self.sequence_spin)
        batch_select_row.addWidget(self.batch_file_label, 1)
        batch_layout.addLayout(batch_select_row)

        delimiter_row = QHBoxLayout()
        delimiter_row.addWidget(QLabel("Delimiter"))
        self.delimiter_edit = QLineEdit("\n\n-----\n\n")
        self.delimiter_edit.setPlaceholderText("Custom delimiter between results")
        delimiter_row.addWidget(self.delimiter_edit, 1)
        batch_layout.addLayout(delimiter_row)

        self.export_txt_button = QPushButton("Export TXT")
        self.export_txt_button.clicked.connect(self.export_batch_txt)
        batch_layout.addWidget(self.export_txt_button)

        right_layout.addWidget(batch_group)

        action_row = QHBoxLayout()
        self.generate_button = QPushButton("Generate")
        self.copy_button = QPushButton("Copy")
        self.save_button = QPushButton("Save as .h")
        self.generate_button.clicked.connect(self.generate_bitmap)
        self.copy_button.clicked.connect(self.copy_bitmap)
        self.save_button.clicked.connect(self.save_bitmap_header)
        action_row.addWidget(self.generate_button)
        action_row.addWidget(self.copy_button)
        action_row.addWidget(self.save_button)
        right_layout.addLayout(action_row)

        self.output_edit = QTextEdit()
        self.output_edit.setPlaceholderText("Generated C array will appear here.")
        self.output_edit.setReadOnly(True)
        self.output_edit.setLineWrapMode(QTextEdit.LineWrapMode.NoWrap)
        self.output_edit.setFontFamily("Consolas")
        right_layout.addWidget(self.output_edit, 1)

        main_layout.addWidget(left_panel, 1)
        main_layout.addWidget(right_panel, 1)

    def set_processing_state(self, busy: bool, message: str = ""):
        self.batch_processing = busy
        self.batch_progress.setVisible(busy)
        self.batch_progress.setValue(0 if busy else 100)
        if message:
            self.batch_status_label.setText(message)

        self.generate_button.setEnabled(not busy)
        self.copy_button.setEnabled(not busy)
        self.save_button.setEnabled(not busy)
        self.export_txt_button.setEnabled(not busy and self.batch_mode)
        self.sequence_spin.setEnabled(not busy and self.batch_mode and self.batch_results.length > 0)

    def open_image(self):
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            "Open Image",
            "",
            "Images (*.png *.jpg *.jpeg *.bmp)",
        )
        if file_path:
            self.batch_mode = False
            self.batch_folder = ""
            self.batch_results.clear()
            self.sequence_spin.blockSignals(True)
            self.sequence_spin.setMaximum(1)
            self.sequence_spin.setValue(1)
            self.sequence_spin.setEnabled(False)
            self.sequence_spin.blockSignals(False)
            self.batch_status_label.setText("Batch: not loaded")
            self.batch_file_label.setText("-")
            self.batch_progress.setVisible(False)
            self.load_single_image(file_path)

    def open_folder(self):
        folder_path = QFileDialog.getExistingDirectory(self, "Open Folder", "")
        if not folder_path:
            return

        error_text, image_files = validate_image_folder(folder_path)
        if error_text:
            QMessageBox.warning(self, "Invalid Folder", error_text)
            return

        if self.batch_processing:
            QMessageBox.information(self, "Busy", "Batch processing is still running. Please wait.")
            return

        self.start_batch_processing(folder_path, image_files)

    def start_batch_processing(self, folder_path: str, image_files: Optional[list[Path]] = None):
        # Keep the UI responsive by pushing the expensive folder work into a worker thread.
        if image_files is None:
            error_text, image_files = validate_image_folder(folder_path)
            if error_text:
                QMessageBox.warning(self, "Invalid Folder", error_text)
                return

        self.batch_mode = True
        self.batch_folder = folder_path
        self.batch_results.clear()
        self.set_processing_state(True, f"Batch: processing {len(image_files)} images...")

        scan_horizontal = self.horizontal_radio.isChecked()
        msb_first = self.msb_radio.isChecked()
        threshold = self.threshold_slider.value()

        self._batch_thread = QThread(self)
        self._batch_worker = BatchWorker(folder_path, threshold, scan_horizontal, msb_first)
        self._batch_worker.moveToThread(self._batch_thread)
        self._batch_thread.started.connect(self._batch_worker.run)
        self._batch_worker.progress.connect(self.on_batch_progress)
        self._batch_worker.finished.connect(self.on_batch_finished)
        self._batch_worker.failed.connect(self.on_batch_failed)
        self._batch_worker.finished.connect(self._batch_thread.quit)
        self._batch_worker.failed.connect(self._batch_thread.quit)
        self._batch_thread.finished.connect(self._cleanup_batch_thread)
        self._batch_thread.start()

    def _cleanup_batch_thread(self):
        self._batch_worker = None
        self._batch_thread = None
        self.batch_processing = False

    def on_batch_progress(self, current: int, total: int, file_name: str):
        percent = int((current / total) * 100) if total else 0
        self.batch_progress.setVisible(True)
        self.batch_progress.setValue(percent)
        self.batch_status_label.setText(f"Batch: {current}/{total} {file_name}")

    def on_batch_finished(self, results_obj):
        self.batch_results = results_obj
        self.batch_mode = True
        self.set_processing_state(False, f"Batch: {self.batch_results.length} images loaded from {Path(self.batch_folder).name}")

        self.sequence_spin.blockSignals(True)
        self.sequence_spin.setMaximum(max(1, self.batch_results.length))
        self.sequence_spin.setValue(1)
        self.sequence_spin.blockSignals(False)
        self.sequence_spin.setEnabled(self.batch_results.length > 0)

        if self.batch_results.length > 0:
            self.select_batch_result(1)
        else:
            self.output_edit.clear()

    def on_batch_failed(self, message: str):
        self.set_processing_state(False, "Batch: failed")
        QMessageBox.critical(self, "Batch Failed", message)

    def load_single_image(self, file_path: str):
        try:
            gray_pixels, width, height = read_grayscale_image(file_path)
        except Exception as exc:
            QMessageBox.critical(self, "Load Failed", f"Unable to load image: {exc}")
            return

        self.current_file_path = file_path
        self.current_gray_pixels = gray_pixels
        self.current_image_width = width
        self.current_image_height = height

        self.source_info_label.setText(f"Image: {Path(file_path).name}")
        self.render_original_image(gray_pixels, width, height)
        self.update_binary_and_preview()

    def render_original_image(self, gray_pixels: np.ndarray, width: int, height: int):
        q_image = QImage(gray_pixels.tobytes(), width, height, width, QImage.Format.Format_Grayscale8)
        pixmap = QPixmap.fromImage(q_image.copy()).scaled(
            self.original_image_label.size(),
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation,
        )
        self.original_image_label.setPixmap(pixmap)

    def on_threshold_changed(self, value: int):
        self.threshold_value_label.setText(str(value))
        if self.batch_mode and self.batch_folder and not self.batch_processing:
            self.start_batch_processing(self.batch_folder)
        else:
            self.update_binary_and_preview()

    def on_render_option_changed(self):
        if self.batch_mode and self.batch_folder and not self.batch_processing:
            self.start_batch_processing(self.batch_folder)
        else:
            self.update_binary_and_preview()

    def on_batch_sequence_changed(self, value: int):
        if self.batch_mode and not self.batch_processing:
            self.select_batch_result(value)

    def select_batch_result(self, sequence: int):
        result = self.batch_results.get(sequence)
        if result is None:
            return

        self.sequence_spin.blockSignals(True)
        self.sequence_spin.setValue(sequence)
        self.sequence_spin.blockSignals(False)

        self.batch_file_label.setText(result.file_name)
        self.source_info_label.setText(f"Batch item {result.index}/{self.batch_results.length}: {result.file_name}")
        self.current_file_path = result.source_path
        self.current_image_width = result.original_width
        self.current_image_height = result.original_height

        self.current_gray_pixels = result.gray_pixels
        padded_width, padded_height, binary = build_binary_matrix(
            self.current_gray_pixels,
            self.current_image_width,
            self.current_image_height,
            self.threshold_slider.value(),
        )
        self.current_binary_pixels = binary
        self.current_generated_width = padded_width
        self.current_generated_height = padded_height
        self.preview_widget.set_binary_image(padded_width, padded_height, binary.reshape(-1).tolist())
        self.output_edit.setPlainText(result.result_text)

    def build_current_result(self) -> str:
        if self.current_gray_pixels is None or self.current_image_width <= 0 or self.current_image_height <= 0:
            return ""

        padded_width, padded_height, binary = build_binary_matrix(
            self.current_gray_pixels,
            self.current_image_width,
            self.current_image_height,
            self.threshold_slider.value(),
        )
        self.current_binary_pixels = binary
        self.current_generated_width = padded_width
        self.current_generated_height = padded_height

        bytes_array = pack_binary_matrix(binary, self.horizontal_radio.isChecked(), self.msb_radio.isChecked())
        return build_result_text(
            bytes_array,
            padded_width,
            padded_height,
            array_name="bitmap",
            width_macro="IMG_WIDTH",
            height_macro="IMG_HEIGHT",
            bytes_macro="IMG_BYTES",
            source_name=Path(self.current_file_path).name if self.current_file_path else None,
        )

    def update_binary_and_preview(self):
        if self.batch_mode:
            if self.batch_results.length > 0 and not self.batch_processing:
                self.select_batch_result(self.sequence_spin.value())
            return

        if self.current_gray_pixels is None or self.current_image_width <= 0 or self.current_image_height <= 0:
            self.current_binary_pixels = None
            self.current_generated_width = 0
            self.current_generated_height = 0
            self.preview_widget.set_binary_image(0, 0, [])
            self.output_edit.clear()
            return

        padded_width, padded_height, binary = build_binary_matrix(
            self.current_gray_pixels,
            self.current_image_width,
            self.current_image_height,
            self.threshold_slider.value(),
        )
        self.current_binary_pixels = binary
        self.current_generated_width = padded_width
        self.current_generated_height = padded_height
        self.preview_widget.set_binary_image(padded_width, padded_height, binary.reshape(-1).tolist())

    def generate_bitmap(self):
        if self.batch_mode:
            self.select_batch_result(self.sequence_spin.value())
            return

        if self.current_gray_pixels is None:
            QMessageBox.information(self, "No Image", "Please open an image first.")
            return

        result_text = self.build_current_result()
        self.output_edit.setPlainText(result_text)
        if self.current_binary_pixels is not None:
            self.preview_widget.set_binary_image(
                self.current_generated_width,
                self.current_generated_height,
                self.current_binary_pixels.reshape(-1).tolist(),
            )

    def copy_bitmap(self):
        if not self.output_edit.toPlainText():
            if self.batch_mode:
                self.select_batch_result(self.sequence_spin.value())
            else:
                self.generate_bitmap()

        text = self.output_edit.toPlainText()
        if not text:
            return

        QApplication.clipboard().setText(text)
        if self.batch_mode:
            QMessageBox.information(self, "Copied", f"Copied sequence {self.sequence_spin.value()} to clipboard.")
        else:
            QMessageBox.information(self, "Copied", "C array copied to clipboard.")

    def save_bitmap_header(self):
        if not self.output_edit.toPlainText():
            if self.batch_mode:
                self.select_batch_result(self.sequence_spin.value())
            else:
                self.generate_bitmap()

        text = self.output_edit.toPlainText()
        if not text:
            return

        default_name = "bitmap.h"
        if self.batch_mode:
            current = self.batch_results.get(self.sequence_spin.value())
            if current is not None:
                default_name = f"{Path(current.file_name).stem}.h"

        file_path, _ = QFileDialog.getSaveFileName(
            self,
            "Save as .h",
            default_name,
            "Header Files (*.h)",
        )
        if not file_path:
            return

        try:
            with open(file_path, "w", encoding="utf-8") as file_object:
                file_object.write(text)
            QMessageBox.information(self, "Saved", f"Header file saved to {file_path}")
        except Exception as exc:
            QMessageBox.critical(self, "Save Failed", f"Unable to save file: {exc}")

    def export_batch_txt(self):
        if not self.batch_mode or self.batch_results.length == 0:
            QMessageBox.information(self, "No Batch", "Please open a valid folder first.")
            return

        delimiter = self.delimiter_edit.text()
        payload = delimiter.join(result.result_text for result in self.batch_results.iter_results())

        file_path, _ = QFileDialog.getSaveFileName(
            self,
            "Export TXT",
            "batch_results.txt",
            "Text Files (*.txt)",
        )
        if not file_path:
            return

        try:
            with open(file_path, "w", encoding="utf-8") as file_object:
                file_object.write(payload)
            QMessageBox.information(self, "Saved", f"Batch TXT exported to {file_path}")
        except Exception as exc:
            QMessageBox.critical(self, "Export Failed", f"Unable to export TXT: {exc}")


def main():
    # Prefer desktop OpenGL when Qt creates its GUI surfaces.
    QCoreApplication.setAttribute(Qt.ApplicationAttribute.AA_UseDesktopOpenGL)
    app = QApplication(sys.argv)
    window = ImageToolWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()