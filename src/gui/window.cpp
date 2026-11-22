#include "window.hpp"

#include "window_helpers.hpp"

#include <algorithm>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>

using rtx::gui::configure_port;
using rtx::gui::list_has;
using rtx::gui::mode_for_protocol;
using rtx::gui::rename_row;
using rtx::gui::show_row;
using rtx::gui::state_text;
using rtx::gui::union_fields;
using rtx::gui::vram_text;

Window::~Window() {
  const auto processes = findChildren<QProcess*>();
  for (QProcess* process : processes) {
    process->disconnect(this);
  }
  ++proxy_token_;
  ++bench_token_;
  stop_process(proxy_);
  stop_process(bench_);
}

void Window::closeEvent(QCloseEvent* event) {
  const bool busy = (proxy_ != nullptr && proxy_->state() != QProcess::NotRunning) ||
                    (bench_ != nullptr && bench_->state() != QProcess::NotRunning);
  if (busy) {
    event->ignore();
    if (closing_) {
      return;
    }
    closing_ = true;
    bench_abort_ = true;
    stopping_ = true;
    ++proxy_token_;
    ++bench_token_;
    if (proxy_ != nullptr && proxy_->state() != QProcess::NotRunning) {
      request_stop(proxy_, proxy_token_, &proxy_token_);
    }
    if (bench_ != nullptr && bench_->state() != QProcess::NotRunning) {
      request_stop(bench_, bench_token_, &bench_token_);
    }
    status_->setText("Завершення сеансу…");
    return;
  }
  closing_ = false;
  QMainWindow::closeEvent(event);
}

void Window::stop_process(QProcess* process) {
  if (process == nullptr || process->state() == QProcess::NotRunning) {
    return;
  }
  process->disconnect(this);
  force_stop_process(process);
}

void Window::force_stop_process(QProcess* process) {
  if (process == nullptr || process->state() == QProcess::NotRunning) {
    return;
  }
  process->write("stop\n");
  if (process->waitForFinished(3000)) {
    return;
  }
  process->terminate();
  if (process->waitForFinished(1000)) {
    return;
  }
  process->kill();
  process->waitForFinished(1000);
}

void Window::request_stop(QProcess* process, quint64 token, quint64* epoch) {
  if (process == nullptr || epoch == nullptr || process->state() == QProcess::NotRunning) {
    return;
  }
  process->setProperty("rtx_stop_generation", QVariant::fromValue(token));
  process->write("stop\n");
  QTimer::singleShot(3000, this, [this, process, token, epoch] {
    if (*epoch != token || process->property("rtx_stop_generation").toULongLong() != token ||
        process->state() == QProcess::NotRunning) {
      return;
    }
    process->terminate();
    QTimer::singleShot(1000, this, [this, process, token, epoch] {
      if (*epoch != token || process->property("rtx_stop_generation").toULongLong() != token ||
          process->state() == QProcess::NotRunning) {
        return;
      }
      process->kill();
    });
  });
}

