#include "secrets.hpp"

#include <array>
#include <memory>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <wincred.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace rtx {
namespace {

#if defined(_WIN32)

std::wstring wide(const std::string& text) {
  if (text.empty()) {
    return {};
  }
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
  return out;
}

class WindowsSecretStore final : public SecretStore {
 public:
  bool save(const std::string& id, const std::string& secret, std::string& error) override {
    const std::wstring target = wide("rtx-vsr-proxy/" + id);
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t*>(target.c_str());
    credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
    credential.CredentialBlob = reinterpret_cast<BYTE*>(const_cast<char*>(secret.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    const std::wstring user = wide("rtx-vsr-proxy");
    credential.UserName = const_cast<wchar_t*>(user.c_str());
    if (!CredWriteW(&credential, 0)) {
      error = "сховище облікових даних Windows відхилило запис";
      return false;
    }
    error.clear();
    return true;
  }

  bool load(const std::string& id, std::string& secret, std::string& error) override {
    const std::wstring target = wide("rtx-vsr-proxy/" + id);
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential) || credential == nullptr) {
      error = "у сховищі ОС немає облікових даних джерела";
      return false;
    }
    secret.assign(reinterpret_cast<const char*>(credential->CredentialBlob), credential->CredentialBlobSize);
    CredFree(credential);
    error.clear();
    return true;
  }

  bool remove(const std::string& id, std::string& error) override {
    const std::wstring target = wide("rtx-vsr-proxy/" + id);
    CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
    error.clear();
    return true;
  }
};

#else

class SecretToolStore final : public SecretStore {
 public:
  bool save(const std::string& id, const std::string& secret, std::string& error) override {
    const std::string output = run({"secret-tool", "store", "--label=RTX VSR Proxy", "service", "rtx-vsr-proxy",
                                    "account", id},
                                   secret, error);
    return error.empty() && output.empty();
  }

  bool load(const std::string& id, std::string& secret, std::string& error) override {
    secret = run({"secret-tool", "lookup", "service", "rtx-vsr-proxy", "account", id}, {}, error);
    if (!error.empty()) {
      return false;
    }
    while (!secret.empty() && (secret.back() == '\n' || secret.back() == '\r')) {
      secret.pop_back();
    }
    return true;
  }

  bool remove(const std::string& id, std::string& error) override {
    run({"secret-tool", "clear", "service", "rtx-vsr-proxy", "account", id}, {}, error);
    error.clear();
    return true;
  }

 private:
  static std::string run(const std::vector<std::string>& args, const std::string& input, std::string& error) {
    int input_pipe[2] = {-1, -1};
    int output_pipe[2] = {-1, -1};
    if (pipe(input_pipe) != 0 || pipe(output_pipe) != 0) {
      error = "не вдалося відкрити канал до сховища ОС";
      return {};
    }
    const pid_t child = fork();
    if (child < 0) {
      error = "не вдалося запустити сховище ОС";
      return {};
    }
    if (child == 0) {
      dup2(input_pipe[0], STDIN_FILENO);
      dup2(output_pipe[1], STDOUT_FILENO);
      dup2(output_pipe[1], STDERR_FILENO);
      close(input_pipe[0]);
      close(input_pipe[1]);
      close(output_pipe[0]);
      close(output_pipe[1]);
      std::vector<char*> argv;
      argv.reserve(args.size() + 1);
      for (const std::string& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
      }
      argv.push_back(nullptr);
      execvp(argv[0], argv.data());
      _exit(127);
    }
    close(input_pipe[0]);
    close(output_pipe[1]);
    std::size_t written = 0;
    while (written < input.size()) {
      const ssize_t chunk = write(input_pipe[1], input.data() + written, input.size() - written);
      if (chunk < 0) {
        break;
      }
      written += static_cast<std::size_t>(chunk);
    }
    close(input_pipe[1]);
    std::string output;
    std::array<char, 256> buffer{};
    while (true) {
      const ssize_t chunk = read(output_pipe[0], buffer.data(), buffer.size());
      if (chunk <= 0) {
        break;
      }
      output.append(buffer.data(), static_cast<std::size_t>(chunk));
    }
    close(output_pipe[0]);
    int status = 0;
    waitpid(child, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      error = "сховище облікових даних ОС недоступне";
      return {};
    }
    error.clear();
    return output;
  }
};

#endif

}  // namespace

std::unique_ptr<SecretStore> system_secret_store() {
#if defined(_WIN32)
  return std::make_unique<WindowsSecretStore>();
#else
  return std::make_unique<SecretToolStore>();
#endif
}

}  // namespace rtx
