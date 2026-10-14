#include "window.hpp"

#include "endpoint.hpp"

#include <algorithm>
#include <memory>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
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
#include <QRadioButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString state_text(const QString& state) {
  if (state == "connecting") {
    return "Підключення";
  }
  if (state == "waiting") {
    return "Очікування споживача";
  }
  if (state == "running") {
    return "Робота VSR";
  }
  if (state == "overloaded") {
    return "Перевантаження";
  }
  if (state == "bypass") {
    return "Передавання без VSR";
  }
  if (state == "error") {
    return "Помилка";
  }
  if (state == "stopped") {
    return "Зупинено";
  }
  return state;
}

QString container_label(const QString& container) {
  if (container == "rtp_mpegts" || container == "mpegts") {
    return "MPEG-TS";
  }
  if (container == "rtp") {
    return "RTP";
  }
  if (container == "flv") {
    return "FLV";
  }
  if (container == "hls") {
    return "HLS";
  }
  if (container == "rtsp") {
    return "RTSP";
  }
  if (container.isEmpty()) {
    return "потік";
  }
  return container;
}

QString vram_text(qulonglong bytes) {
  if (bytes == 0) {
    return "VRAM невідомий";
  }
  const double gib = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
  return gib >= 1.0 ? QString::number(gib, 'f', 1) + " ГБ" : QString::number(bytes / (1024 * 1024)) + " МБ";
}

bool list_has(const QStringList& values, const QString& name) { return values.contains(name); }

QStringList required_of(const QVariantMap& mode) { return mode.value("required").toStringList(); }

QVariantMap mode_payload(const QJsonObject& mode, const QString& codec) {
  QStringList required;
  for (const QJsonValue& item : mode.value("required").toArray()) {
    required.push_back(item.toString());
  }
  QVariantMap payload;
  payload.insert("protocol", mode.value("protocol").toString());
  payload.insert("role", mode.value("role").toString());
  payload.insert("container", mode.value("container").toString());
  payload.insert("codec", codec);
  payload.insert("required", required);
  return payload;
}

}  // namespace