Window::Window(QWidget* parent) : QMainWindow(parent) {
  build();
  proxy_ = new QProcess(this);
  bench_ = new QProcess(this);
  connect(proxy_, &QProcess::readyReadStandardOutput, this, &Window::read_proxy);
  connect(proxy_, &QProcess::readyReadStandardError, this, [this] {
    journal_->appendPlainText(QString::fromUtf8(proxy_->readAllStandardError()).trimmed());
  });
  connect(proxy_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
    const bool user_stop = stopping_;
    stopping_ = false;
    running_ = false;
    start_->setText("Запустити");
    gpu_->setEnabled(true);
    refresh_gpu_->setEnabled(true);
    bench_run_->setEnabled(true);
    if (closing_) {
      if (bench_ == nullptr || bench_->state() == QProcess::NotRunning) {
        close();
      }
      return;
    }
    if (user_stop) {
      status_->setText("Зупинено");
      return;
    }
    if (status_->text().startsWith("Помилка") || code == 0) {
      return;
    }
    status_->setText(code == 3 ? "Помилка: немає підтвердженого GPU або SDK" : "Помилка запуску");
  });
  connect(proxy_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart) {
      return;
    }
    stopping_ = false;
    running_ = false;
    start_->setText("Запустити");
    gpu_->setEnabled(true);
    refresh_gpu_->setEnabled(true);
    bench_run_->setEnabled(true);
    status_->setText("Не вдалося запустити RTX VSR");
  });
  connect(bench_, &QProcess::readyReadStandardOutput, this, &Window::read_benchmark);
  connect(bench_, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
    if (closing_) {
      if (proxy_ == nullptr || proxy_->state() == QProcess::NotRunning) {
        close();
      }
      return;
    }
    finish_benchmark();
  });
  connect(bench_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart || bench_settled_) {
      return;
    }
    bench_abort_ = true;
    bench_wait_reason_ = "не вдалося запустити";
    status_->setText("Не вдалося запустити бенчмарк");
    finish_benchmark();
  });
  bench_wait_timer_ = new QTimer(this);
  bench_wait_timer_->setSingleShot(true);
  connect(bench_wait_timer_, &QTimer::timeout, this, [this] {
    bench_abort_ = true;
    bench_wait_reason_ = "немає споживача";
    status_->setText("Очікування споживача вичерпано");
    ++bench_token_;
    request_stop(bench_, bench_token_, &bench_token_);
  });
  reload_modes();
  reload_gpus();
  refresh_profiles();
  saved_fingerprint_ = QString::fromStdString(rtx::profile_json(profile_from_form()));
  mark_dirty();
}


