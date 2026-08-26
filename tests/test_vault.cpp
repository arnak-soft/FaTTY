#include "tests/test.hpp"
#include "core/vault.hpp"

using namespace fatty;
using namespace fatty::test;

void fatty::test::test_vault() {
  SessionVault vault;
  auto meta = vault.create("correct-horse");
  expect(vault.unlocked(), "vault unlocked after create");
  expect(!meta.salt.empty(), "salt present");
  expect(meta.verifier.rfind("v1:", 0) == 0, "verifier token");
  expect(meta.iterations == kKdfIterations, "iterations");

  auto token = vault.encrypt_secret("secret-value");
  expect(token.rfind("v1:", 0) == 0, "secret token");
  expect(vault.decrypt_secret(token) == "secret-value", "roundtrip decrypt");
  expect(vault.encrypt_secret("").empty(), "empty encrypt");

  SessionVault other;
  expect(other.unlock("correct-horse", meta), "unlock ok");
  expect(other.decrypt_secret(token) == "secret-value", "unlock decrypt");
  expect(!other.unlock("wrong-password", meta), "bad password");

  SessionVault locked;
  bool threw = false;
  try {
    locked.encrypt_secret("x");
  } catch (const VaultLocked&) {
    threw = true;
  }
  expect(threw, "locked encrypt throws");

  bool short_threw = false;
  try {
    vault.create("short");
  } catch (const VaultError&) {
    short_threw = true;
  }
  expect(short_threw, "short password rejected");
}