Window::~Window() {
  const auto processes = findChildren<QProcess*>();
  for (QProcess* process : processes) {
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
      process->kill();
      process->waitForFinished(2000);
    }
  }
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
    running_ = false;
    start_->setText("Запустити");
    gpu_->setEnabled(true);
    refresh_gpu_->setEnabled(true);
    bench_run_->setEnabled(true);
    if (status_->text().startsWith("Помилка") || code == 0) {
      return;
    }
    status_->setText(code == 3 ? "Помилка: немає підтвердженого GPU або SDK" : "Помилка запуску");
  });
  connect(bench_, &QProcess::finished, this, [this](int, QProcess::ExitStatus) { finish_benchmark(); });
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
  auto* source_form = new QFormLayout(source);
  source_url_ = new QRadioButton("URL");
  source_fields_ = new QRadioButton("Адреса і порт");
  source_url_->setChecked(true);
  url_ = new QLineEdit;
  input_protocol_ = new QComboBox;
  input_host_ = new QLineEdit;
  input_path_ = new QLineEdit;
  input_port_ = new QSpinBox;
  input_port_->setRange(0, 65535);
  input_port_->setSpecialValueText("—");
  input_role_ = new QComboBox;
  input_port_caption_ = new QLabel("локальний порт прийому");
  source_form->addRow(source_url_, url_);
  source_form->addRow(source_fields_, input_protocol_);
  source_form->addRow("Адреса", input_host_);
  source_form->addRow("Файл", input_path_);
  source_form->addRow(input_port_caption_, input_port_);
  source_form->addRow("Роль", input_role_);
  root->addWidget(source);

  auto* output = new QGroupBox("Вихід");
  auto* output_form = new QFormLayout(output);
  output_mode_ = new QComboBox;
  output_host_ = new QLineEdit("127.0.0.1");
  output_path_ = new QLineEdit;
  output_port_ = new QSpinBox;
  output_port_->setRange(0, 65535);
  output_port_->setSpecialValueText("—");
  output_port_caption_ = new QLabel("порт отримувача");
  consumer_ = new QLineEdit;
  consumer_->setReadOnly(true);
  auto* copy = new QPushButton("Копіювати");
  auto* consumer_row = new QHBoxLayout;
  consumer_row->addWidget(consumer_, 1);
  consumer_row->addWidget(copy);
  output_form->addRow("Режим", output_mode_);
  output_form->addRow("Адреса", output_host_);
  output_form->addRow("Файл", output_path_);
  output_form->addRow(output_port_caption_, output_port_);
  output_form->addRow("Адреса для споживача", consumer_row);
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
  compare_gpus_ = new QCheckBox("Порівняти GPU");
  bench_form->addRow("Запис", file_row);
  bench_form->addRow("Бюджет", bench_budget_);
  bench_form->addRow("Прогрів", bench_warmup_);
  bench_form->addRow(compare_gpus_);
  bench_layout->addLayout(bench_form);
  auto* bench_buttons = new QHBoxLayout;
  bench_run_ = new QPushButton("Запустити бенчмарк");
  bench_apply_ = new QPushButton("Застосувати вибране");
  bench_progress_ = new QProgressBar;
  bench_buttons->addWidget(bench_run_);
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
    if (!path.isEmpty()) {
      load_profile(path);
    }
  });
  connect(source_url_, &QRadioButton::toggled, this, [this](bool) { fill_roles(); });
  connect(input_protocol_, &QComboBox::currentIndexChanged, this, [this](int) { fill_roles(); });
  connect(input_role_, &QComboBox::currentIndexChanged, this, [this](int) { update_source_mode(); });
  connect(output_mode_, &QComboBox::currentIndexChanged, this, [this](int) { update_output_fields(); });
  connect(output_host_, &QLineEdit::textChanged, this, [this](const QString&) { update_consumer(); });
  connect(output_path_, &QLineEdit::textChanged, this, [this](const QString&) { update_consumer(); });
  connect(output_port_, &QSpinBox::valueChanged, this, [this](int) { update_consumer(); });
  connect(copy, &QPushButton::clicked, this, [this] { QGuiApplication::clipboard()->setText(consumer_->text()); });
  connect(refresh_gpu_, &QPushButton::clicked, this, &Window::reload_gpus);
  connect(gpu_, &QComboBox::currentIndexChanged, this, [this](int) {
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
  connect(bench_apply_, &QPushButton::clicked, this, &Window::apply_benchmark_row);
  const auto touch = [this] { mark_dirty(); };
  connect(url_, &QLineEdit::textChanged, this, touch);
  connect(input_host_, &QLineEdit::textChanged, this, touch);
  connect(input_path_, &QLineEdit::textChanged, this, touch);
  connect(output_host_, &QLineEdit::textChanged, this, touch);
  connect(output_path_, &QLineEdit::textChanged, this, touch);
  connect(bitrate_, &QLineEdit::textChanged, this, touch);
  connect(scale_, &QComboBox::currentIndexChanged, this, [this](int) { mark_dirty(); });
  connect(quality_, &QComboBox::currentIndexChanged, this, [this](int) { mark_dirty(); });
  connect(quality_auto_, &QCheckBox::toggled, this, [this](bool) { mark_dirty(); });
  update_source_mode();
}

QString Window::proxy_program() const {
  const QString beside = QCoreApplication::applicationDirPath() + "/rtx-vsr-proxy";
  return QFileInfo::exists(beside) ? beside : QString("rtx-vsr-proxy");
}

QString Window::secret_id(const QString& path) const { return "profile/" + path; }

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
  const QString previous_output = output_mode_->currentData().toMap().value("protocol").toString() + "|" +
                                  output_mode_->currentData().toMap().value("role").toString() + "|" +
                                  output_mode_->currentData().toMap().value("container").toString() + "|" +
                                  output_mode_->currentData().toMap().value("codec").toString();
  input_protocol_->blockSignals(true);
  output_mode_->blockSignals(true);
  input_protocol_->clear();
  output_mode_->clear();
  QStringList seen;
  const bool h264 = doctor_.value("nvenc_h264").toBool(true);
  const bool hevc = doctor_.value("nvenc_hevc").toBool(true);
  for (const QJsonValue& value : modes) {
    const QJsonObject mode = value.toObject();
    const QString protocol = mode.value("protocol").toString();
    if (mode.value("direction").toString() == "input" && !seen.contains(protocol)) {
      seen.push_back(protocol);
      input_protocol_->addItem(protocol, protocol);
    }
    if (mode.value("direction").toString() != "output") {
      continue;
    }
    const QStringList codecs = {h264 ? "h264" : QString(), hevc ? "hevc" : QString()};
    for (const QString& codec : codecs) {
      if (codec.isEmpty()) {
        continue;
      }
      const QString label = protocol.toUpper() + " → " + container_label(mode.value("container").toString()) + " → " +
                            (codec == "hevc" ? "H.265" : "H.264");
      output_mode_->addItem(label, mode_payload(mode, codec));
    }
  }
  const int input_index = input_protocol_->findData(previous_protocol);
  input_protocol_->setCurrentIndex(input_index < 0 ? 0 : input_index);
  for (int index = 0; index < output_mode_->count(); ++index) {
    const QVariantMap mode = output_mode_->itemData(index).toMap();
    const QString key = mode.value("protocol").toString() + "|" + mode.value("role").toString() + "|" +
                        mode.value("container").toString() + "|" + mode.value("codec").toString();
    if (key == previous_output) {
      output_mode_->setCurrentIndex(index);
      break;
    }
  }
  input_protocol_->blockSignals(false);
  output_mode_->blockSignals(false);
  fill_roles();
  update_output_fields();
  if (profile_path_.isEmpty()) {
    saved_fingerprint_ = QString::fromStdString(rtx::profile_json(profile_from_form()));
  }
  mark_dirty();
}