void Window::build() {
  auto* central = new QWidget(this);
  auto* root = new QVBoxLayout(central);
  auto* profile_row = new QHBoxLayout;
  profiles_ = new QComboBox;
  profiles_->setObjectName("profile");
  save_ = new QPushButton("Зберегти");
  save_as_ = new QPushButton("Зберегти як");
  dirty_ = new QLabel("немає незбережених змін");
  profile_row->addWidget(new QLabel("Профіль"));
  profile_row->addWidget(profiles_, 1);
  profile_row->addWidget(save_);
  profile_row->addWidget(save_as_);
  profile_row->addWidget(dirty_);
  root->addLayout(profile_row);

  auto* source = new QGroupBox("Джерело");
  source_form_ = new QFormLayout(source);
  input_protocol_ = new QComboBox;
  input_host_ = new QLineEdit;
  input_local_host_ = new QLineEdit;
  input_path_ = new QLineEdit;
  input_app_ = new QLineEdit;
  input_playpath_ = new QLineEdit;
  input_params_ = new QLineEdit;
  input_params_->setPlaceholderText("key=value&key2=value2");
  input_port_ = new QSpinBox;
  input_local_port_ = new QSpinBox;
  input_rtcp_ = new QSpinBox;
  input_rtp_min_ = new QSpinBox;
  input_rtp_max_ = new QSpinBox;
  configure_port(input_port_);
  configure_port(input_local_port_);
  configure_port(input_rtcp_);
  configure_port(input_rtp_min_);
  configure_port(input_rtp_max_);
  input_port_caption_ = new QLabel("Порт");
  input_local_caption_ = new QLabel("Порт");
  source_form_->addRow("Протокол", input_protocol_);
  source_form_->addRow("Адреса", input_host_);
  source_form_->addRow(input_port_caption_, input_port_);
  source_form_->addRow("Адреса", input_local_host_);
  source_form_->addRow(input_local_caption_, input_local_port_);
  source_form_->addRow("Файл", input_path_);
  source_form_->addRow("Застосунок", input_app_);
  source_form_->addRow("Потік", input_playpath_);
  source_form_->addRow("RTCP", input_rtcp_);
  source_form_->addRow("RTP від", input_rtp_min_);
  source_form_->addRow("RTP до", input_rtp_max_);
  source_form_->addRow("Параметри", input_params_);
  root->addWidget(source);

  auto* output = new QGroupBox("Вихід");
  output_form_ = new QFormLayout(output);
  output_mode_ = new QComboBox;
  output_host_ = new QLineEdit("127.0.0.1");
  output_local_host_ = new QLineEdit("127.0.0.1");
  output_path_ = new QLineEdit;
  output_sdp_ = new QLineEdit;
  output_app_ = new QLineEdit;
  output_playpath_ = new QLineEdit;
  output_directory_ = new QLineEdit;
  output_params_ = new QLineEdit;
  output_params_->setPlaceholderText("key=value&key2=value2");
  output_port_ = new QSpinBox;
  output_local_port_ = new QSpinBox;
  output_rtcp_ = new QSpinBox;
  output_rtp_min_ = new QSpinBox;
  output_rtp_max_ = new QSpinBox;
  configure_port(output_port_);
  configure_port(output_local_port_);
  configure_port(output_rtcp_);
  configure_port(output_rtp_min_);
  configure_port(output_rtp_max_);
  output_port_caption_ = new QLabel("порт отримувача");
  output_local_caption_ = new QLabel("локальний порт сервера");
  consumer_ = new QLineEdit;
  consumer_->setReadOnly(true);
  auto* copy = new QPushButton("Копіювати");
  auto* consumer_row = new QHBoxLayout;
  consumer_row->addWidget(consumer_, 1);
  consumer_row->addWidget(copy);
  output_form_->addRow("Протокол", output_mode_);
  output_form_->addRow("Адреса", output_host_);
  output_form_->addRow(output_port_caption_, output_port_);
  output_form_->addRow("Адреса", output_local_host_);
  output_form_->addRow(output_local_caption_, output_local_port_);
  output_form_->addRow("Файл", output_path_);
  output_form_->addRow("SDP", output_sdp_);
  output_form_->addRow("Застосунок", output_app_);
  output_form_->addRow("Потік", output_playpath_);
  output_form_->addRow("Каталог", output_directory_);
  output_form_->addRow("RTCP", output_rtcp_);
  output_form_->addRow("RTP від", output_rtp_min_);
  output_form_->addRow("RTP до", output_rtp_max_);
  output_form_->addRow("Параметри", output_params_);
  output_form_->addRow("Адреса для споживача", consumer_row);
  root->addWidget(output);

  auto* enhance = new QGroupBox("Покращення");
  auto* enhance_form = new QFormLayout(enhance);
  gpu_ = new QComboBox;
  gpu_->setObjectName("gpu");
  refresh_gpu_ = new QPushButton("Оновити");
  auto* gpu_row = new QHBoxLayout;
  gpu_row->addWidget(gpu_, 1);
  gpu_row->addWidget(refresh_gpu_);
  gpu_fit_ = new QLabel("придатність ще не перевірено");
  gpu_fit_->setWordWrap(true);
  quality_auto_ = new QCheckBox("Авто");
  quality_auto_->setChecked(true);
  scale_ = new QComboBox;
  scale_->addItem("×2", 2);
  scale_->addItem("×3", 3);
  quality_ = new QComboBox;
  for (int level = 1; level <= 4; ++level) {
    quality_->addItem(QString::number(level), level);
  }
  quality_->setCurrentIndex(1);
  quality_->setEnabled(false);
  bitrate_ = new QLineEdit;
  bitrate_->setPlaceholderText("0 — розрахунок з розміру кадру");
  enhance_form->addRow("GPU", gpu_row);
  enhance_form->addRow("Придатність", gpu_fit_);
  enhance_form->addRow("Якість", quality_auto_);
  enhance_form->addRow("Масштаб", scale_);
  enhance_form->addRow("Рівень 1–4", quality_);
  enhance_form->addRow("Бітрейт", bitrate_);
  root->addWidget(enhance);

  auto* control = new QHBoxLayout;
  start_ = new QPushButton("Запустити");
  start_->setObjectName("start");
  status_ = new QLabel("Зупинено");
  control->addWidget(start_);
  control->addWidget(status_, 1);
  root->addLayout(control);
  journal_ = new QPlainTextEdit;
  journal_->setReadOnly(true);
  journal_->setMaximumBlockCount(200);
  journal_->setFixedHeight(72);
  root->addWidget(journal_);

  benchmark_toggle_ = new QPushButton("Бенчмарк ▸");
  benchmark_toggle_->setObjectName("benchmark");
  root->addWidget(benchmark_toggle_);
  benchmark_body_ = new QWidget;
  auto* bench_layout = new QVBoxLayout(benchmark_body_);
  auto* bench_form = new QFormLayout;
  bench_file_ = new QLineEdit;
  auto* browse = new QPushButton("Файл");
  auto* file_row = new QHBoxLayout;
  file_row->addWidget(bench_file_, 1);
  file_row->addWidget(browse);
  bench_budget_ = new QSpinBox;
  bench_budget_->setRange(1, 1000);
  bench_budget_->setValue(33);
  bench_budget_->setSuffix(" мс");
  bench_warmup_ = new QSpinBox;
  bench_warmup_->setRange(0, 30);
  bench_warmup_->setValue(2);
  bench_warmup_->setSuffix(" с");
  bench_wait_ = new QSpinBox;
  bench_wait_->setRange(1, 120);
  bench_wait_->setValue(15);
  bench_wait_->setSuffix(" с");
  compare_gpus_ = new QCheckBox("Порівняти GPU");
  bench_form->addRow("Запис", file_row);
  bench_form->addRow("Бюджет", bench_budget_);
  bench_form->addRow("Прогрів", bench_warmup_);
  bench_form->addRow("Очікування споживача", bench_wait_);
  bench_form->addRow(compare_gpus_);
  bench_layout->addLayout(bench_form);
  auto* bench_buttons = new QHBoxLayout;
  bench_run_ = new QPushButton("Запустити бенчмарк");
  bench_cancel_ = new QPushButton("Скасувати");
  bench_apply_ = new QPushButton("Застосувати вибране");
  bench_progress_ = new QProgressBar;
  bench_buttons->addWidget(bench_run_);
  bench_buttons->addWidget(bench_cancel_);
  bench_buttons->addWidget(bench_apply_);
  bench_buttons->addWidget(bench_progress_, 1);
  bench_layout->addLayout(bench_buttons);
  bench_table_ = new QTableWidget(0, 9);
  bench_table_->setHorizontalHeaderLabels(
      {"GPU", "Масштаб", "Якість", "FPS", "Відкинуті", "p95", "p99", "Перевантаження", "bypass"});
  bench_table_->horizontalHeader()->setStretchLastSection(true);
  bench_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  bench_layout->addWidget(bench_table_);
  bench_layout->addWidget(new QLabel(
      "Час — це обробка проксі, без камери, мережі й показу у споживачі. Зображення порівнюйте у програмі-споживачі."));
  benchmark_body_->setVisible(false);
  root->addWidget(benchmark_body_);
  setCentralWidget(central);
  setWindowTitle("RTX VSR");

  connect(save_, &QPushButton::clicked, this, [this] {
    if (profile_path_.isEmpty()) {
      save_as_->click();
      return;
    }
    QString error;
    if (!write_profile(profile_path_, error, true)) {
      QMessageBox::warning(this, "Профіль", error);
    }
  });
  connect(save_as_, &QPushButton::clicked, this, [this] {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/profiles";
    const QString path = QFileDialog::getSaveFileName(this, "Зберегти профіль", dir, "JSON (*.json)");
    if (path.isEmpty()) {
      return;
    }
    QString error;
    if (!write_profile(path, error, true)) {
      QMessageBox::warning(this, "Профіль", error);
      return;
    }
    profile_path_ = path;
    refresh_profiles();
  });
  connect(profiles_, &QComboBox::currentIndexChanged, this, [this](int) {
    const QString path = profiles_->currentData().toString();
    if (path.isEmpty()) {
      profile_path_.clear();
      mark_dirty();
      return;
    }
    load_profile(path);
  });
  connect(input_protocol_, &QComboBox::currentIndexChanged, this, [this](int) { update_source_mode(); });
  connect(output_mode_, &QComboBox::currentIndexChanged, this, [this](int) { update_output_fields(); });
  connect(output_host_, &QLineEdit::textChanged, this, [this](const QString&) { update_consumer(); });
  connect(output_local_host_, &QLineEdit::textChanged, this, [this](const QString&) { update_consumer(); });
  connect(output_path_, &QLineEdit::textChanged, this, [this](const QString&) { update_consumer(); });
  connect(output_app_, &QLineEdit::textChanged, this, [this](const QString&) { update_consumer(); });
  connect(output_playpath_, &QLineEdit::textChanged, this, [this](const QString&) { update_consumer(); });
  connect(output_port_, &QSpinBox::valueChanged, this, [this](int) {
    update_consumer();
    mark_dirty();
  });
  connect(output_local_port_, &QSpinBox::valueChanged, this, [this](int) {
    update_consumer();
    mark_dirty();
  });
  connect(copy, &QPushButton::clicked, this, [this] { QGuiApplication::clipboard()->setText(consumer_->text()); });
  connect(refresh_gpu_, &QPushButton::clicked, this, &Window::reload_gpus);
  connect(gpu_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index >= 0) {
      unresolved_gpu_.clear();
    }
    check_selected_gpu();
    mark_dirty();
  });
  connect(quality_auto_, &QCheckBox::toggled, quality_, &QWidget::setDisabled);
  connect(start_, &QPushButton::clicked, this, [this] { running_ ? stop_proxy() : start_proxy(); });
  connect(benchmark_toggle_, &QPushButton::clicked, this, &Window::toggle_benchmark);
  connect(browse, &QPushButton::clicked, this, [this] {
    const QString path = QFileDialog::getOpenFileName(this, "Запис для бенчмарку");
    if (!path.isEmpty()) {
      bench_file_->setText(path);
    }
  });
  connect(bench_run_, &QPushButton::clicked, this, &Window::start_benchmark);
  connect(bench_cancel_, &QPushButton::clicked, this, &Window::cancel_benchmark);
  connect(bench_apply_, &QPushButton::clicked, this, &Window::apply_benchmark_row);
  const auto touch = [this] { mark_dirty(); };
  connect(input_host_, &QLineEdit::textChanged, this, touch);
  connect(input_local_host_, &QLineEdit::textChanged, this, touch);
  connect(input_path_, &QLineEdit::textChanged, this, touch);
  connect(input_app_, &QLineEdit::textChanged, this, touch);
  connect(input_playpath_, &QLineEdit::textChanged, this, touch);
  connect(input_params_, &QLineEdit::textChanged, this, touch);
  connect(output_host_, &QLineEdit::textChanged, this, touch);
  connect(output_local_host_, &QLineEdit::textChanged, this, touch);
  connect(output_path_, &QLineEdit::textChanged, this, touch);
  connect(output_sdp_, &QLineEdit::textChanged, this, touch);
  connect(output_app_, &QLineEdit::textChanged, this, touch);
  connect(output_playpath_, &QLineEdit::textChanged, this, touch);
  connect(output_directory_, &QLineEdit::textChanged, this, touch);
  connect(output_params_, &QLineEdit::textChanged, this, touch);
  connect(bitrate_, &QLineEdit::textChanged, this, touch);
  connect(scale_, &QComboBox::currentIndexChanged, this, [this](int) { mark_dirty(); });
  connect(quality_, &QComboBox::currentIndexChanged, this, [this](int) { mark_dirty(); });
  connect(quality_auto_, &QCheckBox::toggled, this, [this](bool) { mark_dirty(); });
  const auto touch_port = [this](int) { mark_dirty(); };
  connect(input_port_, &QSpinBox::valueChanged, this, touch_port);
  connect(input_local_port_, &QSpinBox::valueChanged, this, touch_port);
  connect(input_rtcp_, &QSpinBox::valueChanged, this, touch_port);
  connect(input_rtp_min_, &QSpinBox::valueChanged, this, touch_port);
  connect(input_rtp_max_, &QSpinBox::valueChanged, this, touch_port);
  connect(output_rtcp_, &QSpinBox::valueChanged, this, touch_port);
  connect(output_rtp_min_, &QSpinBox::valueChanged, this, touch_port);
  connect(output_rtp_max_, &QSpinBox::valueChanged, this, touch_port);
  update_source_mode();
}

