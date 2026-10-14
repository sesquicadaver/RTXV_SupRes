#pragma once

#include <memory>
#include <string>

namespace rtx {

// Облікові дані джерела не входять у файл профілю. Реалізація пише їх у сховище ОС.
class SecretStore {
 public:
  virtual ~SecretStore() = default;
  virtual bool save(const std::string& id, const std::string& secret, std::string& error) = 0;
  virtual bool load(const std::string& id, std::string& secret, std::string& error) = 0;
  virtual bool remove(const std::string& id, std::string& error) = 0;
};

std::unique_ptr<SecretStore> system_secret_store();

}  // namespace rtx
