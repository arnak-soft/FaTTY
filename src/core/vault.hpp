#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace fatty {

inline constexpr int kMinPasswordLen = 8;
inline constexpr int kMinPasswordLenRelaxed = 4;
inline constexpr int kKdfIterations = 600000;
inline constexpr const char* kKdfName = "pbkdf2-sha256";
inline constexpr const char* kVerifierPlain = "fatty-vault-ok";

class VaultError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class VaultLocked : public VaultError {
 public:
  using VaultError::VaultError;
};

struct VaultMeta {
  std::string salt;
  std::string verifier;
  int iterations = kKdfIterations;
  std::string kdf = kKdfName;
};

class SessionVault {
 public:
  bool unlocked() const { return !key_.empty(); }
  const VaultMeta* meta() const { return has_meta_ ? &meta_ : nullptr; }

  VaultMeta create(const std::string& password, int min_len = kMinPasswordLen);
  bool unlock(const std::string& password, const VaultMeta& meta);
  std::string encrypt_secret(const std::string& plaintext) const;
  std::string decrypt_secret(const std::string& token) const;
  void lock();

 private:
  std::vector<unsigned char> key_;
  VaultMeta meta_;
  bool has_meta_ = false;
};

}  // namespace fatty