QString Window::proxy_program() const { return QCoreApplication::applicationFilePath(); }

void Window::reload_modes() {
  auto* process = new QProcess(this);
  connect(process, &QProcess::finished, this, [this, process](int, QProcess::ExitStatus) {
    const QJsonDocument document = QJsonDocument::fromJson(process->readAllStandardOutput());
    apply_modes(document.object().value("modes").toArray());
    process->deleteLater();
  });
  process->start(proxy_program(), {"capabilities", "--json"});
}

void Window::reload_gpus() {
  auto* process = new QProcess(this);
  connect(process, &QProcess::finished, this, [this, process](int, QProcess::ExitStatus) {
    doctor_ = QJsonDocument::fromJson(process->readAllStandardOutput()).object();
    apply_gpus(doctor_);
    if (!modes_.isEmpty()) {
      apply_modes(modes_);
    }
    process->deleteLater();
  });
  process->start(proxy_program(), {"doctor", "--json"});
}

void Window::apply_modes(const QJsonArray& modes) {
  modes_ = modes;
  const QString previous_protocol = input_protocol_->currentData().toString();
  const QString previous_output = output_mode_->currentData().toString();
  input_protocol_->blockSignals(true);
  output_mode_->blockSignals(true);
  input_protocol_->clear();
  output_mode_->clear();
  QStringList seen_input;
  QStringList seen_output;
  for (const QJsonValue& value : modes) {
    const QJsonObject mode = value.toObject();
    const QString protocol = mode.value("protocol").toString();
    if (mode.value("direction").toString() == "input" && !seen_input.contains(protocol)) {
      seen_input.push_back(protocol);
      input_protocol_->addItem(protocol, protocol);
    }
    if (mode.value("direction").toString() == "output" && !seen_output.contains(protocol)) {
      seen_output.push_back(protocol);
      output_mode_->addItem(protocol, protocol);
    }
  }
  const int input_index = input_protocol_->findData(previous_protocol);
  input_protocol_->setCurrentIndex(input_index < 0 ? 0 : input_index);
  const int output_index = output_mode_->findData(previous_output);
  output_mode_->setCurrentIndex(output_index < 0 ? 0 : output_index);
  input_protocol_->blockSignals(false);
  output_mode_->blockSignals(false);
  update_source_mode();
  update_output_fields();
  if (profile_path_.isEmpty()) {
    saved_fingerprint_ = QString::fromStdString(rtx::profile_json(profile_from_form()));
  }
  mark_dirty();
}

