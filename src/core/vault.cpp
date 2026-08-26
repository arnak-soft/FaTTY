#include "core/vault.hpp"

#include "core/util.hpp"

#include <array>
#include <cstring>

#include <openssl/evp.h>
#include <openssl/rand.h>

namespace fatty {
namespace {

constexpr std::array<const char*, 2> kAads = {"fatty-vault", "vps-runner-vault"};
constexpr std::array<const char*, 2> kVerifiers = {"fatty-vault-ok", "vps-runner-vault-ok"};

std::vector<unsigned char> derive_key(const std::string& password, const std::vector<unsigned char>& salt, int iterations) {
  std::vector<unsigned char> key(32);
  if (PKCS5_PBKDF2_HMAC(
          password.data(),
          static_cast<int>(password.size()),
          salt.data(),
          static_cast<int>(salt.size()),
          iterations,
          EVP_sha256(),
          32,
          key.data()) != 1) {
    throw VaultError("KDF failed");
  }
  return key;
}

std::string encrypt_with_key(const std::vector<unsigned char>& key, const std::string& plaintext) {
  if (plaintext.empty()) {
    return "";
  }
  unsigned char nonce[12];
  if (RAND_bytes(nonce, sizeof(nonce)) != 1) {
    throw VaultError("RNG failed");
  }
  std::vector<unsigned char> out(plaintext.size());
  unsigned char tag[16];
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) {
    throw VaultError("cipher ctx");
  }
  int len = 0;
  int ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
  ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr);
  ok = ok && EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce);
  int unused = 0;
  ok = ok && EVP_EncryptUpdate(
                 ctx,
                 nullptr,
                 &unused,
                 reinterpret_cast<const unsigned char*>(kAads[0]),
                 static_cast<int>(std::strlen(kAads[0])));
  ok = ok && EVP_EncryptUpdate(
                 ctx,
                 out.data(),
                 &len,
                 reinterpret_cast<const unsigned char*>(plaintext.data()),
                 static_cast<int>(plaintext.size()));
  int flen = 0;
  ok = ok && EVP_EncryptFinal_ex(ctx, out.data() + len, &flen);
  ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag);
  EVP_CIPHER_CTX_free(ctx);
  if (!ok) {
    throw VaultError("encrypt failed");
  }
  out.resize(static_cast<std::size_t>(len + flen));
  std::vector<unsigned char> packed;
  packed.insert(packed.end(), nonce, nonce + 12);
  packed.insert(packed.end(), out.begin(), out.end());
  packed.insert(packed.end(), tag, tag + 16);
  return "v1:" + b64_encode(packed.data(), packed.size());
}

std::string decrypt_with_key(const std::vector<unsigned char>& key, const std::string& token) {
  if (token.empty()) {
    return "";
  }
  if (token.rfind("v1:", 0) != 0) {
    throw VaultError("Неизвестный формат секрета");
  }
  auto raw = b64_decode(token.substr(3));
  if (raw.size() < 13) {
    throw VaultError("Повреждённый секрет");
  }
  const unsigned char* nonce = raw.data();
  const unsigned char* blob = raw.data() + 12;
  const std::size_t blob_len = raw.size() - 12;
  if (blob_len < 16) {
    throw VaultError("Повреждённый секрет");
  }
  const std::size_t ct_len = blob_len - 16;
  const unsigned char* tag = blob + ct_len;
  std::string last_error;
  for (const char* aad : kAads) {
    std::vector<unsigned char> out(ct_len);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
      continue;
    }
    int len = 0;
    int ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr);
    ok = ok && EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce);
    int unused = 0;
    ok = ok && EVP_DecryptUpdate(
                   ctx,
                   nullptr,
                   &unused,
                   reinterpret_cast<const unsigned char*>(aad),
                   static_cast<int>(std::strlen(aad)));
    if (ct_len > 0) {
      ok = ok && EVP_DecryptUpdate(ctx, out.data(), &len, blob, static_cast<int>(ct_len));
    }
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, const_cast<unsigned char*>(tag));
    int flen = 0;
    ok = ok && EVP_DecryptFinal_ex(ctx, out.data() + len, &flen);
    EVP_CIPHER_CTX_free(ctx);
    if (ok) {
      out.resize(static_cast<std::size_t>(len + flen));
      return std::string(reinterpret_cast<char*>(out.data()), out.size());
    }
    last_error = "gcm";
  }
  throw VaultError("Не удалось расшифровать секрет");
}

}  // namespace

VaultMeta SessionVault::create(const std::string& password, int min_len) {
  if (static_cast<int>(password.size()) < min_len) {
    throw VaultError("Мастер-пароль не короче " + std::to_string(min_len) + " символов");
  }
  std::vector<unsigned char> salt(16);
  if (RAND_bytes(salt.data(), 16) != 1) {
    throw VaultError("RNG failed");
  }
  auto key = derive_key(password, salt, kKdfIterations);
  VaultMeta meta;
  meta.salt = b64_encode(salt.data(), salt.size());
  meta.verifier = encrypt_with_key(key, kVerifierPlain);
  meta.iterations = kKdfIterations;
  meta.kdf = kKdfName;
  key_ = std::move(key);
  meta_ = meta;
  has_meta_ = true;
  return meta;
}

bool SessionVault::unlock(const std::string& password, const VaultMeta& meta) {
  try {
    auto salt = b64_decode(meta.salt);
    int iterations = meta.iterations > 0 ? meta.iterations : kKdfIterations;
    auto key = derive_key(password, salt, iterations);
    auto plain = decrypt_with_key(key, meta.verifier);
    bool ok = false;
    for (const char* v : kVerifiers) {
      if (plain == v) {
        ok = true;
        break;
      }
    }
    if (!ok) {
      return false;
    }
    key_ = std::move(key);
    meta_ = meta;
    has_meta_ = true;
    return true;
  } catch (...) {
    return false;
  }
}

std::string SessionVault::encrypt_secret(const std::string& plaintext) const {
  if (key_.empty()) {
    throw VaultLocked("Хранилище заблокировано");
  }
  return encrypt_with_key(key_, plaintext);
}

std::string SessionVault::decrypt_secret(const std::string& token) const {
  if (key_.empty()) {
    throw VaultLocked("Хранилище заблокировано");
  }
  if (token.empty()) {
    return "";
  }
  return decrypt_with_key(key_, token);
}

void SessionVault::lock() {
  secure_clear(key_);
  has_meta_ = false;
}

}  // namespace fatty
