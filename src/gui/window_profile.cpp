#include "window.hpp"

#include "endpoint.hpp"
#include "window_helpers.hpp"

#include <algorithm>
#include <filesystem>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QStandardPaths>
#include <QString>

void Window::apply_input_rtp_payload(rtx::Profile& profile) const {
  if (profile.input.protocol != "rtp") {
    return;
  }
  QString container;
  for (const QJsonValue& value : modes_) {
    const QJsonObject mode = value.toObject();
    if (mode.value("direction").toString() == "input" &&
        mode.value("protocol").toString().toStdString() == profile.input.protocol &&
        mode.value("role").toString().toStdString() == profile.input.role) {
      container = mode.value("container").toString();
      break;
    }
  }
  if (container == "sdp" || (container.isEmpty() && profile.input.role == "client")) {
    profile.input.payload = "es";
    return;
  }
  profile.input.payload = "mpegts";
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
  endpoint.app = profile.output.app;
  endpoint.playpath = profile.output.playpath;
  consumer_->setText(QString::fromStdString(rtx::consumer_address(endpoint)));
}
rtx::Profile Window::profile_from_form() const {
  rtx::Profile profile;
  profile.name = profiles_->currentText().isEmpty() ? "Профіль" : profiles_->currentText().toStdString();
  if (gpu_->currentIndex() < 0 && !unresolved_gpu_.isEmpty()) {
    profile.gpu_uuid = unresolved_gpu_.toStdString();
  } else {
    profile.gpu_uuid = gpu_->currentData().toString().toStdString();
  }
  profile.scale = scale_->currentData().toInt();
  profile.quality_auto = quality_auto_->isChecked();
  profile.quality = quality_->currentData().toInt();
  profile.bitrate = bitrate_->text().trimmed().isEmpty() ? 0 : bitrate_->text().toLongLong();
  profile.codec = doctor_.value("nvenc_h264").toBool(true) ? "h264" : "hevc";
  QStringList output_filled;
  if (!output_host_->text().trimmed().isEmpty()) {
    output_filled.push_back("remote_host");
  }
  if (output_port_->value() > 0) {
    output_filled.push_back("remote_port");
  }
  if (!output_local_host_->text().trimmed().isEmpty()) {
    output_filled.push_back("local_host");
  }
  if (output_local_port_->value() > 0) {
    output_filled.push_back("local_port");
  }
  if (!output_path_->text().trimmed().isEmpty()) {
    output_filled.push_back("path");
  }
  if (!output_sdp_->text().trimmed().isEmpty()) {
    output_filled.push_back("sdp");
  }
  if (!output_app_->text().trimmed().isEmpty()) {
    output_filled.push_back("app");
  }
  if (!output_playpath_->text().trimmed().isEmpty()) {
    output_filled.push_back("playpath");
  }
  if (!output_directory_->text().trimmed().isEmpty()) {
    output_filled.push_back("directory");
  }
  const QJsonObject output_mode = rtx::gui::mode_for_protocol(modes_, "output", output_mode_->currentData().toString(), output_filled);
  if (!output_mode.isEmpty()) {
    profile.output.protocol = output_mode.value("protocol").toString().toStdString();
    profile.output.role = output_mode.value("role").toString().toStdString();
    const QString container = output_mode.value("container").toString();
    if (profile.output.protocol == "rtp") {
      profile.output.payload = container == "rtp" ? profile.codec : "mpegts";
    }
    if (output_path_->isVisible()) {
      profile.output.path = rtx::gui::utf8_from_qstring(output_path_->text().trimmed());
    }
    if (output_sdp_->isVisible()) {
      profile.output.sdp = rtx::gui::utf8_from_qstring(output_sdp_->text().trimmed());
    }
    if (output_app_->isVisible()) {
      profile.output.app = output_app_->text().trimmed().toStdString();
    }
    if (output_playpath_->isVisible()) {
      profile.output.playpath = output_playpath_->text().trimmed().toStdString();
    }
    if (output_directory_->isVisible()) {
      profile.output.directory = rtx::gui::utf8_from_qstring(output_directory_->text().trimmed());
    }
    if (output_rtcp_->isVisible()) {
      profile.output.rtcp_port = output_rtcp_->value() > 0 ? output_rtcp_->value() : -1;
    }
    if (output_rtp_min_->isVisible()) {
      profile.output.rtp_port_min = output_rtp_min_->value() > 0 ? output_rtp_min_->value() : -1;
    }
    if (output_rtp_max_->isVisible()) {
      profile.output.rtp_port_max = output_rtp_max_->value() > 0 ? output_rtp_max_->value() : -1;
    }
    if (output_params_->isVisible()) {
      read_rtx::gui::params_line(output_params_->text(), profile.output.params);
    }
  }
  if (output_host_->isVisible()) {
    profile.output.remote_host = output_host_->text().trimmed().toStdString();
  }
  if (output_port_->isVisible()) {
    profile.output.remote_port = output_port_->value() > 0 ? output_port_->value() : -1;
  }
  if (output_local_host_->isVisible()) {
    profile.output.local_host = output_local_host_->text().trimmed().toStdString();
  }
  if (output_local_port_->isVisible()) {
    profile.output.local_port = output_local_port_->value() > 0 ? output_local_port_->value() : -1;
  }
  QStringList input_filled;
  if (!input_host_->text().trimmed().isEmpty()) {
    input_filled.push_back("remote_host");
  }
  if (input_port_->value() > 0) {
    input_filled.push_back("remote_port");
  }
  if (!input_local_host_->text().trimmed().isEmpty()) {
    input_filled.push_back("local_host");
  }
  if (input_local_port_->value() > 0) {
    input_filled.push_back("local_port");
  }
  if (!input_path_->text().trimmed().isEmpty()) {
    input_filled.push_back("path");
  }
  if (!input_app_->text().trimmed().isEmpty()) {
    input_filled.push_back("app");
  }
  if (!input_playpath_->text().trimmed().isEmpty()) {
    input_filled.push_back("playpath");
  }
  const QJsonObject input_mode = rtx::gui::mode_for_protocol(modes_, "input", input_protocol_->currentData().toString(), input_filled);
  if (!input_mode.isEmpty()) {
    profile.input.protocol = input_mode.value("protocol").toString().toStdString();
    profile.input.role = input_mode.value("role").toString().toStdString();
    if (input_host_->isVisible()) {
      profile.input.remote_host = input_host_->text().trimmed().toStdString();
    }
    if (input_port_->isVisible()) {
      profile.input.remote_port = input_port_->value() > 0 ? input_port_->value() : -1;
    }
    if (input_local_host_->isVisible()) {
      profile.input.local_host = input_local_host_->text().trimmed().toStdString();
    }
    if (input_local_port_->isVisible()) {
      profile.input.local_port = input_local_port_->value() > 0 ? input_local_port_->value() : -1;
    }
    if (input_path_->isVisible()) {
      profile.input.path = rtx::gui::utf8_from_qstring(input_path_->text().trimmed());
    }
    if (input_app_->isVisible()) {
      profile.input.app = input_app_->text().trimmed().toStdString();
    }
    if (input_playpath_->isVisible()) {
      profile.input.playpath = input_playpath_->text().trimmed().toStdString();
    }
    if (input_rtcp_->isVisible()) {
      profile.input.rtcp_port = input_rtcp_->value() > 0 ? input_rtcp_->value() : -1;
    }
    if (input_rtp_min_->isVisible()) {
      profile.input.rtp_port_min = input_rtp_min_->value() > 0 ? input_rtp_min_->value() : -1;
    }
    if (input_rtp_max_->isVisible()) {
      profile.input.rtp_port_max = input_rtp_max_->value() > 0 ? input_rtp_max_->value() : -1;
    }
    if (input_params_->isVisible()) {
      read_rtx::gui::params_line(input_params_->text(), profile.input.params);
    }
  }
  apply_input_rtp_payload(profile);
  return profile;
}
void Window::form_from_profile(const rtx::Profile& profile, const QString& visible_url) {
  const int gpu_index = gpu_->findData(QString::fromStdString(profile.gpu_uuid));
  if (!profile.gpu_uuid.empty() && gpu_index < 0) {
    unresolved_gpu_ = QString::fromStdString(profile.gpu_uuid);
    gpu_->setCurrentIndex(-1);
    gpu_fit_->setText("Збережений GPU відсутній, оберіть інший.");
  } else {
    unresolved_gpu_.clear();
    if (gpu_index >= 0) {
      gpu_->setCurrentIndex(gpu_index);
    }
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
  QString protocol = QString::fromStdString(profile.input.protocol);
  if (protocol.isEmpty()) {
    protocol = visible_url.section("://", 0, 0);
  }
  const int protocol_index = input_protocol_->findData(protocol);
  if (protocol_index >= 0) {
    input_protocol_->setCurrentIndex(protocol_index);
  }
  update_source_mode();
  {
    input_host_->setText(QString::fromStdString(profile.input.remote_host));
    input_port_->setValue(std::max(0, profile.input.remote_port));
    input_local_host_->setText(QString::fromStdString(profile.input.local_host));
    input_local_port_->setValue(std::max(0, profile.input.local_port));
    input_path_->setText(QString::fromStdString(profile.input.path));
    input_app_->setText(QString::fromStdString(profile.input.app));
    input_playpath_->setText(QString::fromStdString(profile.input.playpath));
    input_rtcp_->setValue(std::max(0, profile.input.rtcp_port));
    input_rtp_min_->setValue(std::max(0, profile.input.rtp_port_min));
    input_rtp_max_->setValue(std::max(0, profile.input.rtp_port_max));
    input_params_->setText(rtx::gui::params_line(profile.input.params));
  }
  const int output_index = output_mode_->findData(QString::fromStdString(profile.output.protocol));
  if (output_index >= 0) {
    output_mode_->setCurrentIndex(output_index);
  }
  update_output_fields();
  output_host_->setText(QString::fromStdString(profile.output.remote_host));
  output_port_->setValue(std::max(0, profile.output.remote_port));
  output_local_host_->setText(QString::fromStdString(profile.output.local_host));
  output_local_port_->setValue(std::max(0, profile.output.local_port));
  output_path_->setText(QString::fromStdString(profile.output.path));
  output_sdp_->setText(QString::fromStdString(profile.output.sdp));
  output_app_->setText(QString::fromStdString(profile.output.app));
  output_playpath_->setText(QString::fromStdString(profile.output.playpath));
  output_directory_->setText(QString::fromStdString(profile.output.directory));
  output_rtcp_->setValue(std::max(0, profile.output.rtcp_port));
  output_rtp_min_->setValue(std::max(0, profile.output.rtp_port_min));
  output_rtp_max_->setValue(std::max(0, profile.output.rtp_port_max));
  output_params_->setText(rtx::gui::params_line(profile.output.params));
}
bool Window::write_profile(const QString& path, QString& error, bool remember) {
  rtx::Profile profile = profile_from_form();
  std::string failure = rtx::password_access_error(profile);
  if (!failure.empty()) {
    error = QString::fromStdString(failure);
    return false;
  }
  if (!rtx::save_profile_file(rtx::gui::path_from_qstring(path), profile, failure)) {
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
  if (!rtx::load_profile_file(rtx::gui::path_from_qstring(path), profile, error)) {
    status_->setText(QString::fromStdString(error));
    return;
  }
  if (const std::string denied = rtx::password_access_error(profile); !denied.empty()) {
    status_->setText(QString::fromStdString(denied));
    return;
  }
  loaded_url_ = QString::fromStdString(profile.input.url);
  form_from_profile(profile, loaded_url_);
  profile_path_ = path;
  if (!profile.gpu_uuid.empty() && gpu_->findData(QString::fromStdString(profile.gpu_uuid)) < 0) {
    unresolved_gpu_ = QString::fromStdString(profile.gpu_uuid);
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
