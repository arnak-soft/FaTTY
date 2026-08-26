#include "putty/ppk.hpp"

#include "core/util.hpp"

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <openssl/params.h>
#include <openssl/sha.h>

#include <fstream>
#include <vector>

namespace fatty {
namespace {

std::string ssh_string(const unsigned char* data, std::size_t len) {
  std::string out;
  unsigned char hdr[4] = {
      static_cast<unsigned char>((len >> 24) & 0xff),
      static_cast<unsigned char>((len >> 16) & 0xff),
      static_cast<unsigned char>((len >> 8) & 0xff),
      static_cast<unsigned char>(len & 0xff),
  };
  out.append(reinterpret_cast<char*>(hdr), 4);
  out.append(reinterpret_cast<const char*>(data), len);
  return out;
}

std::string ssh_string(const std::string& s) {
  return ssh_string(reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

std::string ssh_mpint(const BIGNUM* bn) {
  if (!bn || BN_is_zero(bn)) {
    return ssh_string(nullptr, 0);
  }
  int bytes = BN_num_bytes(bn);
  std::vector<unsigned char> raw(static_cast<std::size_t>(bytes) + 1, 0);
  BN_bn2bin(bn, raw.data() + 1);
  std::size_t off = 1;
  if (raw[1] & 0x80) {
    off = 0;
    ++bytes;
  }
  return ssh_string(raw.data() + off, static_cast<std::size_t>(bytes));
}

std::vector<std::string> b64_lines(const std::string& data, int width = 64) {
  auto text = b64_encode(reinterpret_cast<const unsigned char*>(data.data()), data.size());
  std::vector<std::string> lines;
  if (text.empty()) {
    lines.emplace_back("");
    return lines;
  }
  for (std::size_t i = 0; i < text.size(); i += static_cast<std::size_t>(width)) {
    lines.push_back(text.substr(i, static_cast<std::size_t>(width)));
  }
  return lines;
}

std::string mac_v2(const std::string& algo, const std::string& encryption, const std::string& comment,
                   const std::string& pub, const std::string& priv) {
  unsigned char mac_key[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const unsigned char*>("putty-private-key-file-mac-key"), 32, mac_key);
  auto pre = ssh_string(algo) + ssh_string(encryption) + ssh_string(comment) + ssh_string(pub) + ssh_string(priv);
  unsigned char mac[SHA_DIGEST_LENGTH];
  unsigned int mac_len = 0;
  HMAC(EVP_sha1(), mac_key, SHA_DIGEST_LENGTH, reinterpret_cast<const unsigned char*>(pre.data()), pre.size(), mac,
       &mac_len);
  static const char* hex = "0123456789abcdef";
  std::string out;
  out.resize(mac_len * 2);
  for (unsigned int i = 0; i < mac_len; ++i) {
    out[i * 2] = hex[(mac[i] >> 4) & 0xf];
    out[i * 2 + 1] = hex[mac[i] & 0xf];
  }
  return out;
}

std::string encode_ppk(const std::string& algo, const std::string& comment, const std::string& pub,
                       const std::string& priv) {
  std::string encryption = "none";
  std::string text = "PuTTY-User-Key-File-2: " + algo + "\nEncryption: " + encryption + "\nComment: " + comment + "\n";
  auto pub_lines = b64_lines(pub);
  text += "Public-Lines: " + std::to_string(pub_lines.size()) + "\n";
  for (const auto& line : pub_lines) text += line + "\n";
  auto priv_lines = b64_lines(priv);
  text += "Private-Lines: " + std::to_string(priv_lines.size()) + "\n";
  for (const auto& line : priv_lines) text += line + "\n";
  text += "Private-MAC: " + mac_v2(algo, encryption, comment, pub, priv) + "\n";
  return text;
}

BIGNUM* get_bn(EVP_PKEY* pkey, const char* name) {
  BIGNUM* bn = nullptr;
  if (EVP_PKEY_get_bn_param(pkey, name, &bn) != 1) {
    return nullptr;
  }
  return bn;
}

void blobs_for_key(EVP_PKEY* pkey, std::string& algo, std::string& pub, std::string& priv) {
  if (EVP_PKEY_is_a(pkey, "ED25519")) {
    unsigned char seed[32];
    unsigned char pub_raw[32];
    size_t slen = sizeof(seed);
    size_t plen = sizeof(pub_raw);
    if (EVP_PKEY_get_raw_private_key(pkey, seed, &slen) != 1 ||
        EVP_PKEY_get_raw_public_key(pkey, pub_raw, &plen) != 1) {
      throw PPKError("Не удалось разобрать Ed25519-ключ");
    }
    algo = "ssh-ed25519";
    pub = ssh_string(algo) + ssh_string(pub_raw, plen);
    priv = ssh_string(seed, slen);
    return;
  }
  if (EVP_PKEY_is_a(pkey, "RSA")) {
    BIGNUM *n = get_bn(pkey, OSSL_PKEY_PARAM_RSA_N);
    BIGNUM *e = get_bn(pkey, OSSL_PKEY_PARAM_RSA_E);
    BIGNUM *d = get_bn(pkey, OSSL_PKEY_PARAM_RSA_D);
    BIGNUM *p = get_bn(pkey, OSSL_PKEY_PARAM_RSA_FACTOR1);
    BIGNUM *q = get_bn(pkey, OSSL_PKEY_PARAM_RSA_FACTOR2);
    if (!n || !e || !d || !p || !q) {
      BN_free(n); BN_free(e); BN_free(d); BN_free(p); BN_free(q);
      throw PPKError("Не удалось разобрать RSA-ключ");
    }
    algo = "ssh-rsa";
    pub = ssh_string(algo) + ssh_mpint(e) + ssh_mpint(n);
    BIGNUM* iqmp = BN_new();
    BN_CTX* ctx = BN_CTX_new();
    BN_mod_inverse(iqmp, q, p, ctx);
    priv = ssh_mpint(d) + ssh_mpint(p) + ssh_mpint(q) + ssh_mpint(iqmp);
    BN_CTX_free(ctx);
    BN_free(iqmp);
    BN_free(n); BN_free(e); BN_free(d); BN_free(p); BN_free(q);
    return;
  }
  if (EVP_PKEY_is_a(pkey, "EC") || EVP_PKEY_is_a(pkey, "id-ecPublicKey")) {
    char gname[64] = {};
    size_t glen = 0;
    EVP_PKEY_get_group_name(pkey, gname, sizeof(gname), &glen);
    std::string curve = gname;
    std::string curve_id;
    if (curve == "prime256v1" || curve == "P-256") {
      algo = "ecdsa-sha2-nistp256";
      curve_id = "nistp256";
    } else if (curve == "secp384r1" || curve == "P-384") {
      algo = "ecdsa-sha2-nistp384";
      curve_id = "nistp384";
    } else if (curve == "secp521r1" || curve == "P-521") {
      algo = "ecdsa-sha2-nistp521";
      curve_id = "nistp521";
    } else {
      throw PPKError("ECDSA-кривая не поддерживается для PuTTY: " + curve);
    }
    BIGNUM* priv_bn = get_bn(pkey, OSSL_PKEY_PARAM_PRIV_KEY);
    size_t pub_len = 0;
    EVP_PKEY_get_octet_string_param(pkey, OSSL_PKEY_PARAM_ENCODED_PUBLIC_KEY, nullptr, 0, &pub_len);
    std::vector<unsigned char> pub_bytes(pub_len);
    EVP_PKEY_get_octet_string_param(pkey, OSSL_PKEY_PARAM_ENCODED_PUBLIC_KEY, pub_bytes.data(), pub_len, &pub_len);
    pub = ssh_string(algo) + ssh_string(curve_id) + ssh_string(pub_bytes.data(), pub_len);
    priv = ssh_mpint(priv_bn);
    BN_free(priv_bn);
    return;
  }
  if (EVP_PKEY_is_a(pkey, "DSA")) {
    throw PPKError("DSA-ключи для PuTTY не поддерживаются. Укажите .ppk или пароль.");
  }
  throw PPKError("Тип ключа не поддерживается для PuTTY");
}

}  // namespace

std::string openssh_to_ppk_text(const std::filesystem::path& source) {
  BIO* bio = BIO_new_file(source.string().c_str(), "rb");
  if (!bio) {
    throw PPKError("Не удалось прочитать ключ");
  }
  EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (!pkey) {
    throw PPKError(
        "Не удалось разобрать SSH-ключ. Если он защищён passphrase — сохраните .ppk через PuTTYgen "
        "или укажите пароль VPS.");
  }
  std::string algo, pub, priv;
  try {
    blobs_for_key(pkey, algo, pub, priv);
  } catch (...) {
    EVP_PKEY_free(pkey);
    throw;
  }
  EVP_PKEY_free(pkey);
  return encode_ppk(algo, source.filename().string(), pub, priv);
}

std::filesystem::path write_openssh_as_ppk(const std::filesystem::path& source, const std::filesystem::path& dest) {
  auto text = openssh_to_ppk_text(source);
  std::filesystem::create_directories(dest.parent_path());
  std::ofstream out(dest, std::ios::binary | std::ios::trunc);
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  return dest;
}

}  // namespace fatty
