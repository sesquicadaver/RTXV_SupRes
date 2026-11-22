#include "window_helpers.hpp"

#include <QSpinBox>

namespace rtx::gui {

void configure_port(QSpinBox* spin) {
  spin->setRange(0, 65535);
  spin->setSpecialValueText("—");
}

QString params_line(const std::vector<std::pair<std::string, std::string>>& params) {
  QStringList parts;
  for (const auto& param : params) {
    parts.push_back(QString::fromStdString(param.first + "=" + param.second));
  }
  return parts.join('&');
}

void read_params_line(const QString& text, std::vector<std::pair<std::string, std::string>>& params) {
  const QStringList parts = text.split('&', Qt::SkipEmptyParts);
  for (const QString& piece : parts) {
    const int eq = piece.indexOf('=');
    if (eq > 0) {
      params.emplace_back(piece.left(eq).trimmed().toStdString(), piece.mid(eq + 1).trimmed().toStdString());
    }
  }
}

QStringList field_names(const QStringList& required, const QStringList& optional) {
  QStringList fields = required;
  for (const QString& name : optional) {
    if (!fields.contains(name)) {
      fields.push_back(name);
    }
  }
  return fields;
}

QStringList names_of(const QJsonArray& items) {
  QStringList names;
  for (const QJsonValue& item : items) {
    names.push_back(item.toString());
  }
  return names;
}

QStringList union_fields(const QJsonArray& modes, const QString& direction, const QString& protocol) {
  QStringList fields;
  for (const QJsonValue& value : modes) {
    const QJsonObject mode = value.toObject();
    if (mode.value("direction").toString() != direction || mode.value("protocol").toString() != protocol) {
      continue;
    }
    for (const QString& name :
         field_names(names_of(mode.value("required").toArray()), names_of(mode.value("optional").toArray()))) {
      if (!fields.contains(name)) {
        fields.push_back(name);
      }
    }
  }
  return fields;
}

QJsonObject mode_for_protocol(const QJsonArray& modes, const QString& direction, const QString& protocol,
                              const QStringList& filled) {
  QJsonObject fallback;
  QJsonObject chosen;
  int best = -1;
  for (const QJsonValue& value : modes) {
    const QJsonObject mode = value.toObject();
    if (mode.value("direction").toString() != direction || mode.value("protocol").toString() != protocol) {
      continue;
    }
    if (fallback.isEmpty()) {
      fallback = mode;
    }
    const QStringList required = names_of(mode.value("required").toArray());
    bool ready = true;
    for (const QString& name : required) {
      if (!filled.contains(name)) {
        ready = false;
        break;
      }
    }
    if (ready && required.size() > best) {
      best = required.size();
      chosen = mode;
    }
  }
  return chosen.isEmpty() ? fallback : chosen;
}

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

QString vram_text(qulonglong bytes) {
  if (bytes == 0) {
    return "VRAM невідомий";
  }
  const double gib = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
  return gib >= 1.0 ? QString::number(gib, 'f', 1) + " ГБ" : QString::number(bytes / (1024 * 1024)) + " МБ";
}

bool list_has(const QStringList& values, const QString& name) { return values.contains(name); }

void show_row(QFormLayout* form, QWidget* field, bool visible) {
  if (form == nullptr || field == nullptr) {
    return;
  }
  form->setRowVisible(field, visible);
}

void rename_row(QFormLayout* form, QWidget* field, const QString& text) {
  if (auto* label = qobject_cast<QLabel*>(form->labelForField(field))) {
    label->setText(text);
  }
}

}  // namespace rtx::gui
