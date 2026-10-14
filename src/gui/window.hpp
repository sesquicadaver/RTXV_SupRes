#pragma once

#include "profile.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QProcess>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTableWidget;
class QWidget;

class Window : public QMainWindow {
  Q_OBJECT

 public:
  explicit Window(QWidget* parent = nullptr);
  ~Window() override;

 private:
  struct BenchJob {
    int scale = 2;
    int quality = 1;
    QString gpu_uuid;
    QString gpu_name;
  };

  void build();
  void reload_modes();
  void reload_gpus();
  void apply_modes(const QJsonArray& modes);
  void apply_gpus(const QJsonObject& doctor);
  void fill_roles();
  void update_source_mode();
  void update_output_fields();
  void update_consumer();
  void refresh_profiles();
  void load_profile(const QString& path);
  bool write_profile(const QString& path, QString& error, bool remember);
  void check_selected_gpu();
  void flush_benchmark(const QString& uuid);
  rtx::Profile profile_from_form() const;
  void form_from_profile(const rtx::Profile& profile, const QString& visible_url);
  void mark_dirty();
  void start_proxy();
  void stop_proxy();
  void read_proxy();
  void toggle_benchmark();
  void start_benchmark();
  void run_next_benchmark();
  void finish_benchmark();
  void apply_benchmark_row();
  QString proxy_program() const;
  QString secret_id(const QString& path) const;

  QComboBox* profiles_ = nullptr;
  QPushButton* save_ = nullptr;
  QPushButton* save_as_ = nullptr;
  QLabel* dirty_ = nullptr;
  QRadioButton* source_url_ = nullptr;
  QRadioButton* source_fields_ = nullptr;
  QLineEdit* url_ = nullptr;
  QComboBox* input_protocol_ = nullptr;
  QLineEdit* input_host_ = nullptr;
  QLineEdit* input_path_ = nullptr;
  QSpinBox* input_port_ = nullptr;
  QComboBox* input_role_ = nullptr;
  QLabel* input_port_caption_ = nullptr;
  QComboBox* output_mode_ = nullptr;
  QLineEdit* output_host_ = nullptr;
  QLineEdit* output_path_ = nullptr;
  QSpinBox* output_port_ = nullptr;
  QLabel* output_port_caption_ = nullptr;
  QLineEdit* consumer_ = nullptr;
  QComboBox* gpu_ = nullptr;
  QPushButton* refresh_gpu_ = nullptr;
  QLabel* gpu_fit_ = nullptr;
  QCheckBox* quality_auto_ = nullptr;
  QComboBox* scale_ = nullptr;
  QComboBox* quality_ = nullptr;
  QLineEdit* bitrate_ = nullptr;
  QPushButton* start_ = nullptr;
  QLabel* status_ = nullptr;
  QPlainTextEdit* journal_ = nullptr;
  QPushButton* benchmark_toggle_ = nullptr;
  QWidget* benchmark_body_ = nullptr;
  QLineEdit* bench_file_ = nullptr;
  QSpinBox* bench_budget_ = nullptr;
  QSpinBox* bench_warmup_ = nullptr;
  QCheckBox* compare_gpus_ = nullptr;
  QPushButton* bench_run_ = nullptr;
  QProgressBar* bench_progress_ = nullptr;
  QTableWidget* bench_table_ = nullptr;
  QPushButton* bench_apply_ = nullptr;
  QProcess* proxy_ = nullptr;
  QProcess* bench_ = nullptr;
  QJsonObject doctor_;
  QJsonArray modes_;
  QJsonArray bench_card_;
  QString bench_card_uuid_;
  QString profile_path_;
  QString saved_fingerprint_;
  QString loaded_url_;
  QString loaded_credential_;
  bool running_ = false;
  QList<BenchJob> bench_queue_;
  int bench_index_ = 0;
  QString bench_profile_;
};