void Window::apply_gpus(const QJsonObject& doctor) {
  const QString selected = gpu_->currentData().toString();
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
    gpu_fit_->setText(doctor.value("cuda_note").toString("CUDA-пристроїв немає. Оберіть карту після підключення."));
  } else {
    const int index = gpu_->findData(selected);
    gpu_->setCurrentIndex(index < 0 ? 0 : index);
    if (!selected.isEmpty() && index < 0) {
      gpu_fit_->setText("Збережений GPU відсутній, оберіть інший.");
    }
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
  const QString codec = output_mode_->currentData().toMap().value("codec").toString();
  if (uuid.isEmpty()) {
    return;
  }
  QStringList args{"doctor", "--json", "--gpu-uuid", uuid};
  if (codec == "h264" || codec == "hevc") {
    args << "--codec" << codec;
  }
  auto* process = new QProcess(this);
  connect(process, &QProcess::finished, this, [this, process, uuid, codec](int, QProcess::ExitStatus) {
    const QJsonObject doctor = QJsonDocument::fromJson(process->readAllStandardOutput()).object();
    process->deleteLater();
    if (running_ || gpu_->currentData().toString() != uuid) {
      return;
    }
    const bool effect = doctor.value("effect").toString() == "confirmed";
    QStringList blockers;
    for (const QJsonValue& blocker : doctor.value("blockers").toArray()) {
      blockers.push_back(blocker.toString());
    }
    const bool encoder = codec == "hevc" ? doctor.value("nvenc_hevc").toBool() : doctor.value("nvenc_h264").toBool();
    QString text = effect ? "VSR на цій карті підтверджено створенням ефекту."
                          : (blockers.isEmpty() ? "VSR на цій карті не підтверджено." : blockers.join(" "));
    text += encoder ? " Кодер вибраного виходу є у FFmpeg, але це ще не весь конвеєр."
                    : " Кодера вибраного виходу у FFmpeg немає.";
    text += " Придатність до конкретного потоку підтверджується запуском сеансу.";
    gpu_fit_->setText(text);
  });
  process->start(proxy_program(), args);
}

