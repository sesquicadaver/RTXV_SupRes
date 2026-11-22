#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <QFormLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <QWidget>

namespace rtx::gui {

void configure_port(class QSpinBox* spin);

QString params_line(const std::vector<std::pair<std::string, std::string>>& params);
void read_params_line(const QString& text, std::vector<std::pair<std::string, std::string>>& params);
QStringList field_names(const QStringList& required, const QStringList& optional);
QStringList names_of(const QJsonArray& items);
QStringList union_fields(const QJsonArray& modes, const QString& direction, const QString& protocol);
QJsonObject mode_for_protocol(const QJsonArray& modes, const QString& direction, const QString& protocol,
                              const QStringList& filled);
QString state_text(const QString& state);
QString vram_text(qulonglong bytes);
bool list_has(const QStringList& values, const QString& name);
void show_row(QFormLayout* form, QWidget* field, bool visible);
void rename_row(QFormLayout* form, QWidget* field, const QString& text);

inline std::string utf8_from_qstring(const QString& text) { return std::string(text.toUtf8().constData()); }

inline std::filesystem::path path_from_qstring(const QString& text) {
#if defined(_WIN32)
  return std::filesystem::path(text.toStdWString());
#else
  return std::filesystem::path(text.toUtf8().constData());
#endif
}

}  // namespace rtx::gui