void Window::apply_gpus(const QJsonObject& doctor) {
  QString wanted = gpu_->currentData().toString();
  if (wanted.isEmpty()) {
    wanted = unresolved_gpu_;
  }
  gpu_->blockSignals(true);
  gpu_->clear();
  const QJsonArray gpus = doctor.value("gpus").toArray();
  for (const QJsonValue& value : gpus) {
    const QJsonObject gpu = value.toObject();
    const QString uuid = gpu.value("uuid").toString();
    const QString name = gpu.value("name").toString();
    const QString id = uuid.isEmpty() ? QString("#%1").arg(gpu.value("index").toInt()) : uuid.left(16);
    gpu_->addItem(name + " · " + vram_text(static_cast<qulonglong>(gpu.value("vram_bytes").toDouble())) + " · " + id,
                  uuid);
  }
  if (gpus.isEmpty()) {
    gpu_->addItem("GPU не виявлено", QString());
  }
  const int index = wanted.isEmpty() ? -1 : gpu_->findData(wanted);
  if (!wanted.isEmpty() && index < 0) {
    unresolved_gpu_ = wanted;
    gpu_->setCurrentIndex(-1);
    gpu_fit_->setText("Збережений GPU відсутній, оберіть інший.");
  } else if (gpus.isEmpty()) {
    unresolved_gpu_.clear();
    gpu_->setCurrentIndex(0);
    const QString note = doctor.value("cuda_note").toString();
    const QString text = note.isEmpty() ? QString("GPU NVIDIA немає. Ефект RTX VSR запуститься після підключення карти.") : note;
    gpu_fit_->setText(text);
    status_->setText(text);
  } else {
    unresolved_gpu_.clear();
    gpu_->setCurrentIndex(index < 0 ? 0 : index);
  }
  gpu_->blockSignals(false);
  check_selected_gpu();
  if (profile_path_.isEmpty()) {
    saved_fingerprint_ = QString::fromStdString(rtx::profile_json(profile_from_form()));
  }
  mark_dirty();
}