void Window::fill_roles() {
  input_role_->blockSignals(true);
  const QString previous = input_role_->currentData().toString();
  const QString protocol = input_protocol_->currentData().toString();
  const QString scheme = url_->text().section("://", 0, 0);
  input_role_->clear();
  if (source_url_->isChecked()) {
    input_role_->addItem("з URL", QString());
  }
  QStringList seen;
  for (const QJsonValue& value : modes_) {
    const QJsonObject mode = value.toObject();
    if (mode.value("direction").toString() != "input") {
      continue;
    }
    const QString mode_protocol = mode.value("protocol").toString();
    if (source_url_->isChecked()) {
      if (!scheme.isEmpty() && mode_protocol != scheme) {
        continue;
      }
    } else if (mode_protocol != protocol) {
      continue;
    }
    const QString role = mode.value("role").toString();
    if (seen.contains(role)) {
      continue;
    }
    seen.push_back(role);
    input_role_->addItem(role, role);
  }
  const int index = input_role_->findData(previous);
  input_role_->setCurrentIndex(index < 0 ? 0 : index);
  input_role_->blockSignals(false);
  update_source_mode();
}

void Window::update_source_mode() {
  const bool url = source_url_->isChecked();
  url_->setVisible(url);
  input_protocol_->setEnabled(!url);
  QStringList required;
  if (!url) {
    for (const QJsonValue& value : modes_) {
      const QJsonObject mode = value.toObject();
      if (mode.value("direction").toString() == "input" &&
          mode.value("protocol").toString() == input_protocol_->currentData().toString() &&
          mode.value("role").toString() == input_role_->currentData().toString()) {
        for (const QJsonValue& item : mode.value("required").toArray()) {
          required.push_back(item.toString());
        }
        break;
      }
    }
  }
  const bool host = list_has(required, "remote_host") || list_has(required, "local_host");
  const bool port = list_has(required, "remote_port") || list_has(required, "local_port");
  const bool path = list_has(required, "path");
  input_host_->setVisible(!url && host);
  input_port_->setVisible(!url && port);
  input_port_caption_->setVisible(!url && port);
  input_path_->setVisible(!url && path);
  rtx::Role role = rtx::Role::Listener;
  rtx::parse_role(input_role_->currentData().toString().toStdString(), role);
  input_port_caption_->setText(QString::fromStdString(rtx::port_caption(rtx::Direction::Input, role)));
  mark_dirty();
}

void Window::update_output_fields() {
  const QVariantMap mode = output_mode_->currentData().toMap();
  const QStringList required = required_of(mode);
  rtx::Role role = rtx::Role::Client;
  rtx::parse_role(mode.value("role").toString().toStdString(), role);
  output_port_caption_->setText(QString::fromStdString(rtx::port_caption(rtx::Direction::Output, role)));
  const bool host = list_has(required, "remote_host") || list_has(required, "local_host");
  const bool port = list_has(required, "remote_port") || list_has(required, "local_port");
  output_host_->setVisible(host);
  output_port_->setVisible(port);
  output_port_caption_->setVisible(port);
  output_path_->setVisible(list_has(required, "path"));
  update_consumer();
  check_selected_gpu();
  mark_dirty();
}

void Window::update_consumer() {
  const rtx::Profile profile = profile_from_form();
  if (profile.output.protocol == "file") {
    consumer_->setText(QString::fromStdString(profile.output.path));
    return;
  }
  rtx::Endpoint endpoint;
  endpoint.protocol = profile.output.protocol;
  endpoint.role = profile.output.role;
  endpoint.remote_host = profile.output.remote_host;
  endpoint.remote_port = profile.output.remote_port;
  endpoint.local_host = profile.output.local_host;
  endpoint.local_port = profile.output.local_port;
  endpoint.path = profile.output.path;
  consumer_->setText(QString::fromStdString(rtx::consumer_address(endpoint)));
}

