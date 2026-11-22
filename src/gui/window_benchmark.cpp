#include "window.hpp"

#include "latency.hpp"
#include "window_helpers.hpp"

#include <algorithm>

#include <QColor>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>

void Window::toggle_benchmark() {
  const bool open = !benchmark_body_->isVisible();
  benchmark_body_->setVisible(open);
  benchmark_toggle_->setText(open ? "Бенчмарк ▾" : "Бенчмарк ▸");
}
void Window::start_benchmark() {
  if (running_ || bench_->state() != QProcess::NotRunning) {
    return;
  }
  if (bench_file_->text().isEmpty()) {
    status_->setText("Для порівняння потрібен однаковий запис");
    return;
  }
  const QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/benchmark.json";
  QDir().mkpath(QFileInfo(path).path());
  rtx::Profile profile = profile_from_form();
  profile.input = {};
  profile.input.protocol = "file";
  profile.input.role = "client";
  profile.input.path = rtx::gui::utf8_from_qstring(bench_file_->text().trimmed());
  std::string failure = rtx::password_access_error(profile);
  if (!failure.empty()) {
    status_->setText(QString::fromStdString(failure));
    return;
  }
  if (!rtx::save_profile_file(rtx::gui::path_from_qstring(path), profile, failure)) {
    status_->setText(QString::fromStdString(failure));
    return;
  }
  bench_profile_ = path;
  bench_queue_.clear();
  bench_card_ = {};
  bench_card_uuid_.clear();
  bench_abort_ = false;
  bench_stdout_.clear();
  bench_table_->setRowCount(0);
  QStringList gpus;
  QStringList names;
  if (compare_gpus_->isChecked()) {
    for (const QJsonValue& value : doctor_.value("gpus").toArray()) {
      gpus.push_back(value.toObject().value("uuid").toString());
      names.push_back(value.toObject().value("name").toString());
    }
  }
  if (gpus.isEmpty()) {
    gpus.push_back(gpu_->currentData().toString());
    names.push_back(gpu_->currentText());
  }
  for (int gpu_index = 0; gpu_index < gpus.size(); ++gpu_index) {
    for (const int scale : {2, 3}) {
      for (int quality = 1; quality <= 4; ++quality) {
        BenchJob job;
        job.scale = scale;
        job.quality = quality;
        job.gpu_uuid = gpus.at(gpu_index);
        job.gpu_name = names.at(gpu_index);
        bench_queue_.push_back(job);
      }
    }
  }
  bench_index_ = 0;
  bench_progress_->setRange(0, bench_queue_.size());
  bench_progress_->setValue(0);
  bench_run_->setEnabled(false);
  start_->setEnabled(false);
  run_next_benchmark();
}
void Window::run_next_benchmark() {
  if (bench_index_ >= bench_queue_.size()) {
    flush_benchmark(bench_card_uuid_);
    bench_run_->setEnabled(true);
    start_->setEnabled(true);
    status_->setText(bench_wait_reason_ == "скасовано" ? "Бенчмарк скасовано" : "Бенчмарк завершено");
    bench_wait_reason_.clear();
    return;
  }
  const BenchJob& job = bench_queue_.at(bench_index_);
  QStringList args{"run",
                   "--profile",
                   bench_profile_,
                   "--scale",
                   QString::number(job.scale),
                   "--quality",
                   QString::number(job.quality),
                   "--pace",
                   "--warmup",
                   QString::number(bench_warmup_->value()),
                   "--metrics-interval",
                   "0"};
  if (!job.gpu_uuid.isEmpty()) {
    args << "--gpu-uuid" << job.gpu_uuid;
  }
  status_->setText(QString("Бенчмарк %1/%2").arg(bench_index_ + 1).arg(bench_queue_.size()));
  bench_stdout_.clear();
  bench_stdout_scan_ = 0;
  bench_settled_ = false;
  bench_wait_timer_->stop();
  ++bench_token_;
  bench_->start(proxy_program(), args);
}
void Window::cancel_benchmark() {
  bench_abort_ = true;
  bench_wait_reason_ = "скасовано";
  bench_wait_timer_->stop();
  if (bench_->state() != QProcess::NotRunning) {
    ++bench_token_;
    request_stop(bench_, bench_token_, &bench_token_);
    return;
  }
  bench_index_ = bench_queue_.size();
  bench_run_->setEnabled(true);
  start_->setEnabled(true);
  bench_abort_ = false;
  status_->setText("Бенчмарк скасовано");
}
void Window::read_benchmark() {
  bench_stdout_.append(bench_->readAllStandardOutput());
  // Скануємо лише новий хвіст — без повторного проходу з нуля (O(N²)) і без втрати benchmark-рядків.
  while (bench_stdout_scan_ < bench_stdout_.size()) {
    const int end = bench_stdout_.indexOf('\n', bench_stdout_scan_);
    if (end < 0) {
      break;
    }
    const QJsonObject event =
        QJsonDocument::fromJson(bench_stdout_.mid(bench_stdout_scan_, end - bench_stdout_scan_)).object();
    bench_stdout_scan_ = end + 1;
    if (event.value("type").toString() != "status") {
      continue;
    }
    const QString state = event.value("state").toString();
    if (state == "waiting") {
      if (!bench_wait_timer_->isActive()) {
        bench_wait_timer_->start(bench_wait_->value() * 1000);
      }
    } else if (state == "running" || state == "overloaded" || state == "bypass" || state == "stopped") {
      bench_wait_timer_->stop();
    }
  }
}
void Window::flush_benchmark(const QString& uuid) {
  if (bench_card_.isEmpty()) {
    return;
  }
  QJsonObject document;
  document.insert("gpu_uuid", uuid);
  document.insert("gpu_name", bench_card_.first().toObject().value("gpu_name"));
  document.insert("source", bench_file_->text());
  document.insert("source_bytes", QFileInfo(bench_file_->text()).size());
  document.insert("budget_ms", bench_budget_->value());
  document.insert("warmup_s", bench_warmup_->value());
  document.insert("ffmpeg", bench_card_.first().toObject().value("ffmpeg"));
  document.insert("runs", bench_card_);
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/benchmarks";
  QDir().mkpath(dir);
  const QString safe = uuid.isEmpty() ? QString("no-gpu") : QString(uuid).replace('/', '_');
  QSaveFile file(dir + "/" + safe + ".json");
  if (file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(document).toJson()) >= 0) {
    file.commit();
  }
  bench_card_ = {};
}
void Window::finish_benchmark() {
  if (bench_settled_ || bench_index_ >= bench_queue_.size()) {
    return;
  }
  bench_settled_ = true;
  bench_wait_timer_->stop();
  bench_stdout_.append(bench_->readAllStandardOutput());
  const BenchJob job = bench_queue_.at(bench_index_);
  if (!bench_card_.isEmpty() && bench_card_uuid_ != job.gpu_uuid) {
    flush_benchmark(bench_card_uuid_);
  }
  bench_card_uuid_ = job.gpu_uuid;
  const QByteArray output = bench_stdout_;
  QJsonObject result;
  for (const QByteArray& line : output.split('\n')) {
    const QJsonDocument document = QJsonDocument::fromJson(line);
    if (document.object().value("type").toString() == "benchmark") {
      result = document.object();
    }
  }
  const int row = bench_table_->rowCount();
  bench_table_->insertRow(row);
  auto put = [&](int column, const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setData(Qt::UserRole, job.scale);
    item->setData(Qt::UserRole + 1, job.quality);
    item->setData(Qt::UserRole + 2, job.gpu_uuid);
    bench_table_->setItem(row, column, item);
  };
  put(0, job.gpu_name);
  put(1, QString::number(job.scale));
  put(2, QString::number(job.quality));
  QJsonObject saved;
  saved.insert("scale", job.scale);
  saved.insert("quality", job.quality);
  saved.insert("gpu_name", job.gpu_name);
  saved.insert("gpu_uuid", job.gpu_uuid);
  const bool finished_ok =
      bench_->exitStatus() == QProcess::NormalExit && bench_->exitCode() == 0 && result.value("ok").toBool();
  const auto samples = static_cast<std::uint64_t>(result.value("proxy_samples").toDouble());
  bool applicable = false;
  if (bench_abort_) {
    put(3, bench_wait_reason_.isEmpty() ? QString("скасовано") : bench_wait_reason_);
    saved.insert("error", bench_wait_reason_);
    bench_index_ = bench_queue_.size();
    bench_abort_ = false;
  } else if (!finished_ok) {
    const QString reason = QString::fromUtf8(bench_->readAllStandardError()).trimmed();
    put(3, bench_->exitCode() == 3 ? "непридатний" : "помилка");
    put(4, reason.left(80));
    saved.insert("error", reason);
    saved.insert("exit_code", bench_->exitCode());
    int skip = bench_index_ + 1;
    while (skip < bench_queue_.size() && bench_queue_.at(skip).gpu_uuid == job.gpu_uuid) {
      ++skip;
    }
    bench_index_ = skip;
  } else {
    const double dropped = result.value("frames_dropped").toDouble();
    const double incoming = std::max(1.0, result.value("frames_in").toDouble());
    const double p95 = result.value("proxy_p95_ms").toDouble();
    applicable = rtx::benchmark_run_valid(0, true, samples);
    const bool within = rtx::measurement_within_budget(samples, p95, bench_budget_->value());
    put(3, QString::number(result.value("fps").toDouble(), 'f', 1));
    put(4, QString::number(100.0 * dropped / incoming, 'f', 1) + "%");
    put(5, samples == 0 ? QString("немає виміру") : QString::number(p95, 'f', 1));
    put(6, samples == 0 ? QString("немає виміру") : QString::number(result.value("proxy_p99_ms").toDouble(), 'f', 1));
    put(7, result.value("overload_frames").toDouble() > 0 ? "так" : "ні");
    put(8, result.value("bypass_entries").toDouble() > 0 ? "так" : "ні");
    if (samples > 0 && !within) {
      bench_table_->item(row, 5)->setBackground(QColor(80, 40, 40));
    }
    saved.insert("fps", result.value("fps"));
    saved.insert("frames_in", result.value("frames_in"));
    saved.insert("frames_dropped", result.value("frames_dropped"));
    saved.insert("proxy_p95_ms", result.value("proxy_p95_ms"));
    saved.insert("proxy_p99_ms", result.value("proxy_p99_ms"));
    saved.insert("overload_frames", result.value("overload_frames"));
    saved.insert("bypass_entries", result.value("bypass_entries"));
    saved.insert("proxy_samples", static_cast<double>(samples));
    saved.insert("within_budget", within);
    saved.insert("ffmpeg", result.value("ffmpeg"));
    saved.insert("input_width", result.value("input_width"));
    saved.insert("input_height", result.value("input_height"));
    saved.insert("output_width", result.value("output_width"));
    saved.insert("output_height", result.value("output_height"));
    saved.insert("vsr_scale", result.value("vsr_scale"));
    saved.insert("vsr_quality", result.value("vsr_quality"));
    ++bench_index_;
  }
  saved.insert("valid", applicable);
  if (auto* head = bench_table_->item(row, 0)) {
    head->setData(Qt::UserRole + 3, applicable);
  }
  bench_card_.append(saved);
  bench_progress_->setValue(std::min(bench_index_, static_cast<int>(bench_queue_.size())));
  run_next_benchmark();
}
void Window::apply_benchmark_row() {
  const int row = bench_table_->currentRow();
  if (row < 0 || bench_table_->item(row, 0) == nullptr) {
    return;
  }
  if (!bench_table_->item(row, 0)->data(Qt::UserRole + 3).toBool()) {
    status_->setText("Цей рядок не є успішним виміром");
    return;
  }
  const int scale = bench_table_->item(row, 0)->data(Qt::UserRole).toInt();
  const int quality = bench_table_->item(row, 0)->data(Qt::UserRole + 1).toInt();
  const QString uuid = bench_table_->item(row, 0)->data(Qt::UserRole + 2).toString();
  if (scale < 2) {
    return;
  }
  const int gpu_index = uuid.isEmpty() ? -1 : gpu_->findData(uuid);
  if (!uuid.isEmpty() && gpu_index < 0) {
    unresolved_gpu_ = uuid;
    gpu_->setCurrentIndex(-1);
    gpu_fit_->setText("Збережений GPU відсутній, оберіть інший.");
  } else if (gpu_index >= 0) {
    unresolved_gpu_.clear();
    gpu_->setCurrentIndex(gpu_index);
  }
  const int scale_index = scale_->findData(scale);
  if (scale_index >= 0) {
    scale_->setCurrentIndex(scale_index);
  }
  quality_auto_->setChecked(false);
  const int quality_index = quality_->findData(quality);
  if (quality_index >= 0) {
    quality_->setCurrentIndex(quality_index);
  }
  mark_dirty();
}