void Window::check_selected_gpu() {
  if (running_) {
    return;
  }
  const QString uuid = gpu_->currentData().toString();
  if (uuid.isEmpty()) {
    return;
  }
  // Без повторного doctor/SDK на кожну зміну combo: беремо кеш reload_gpus.
  bool listed = false;
  for (const QJsonValue& value : doctor_.value("gpus").toArray()) {
    if (value.toObject().value("uuid").toString() == uuid) {
      listed = true;
      break;
    }
  }
  const bool effect = doctor_.value("effect").toString() == "confirmed";
  const bool encoder = doctor_.value("nvenc_h264").toBool() || doctor_.value("nvenc_hevc").toBool();
  QStringList blockers;
  for (const QJsonValue& blocker : doctor_.value("blockers").toArray()) {
    blockers.push_back(blocker.toString());
  }
  QString text;
  if (!listed) {
    text = "Обраний GPU відсутній у останньому опитуванні. Оновіть список карт.";
  } else if (effect) {
    text = "VSR підтверджено при опитуванні GPU. Придатність до потоку — запуском сеансу.";
  } else {
    text = blockers.isEmpty() ? "VSR на цій карті не підтверджено." : blockers.join(" ");
  }
  text += encoder ? " Кодер NVENC є у FFmpeg, але це ще не весь конвеєр."
                  : " Кодера NVENC у FFmpeg немає.";
  gpu_fit_->setText(text);
}