rtx::Profile Window::profile_from_form() const {
  rtx::Profile profile;
  profile.name = profiles_->currentText().isEmpty() ? "Профіль" : profiles_->currentText().toStdString();
  profile.gpu_uuid = gpu_->currentData().toString().toStdString();
  profile.scale = scale_->currentData().toInt();
  profile.quality_auto = quality_auto_->isChecked();
  profile.quality = quality_->currentData().toInt();
  profile.bitrate = bitrate_->text().trimmed().isEmpty() ? 0 : bitrate_->text().toLongLong();
  profile.codec = "h264";
  const QVariantMap mode = output_mode_->currentData().toMap();
  if (!mode.isEmpty()) {
    profile.output.protocol = mode.value("protocol").toString().toStdString();
    profile.output.role = mode.value("role").toString().toStdString();
    profile.codec = mode.value("codec").toString().toStdString();
    const QString container = mode.value("container").toString();
    if (profile.output.protocol == "rtp") {
      profile.output.payload = container == "rtp" ? profile.codec : "mpegts";
    }
    if (list_has(required_of(mode), "path")) {
      profile.output.path = output_path_->text().trimmed().toStdString();
    }
  }
  const bool local_out = profile.output.role == "listener" || profile.output.role == "server" ||
                         profile.output.role == "rendezvous";
  if (output_host_->isVisible()) {
    if (local_out) {
      profile.output.local_host = output_host_->text().trimmed().toStdString();
    } else {
      profile.output.remote_host = output_host_->text().trimmed().toStdString();
    }
  }
  if (output_port_->isVisible()) {
    const int port = output_port_->value() > 0 ? output_port_->value() : -1;
    if (local_out) {
      profile.output.local_port = port;
    } else {
      profile.output.remote_port = port;
    }
  }
  if (source_url_->isChecked()) {
    profile.input.url = url_->text().trimmed().toStdString();
    const auto scheme = profile.input.url.find("://");
    if (scheme != std::string::npos) {
      profile.input.protocol = profile.input.url.substr(0, scheme);
    }
    profile.input.role = input_role_->currentData().toString().toStdString();
  } else {
    profile.input.protocol = input_protocol_->currentData().toString().toStdString();
    profile.input.role = input_role_->currentData().toString().toStdString();
    const bool local_in = profile.input.role == "listener" || profile.input.role == "server" ||
                          profile.input.role == "rendezvous";
    if (input_host_->isVisible()) {
      if (local_in) {
        profile.input.local_host = input_host_->text().trimmed().toStdString();
      } else {
        profile.input.remote_host = input_host_->text().trimmed().toStdString();
      }
    }
    if (input_port_->isVisible()) {
      const int port = input_port_->value() > 0 ? input_port_->value() : -1;
      if (local_in) {
        profile.input.local_port = port;
      } else {
        profile.input.remote_port = port;
      }
    }
    if (input_path_->isVisible()) {
      profile.input.path = input_path_->text().trimmed().toStdString();
    }
  }
  if (profile.input.url == loaded_url_.toStdString()) {
    profile.input.credential_id = loaded_credential_.toStdString();
  }
  return profile;
}

void Window::form_from_profile(const rtx::Profile& profile, const QString& visible_url) {
  const int gpu_index = gpu_->findData(QString::fromStdString(profile.gpu_uuid));
  if (gpu_index >= 0) {
    gpu_->setCurrentIndex(gpu_index);
  }
  const int scale_index = scale_->findData(profile.scale);
  if (scale_index >= 0) {
    scale_->setCurrentIndex(scale_index);
  }
  quality_auto_->setChecked(profile.quality_auto);
  const int quality_index = quality_->findData(profile.quality);
  if (quality_index >= 0) {
    quality_->setCurrentIndex(quality_index);
  }
  bitrate_->setText(profile.bitrate > 0 ? QString::number(profile.bitrate) : QString());
  if (!profile.input.url.empty() || !visible_url.isEmpty()) {
    source_url_->setChecked(true);
    url_->setText(visible_url);
  } else {
    source_fields_->setChecked(true);
    const int protocol = input_protocol_->findData(QString::fromStdString(profile.input.protocol));
    if (protocol >= 0) {
      input_protocol_->setCurrentIndex(protocol);
    }
    fill_roles();
    const int role = input_role_->findData(QString::fromStdString(profile.input.role));
    if (role >= 0) {
      input_role_->setCurrentIndex(role);
    }
    const bool local = profile.input.role == "listener" || profile.input.role == "server" ||
                       profile.input.role == "rendezvous";
    input_host_->setText(QString::fromStdString(local ? profile.input.local_host : profile.input.remote_host));
    input_port_->setValue(local ? std::max(0, profile.input.local_port) : std::max(0, profile.input.remote_port));
    input_path_->setText(QString::fromStdString(profile.input.path));
  }
  const QString container = profile.output.protocol == "rtp" && profile.output.payload != "mpegts" &&
                                    !profile.output.payload.empty()
                                ? "rtp"
                                : QString();
  for (int index = 0; index < output_mode_->count(); ++index) {
    const QVariantMap mode = output_mode_->itemData(index).toMap();
    if (mode.value("protocol").toString().toStdString() == profile.output.protocol &&
        mode.value("role").toString().toStdString() == profile.output.role &&
        mode.value("codec").toString().toStdString() == profile.codec &&
        (container.isEmpty() || mode.value("container").toString() == container)) {
      output_mode_->setCurrentIndex(index);
      break;
    }
  }
  const bool local_out = profile.output.role == "listener" || profile.output.role == "server" ||
                         profile.output.role == "rendezvous";
  output_host_->setText(QString::fromStdString(local_out ? profile.output.local_host : profile.output.remote_host));
  output_port_->setValue(local_out ? std::max(0, profile.output.local_port) : std::max(0, profile.output.remote_port));
  output_path_->setText(QString::fromStdString(profile.output.path));
}

bool Window::write_profile(const QString& path, QString& error, bool remember) {
  rtx::Profile profile = profile_from_form();
  const std::unique_ptr<rtx::SecretStore> store = rtx::system_secret_store();
  std::string failure;
  if (source_url_->isChecked()) {
    const rtx::UrlSecret parts = rtx::split_url_secret(url_->text().trimmed().toStdString());
    if (parts.has_password) {
      if (!rtx::seal_input_secret(profile, url_->text().trimmed().toStdString(), secret_id(path).toStdString(), *store,
                                 failure)) {
        error = QString::fromStdString(failure);
        return false;
      }
      loaded_credential_ = QString::fromStdString(profile.input.credential_id);
      loaded_url_ = QString::fromStdString(profile.input.url);
      url_->setText(loaded_url_);
    } else if (url_->text().trimmed() != loaded_url_ && !loaded_credential_.isEmpty()) {
      store->remove(loaded_credential_.toStdString(), failure);
      profile.input.credential_id.clear();
      loaded_credential_.clear();
      loaded_url_ = url_->text().trimmed();
    }
  }
  if (!rtx::save_profile_file(path.toStdString(), profile, failure)) {
    error = QString::fromStdString(failure);
    return false;
  }
  if (remember) {
    profile_path_ = path;
    saved_fingerprint_ = QString::fromStdString(rtx::profile_json(profile_from_form()));
    mark_dirty();
  }
  error.clear();
  return true;
}

void Window::load_profile(const QString& path) {
  rtx::Profile profile;
  std::string error;
  if (!rtx::load_profile_file(path.toStdString(), profile, error)) {
    status_->setText(QString::fromStdString(error));
    return;
  }
  loaded_credential_ = QString::fromStdString(profile.input.credential_id);
  loaded_url_ = QString::fromStdString(profile.input.url);
  form_from_profile(profile, loaded_url_);
  profile_path_ = path;
  if (!profile.gpu_uuid.empty() && gpu_->findData(QString::fromStdString(profile.gpu_uuid)) < 0) {
    gpu_fit_->setText("Збережений GPU відсутній, оберіть інший.");
  }
  saved_fingerprint_ = QString::fromStdString(rtx::profile_json(profile_from_form()));
  mark_dirty();
}

void Window::refresh_profiles() {
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/profiles";
  QDir().mkpath(dir);
  const QString current = profile_path_;
  profiles_->blockSignals(true);
  profiles_->clear();
  profiles_->addItem("новий", QString());
  const QStringList files = QDir(dir).entryList({"*.json"}, QDir::Files, QDir::Name);
  for (const QString& name : files) {
    profiles_->addItem(name, dir + "/" + name);
  }
  if (!current.isEmpty() && profiles_->findData(current) < 0) {
    profiles_->addItem(QFileInfo(current).fileName(), current);
  }
  const int index = profiles_->findData(current);
  profiles_->setCurrentIndex(index < 0 ? 0 : index);
  profiles_->blockSignals(false);
}