void Window::fill_roles() { update_source_mode(); }

void Window::update_source_mode() {
  const QString protocol = input_protocol_->currentData().toString();
  const QStringList fields = union_fields(modes_, "input", protocol);
  const bool remote = list_has(fields, "remote_host");
  const bool local = list_has(fields, "local_host");
  const bool remote_port = list_has(fields, "remote_port");
  const bool local_port = list_has(fields, "local_port");
  rename_row(source_form_, input_host_, remote && local ? "Віддалена адреса" : "Адреса");
  rename_row(source_form_, input_local_host_, remote && local ? "Локальна адреса" : "Адреса");
  input_port_caption_->setText(remote_port && local_port ? "Віддалений порт" : "Порт");
  input_local_caption_->setText(local_port && remote_port ? "Локальний порт" : "Порт");
  show_row(source_form_, input_host_, remote);
  show_row(source_form_, input_port_, remote_port);
  show_row(source_form_, input_local_host_, local);
  show_row(source_form_, input_local_port_, local_port);
  show_row(source_form_, input_path_, list_has(fields, "path"));
  show_row(source_form_, input_app_, list_has(fields, "app"));
  show_row(source_form_, input_playpath_, list_has(fields, "playpath"));
  show_row(source_form_, input_rtcp_, list_has(fields, "rtcp_port"));
  show_row(source_form_, input_rtp_min_, list_has(fields, "rtp_port_min"));
  show_row(source_form_, input_rtp_max_, list_has(fields, "rtp_port_max"));
  show_row(source_form_, input_params_, list_has(fields, "params"));
  mark_dirty();
}

void Window::update_output_fields() {
  const QStringList fields = union_fields(modes_, "output", output_mode_->currentData().toString());
  const bool remote = list_has(fields, "remote_host");
  const bool local = list_has(fields, "local_host");
  const bool remote_port = list_has(fields, "remote_port");
  const bool local_port = list_has(fields, "local_port");
  rename_row(output_form_, output_host_, remote && local ? "Віддалена адреса" : "Адреса");
  rename_row(output_form_, output_local_host_, remote && local ? "Локальна адреса" : "Адреса");
  output_port_caption_->setText(remote_port && local_port ? "Віддалений порт" : "Порт");
  output_local_caption_->setText(local_port && remote_port ? "Локальний порт" : "Порт");
  show_row(output_form_, output_host_, remote);
  show_row(output_form_, output_port_, remote_port);
  show_row(output_form_, output_local_host_, local);
  show_row(output_form_, output_local_port_, local_port);
  show_row(output_form_, output_path_, list_has(fields, "path"));
  show_row(output_form_, output_sdp_, list_has(fields, "sdp"));
  show_row(output_form_, output_app_, list_has(fields, "app"));
  show_row(output_form_, output_playpath_, list_has(fields, "playpath"));
  show_row(output_form_, output_directory_, list_has(fields, "directory"));
  show_row(output_form_, output_rtcp_, list_has(fields, "rtcp_port"));
  show_row(output_form_, output_rtp_min_, list_has(fields, "rtp_port_min"));
  show_row(output_form_, output_rtp_max_, list_has(fields, "rtp_port_max"));
  show_row(output_form_, output_params_, list_has(fields, "params"));
  update_consumer();
  mark_dirty();
}

void Window::start_proxy() {
  if (running_) {
    return;
  }
  if (gpu_->currentIndex() < 0 && !unresolved_gpu_.isEmpty()) {
    status_->setText("Збережений GPU відсутній, оберіть інший.");
    return;
  }
  // Інвалідуємо таймери stop і не стартуємо поверх ще живого дочірнього процесу.
  ++proxy_token_;
  if (proxy_->state() != QProcess::NotRunning) {
    status_->setText("Зупинення попереднього сеансу…");
    force_stop_process(proxy_);
  }
  if (proxy_->state() != QProcess::NotRunning) {
    status_->setText("Попередній сеанс ще не завершився");
    return;
  }
  const QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/session.json";
  QDir().mkpath(QFileInfo(path).path());
  QString error;
  if (!write_profile(path, error, false)) {
    status_->setText(error);
    return;
  }
  journal_->clear();
  status_->setText("Підключення");
  stopping_ = false;
  running_ = true;
  start_->setText("Зупинити");
  gpu_->setEnabled(false);
  refresh_gpu_->setEnabled(false);
  bench_run_->setEnabled(false);
  ++proxy_token_;
  proxy_->setProperty("rtx_stop_generation", QVariant::fromValue(proxy_token_));
  proxy_->start(proxy_program(), {"run", "--profile", path});
}

void Window::stop_proxy() {
  if (proxy_->state() == QProcess::NotRunning) {
    stopping_ = false;
    running_ = false;
    start_->setText("Запустити");
    status_->setText("Зупинено");
    return;
  }
  stopping_ = true;
  status_->setText("Зупинено");
  ++proxy_token_;
  request_stop(proxy_, proxy_token_, &proxy_token_);
}

void Window::read_proxy() {
  while (proxy_->canReadLine()) {
    const QJsonDocument document = QJsonDocument::fromJson(proxy_->readLine());
    const QJsonObject event = document.object();
    const QString type = event.value("type").toString();
    if (type == "status") {
      const QString text = state_text(event.value("state").toString());
      const QString detail = event.value("detail").toString();
      status_->setText(detail.isEmpty() ? text : text + ": " + detail);
    } else if (type == "metrics" || type == "benchmark") {
      const QString line = QString("×%1 якість %2, %3×%4 → %5×%6, p95 %7 мс")
                               .arg(event.value("vsr_scale").toInt())
                               .arg(event.value("vsr_quality").toInt())
                               .arg(event.value("input_width").toInt())
                               .arg(event.value("input_height").toInt())
                               .arg(event.value("output_width").toInt())
                               .arg(event.value("output_height").toInt())
                               .arg(event.value("proxy_p95_ms").toDouble(), 0, 'f', 1);
      const QString state = state_text(event.value("vsr_state").toString());
      if (!state.isEmpty()) {
        status_->setText(state + " · " + line);
      }
      if (!event.value("gpu_name").toString().isEmpty()) {
        gpu_fit_->setText("Зараз використовується " + event.value("gpu_name").toString() + " " +
                          event.value("gpu_uuid").toString());
      }
    }
  }
}