void Window::mark_dirty() {
  const bool dirty = QString::fromStdString(rtx::profile_json(profile_from_form())) != saved_fingerprint_;
  dirty_->setText(dirty ? "є незбережені зміни" : "немає незбережених змін");
  setWindowTitle(dirty ? "RTX VSR •" : "RTX VSR");
}

void Window::start_proxy() {
  if (running_) {
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
  running_ = true;
  start_->setText("Зупинити");
  gpu_->setEnabled(false);
  refresh_gpu_->setEnabled(false);
  bench_run_->setEnabled(false);
  proxy_->start(proxy_program(), {"run", "--profile", path});
}

void Window::stop_proxy() {
  if (proxy_->state() == QProcess::NotRunning) {
    running_ = false;
    start_->setText("Запустити");
    return;
  }
  proxy_->write("stop\n");
  status_->setText("Зупинено");
  QTimer::singleShot(3000, this, [this] {
    if (proxy_->state() != QProcess::NotRunning) {
      proxy_->terminate();
    }
  });
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
  QString error;
  const QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/benchmark.json";
  QDir().mkpath(QFileInfo(path).path());
  if (!write_profile(path, error, false)) {
    status_->setText(error);
    return;
  }
  bench_profile_ = path;
  bench_queue_.clear();
  bench_card_ = {};
  bench_card_uuid_.clear();
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
    status_->setText("Бенчмарк завершено");
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
                   "0",
                   "--input-protocol",
                   "file",
                   "--input-role",
                   "client",
                   "--input-path",
                   bench_file_->text()};
  if (!job.gpu_uuid.isEmpty()) {
    args << "--gpu-uuid" << job.gpu_uuid;
  }
  status_->setText(QString("Бенчмарк %1/%2").arg(bench_index_ + 1).arg(bench_queue_.size()));
  bench_->start(proxy_program(), args);
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
  if (bench_index_ >= bench_queue_.size()) {
    return;
  }
  const BenchJob job = bench_queue_.at(bench_index_);
  if (!bench_card_.isEmpty() && bench_card_uuid_ != job.gpu_uuid) {
    flush_benchmark(bench_card_uuid_);
  }
  bench_card_uuid_ = job.gpu_uuid;
  const QByteArray output = bench_->readAllStandardOutput();
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
  if (result.isEmpty()) {
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
    put(3, QString::number(result.value("fps").toDouble(), 'f', 1));
    put(4, QString::number(100.0 * dropped / incoming, 'f', 1) + "%");
    put(5, QString::number(p95, 'f', 1));
    put(6, QString::number(result.value("proxy_p99_ms").toDouble(), 'f', 1));
    put(7, result.value("overload_frames").toDouble() > 0 ? "так" : "ні");
    put(8, result.value("bypass_entries").toDouble() > 0 ? "так" : "ні");
    if (p95 > bench_budget_->value()) {
      bench_table_->item(row, 5)->setBackground(QColor(80, 40, 40));
    }
    saved.insert("fps", result.value("fps"));
    saved.insert("frames_in", result.value("frames_in"));
    saved.insert("frames_dropped", result.value("frames_dropped"));
    saved.insert("proxy_p95_ms", result.value("proxy_p95_ms"));
    saved.insert("proxy_p99_ms", result.value("proxy_p99_ms"));
    saved.insert("overload_frames", result.value("overload_frames"));
    saved.insert("bypass_entries", result.value("bypass_entries"));
    saved.insert("within_budget", p95 <= bench_budget_->value());
    saved.insert("ffmpeg", result.value("ffmpeg"));
    saved.insert("input_width", result.value("input_width"));
    saved.insert("input_height", result.value("input_height"));
    saved.insert("output_width", result.value("output_width"));
    saved.insert("output_height", result.value("output_height"));
    saved.insert("vsr_scale", result.value("vsr_scale"));
    saved.insert("vsr_quality", result.value("vsr_quality"));
    ++bench_index_;
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
  const int scale = bench_table_->item(row, 0)->data(Qt::UserRole).toInt();
  const int quality = bench_table_->item(row, 0)->data(Qt::UserRole + 1).toInt();
  if (scale < 2) {
    return;
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
