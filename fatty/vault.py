"""Master-password vault: PBKDF2-SHA256 + AES-256-GCM.

The key lives only in process memory after the user types the password.
It is never written to disk.
"""

from __future__ import annotations

import base64
import os
from dataclasses import dataclass

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC

MIN_PASSWORD_LEN = 8
KDF_ITERATIONS = 600_000
KDF_NAME = "pbkdf2-sha256"
VERIFIER_PLAIN = "fatty-vault-ok"
_AAD = b"fatty-vault"
_COMPAT_AADS = (b"fatty-vault", b"vps-runner-vault")
_COMPAT_VERIFIERS = frozenset({"fatty-vault-ok", "vps-runner-vault-ok"})


class VaultError(Exception):
    pass


class VaultLocked(VaultError):
    pass


@dataclass
class VaultMeta:
    salt: str
    verifier: str
    iterations: int = KDF_ITERATIONS
    kdf: str = KDF_NAME


def _b64e(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")


def _b64d(text: str) -> bytes:
    return base64.b64decode(text.encode("ascii"))


def _derive_key(password: str, salt: bytes, iterations: int) -> bytes:
    kdf = PBKDF2HMAC(
        algorithm=hashes.SHA256(),
        length=32,
        salt=salt,
        iterations=iterations,
    )
    return kdf.derive(password.encode("utf-8"))


def _encrypt(key: bytes, plaintext: str) -> str:
    if plaintext == "":
        return ""
    nonce = os.urandom(12)
    token = AESGCM(key).encrypt(nonce, plaintext.encode("utf-8"), _AAD)
    return "v1:" + _b64e(nonce + token)


def _decrypt(key: bytes, token: str) -> str:
    if not token:
        return ""
    if not token.startswith("v1:"):
        raise VaultError("Неизвестный формат секрета")
    raw = _b64d(token[3:])
    if len(raw) < 13:
        raise VaultError("Повреждённый секрет")
    nonce, blob = raw[:12], raw[12:]
    last_error: Exception | None = None
    for aad in _COMPAT_AADS:
        try:
            return AESGCM(key).decrypt(nonce, blob, aad).decode("utf-8")
        except Exception as exc:
            last_error = exc
    raise VaultError("Не удалось расшифровать секрет") from last_error


class SessionVault:
    def __init__(self) -> None:
        self.key: bytes | None = None
        self.meta: VaultMeta | None = None

    @property
    def unlocked(self) -> bool:
        return self.key is not None

    def create(self, password: str) -> VaultMeta:
        if len(password) < MIN_PASSWORD_LEN:
            raise VaultError(f"Мастер-пароль не короче {MIN_PASSWORD_LEN} символов")
        salt = os.urandom(16)
        key = _derive_key(password, salt, KDF_ITERATIONS)
        meta = VaultMeta(
            salt=_b64e(salt),
            verifier=_encrypt(key, VERIFIER_PLAIN),
            iterations=KDF_ITERATIONS,
            kdf=KDF_NAME,
        )
        self.key = key
        self.meta = meta
        return meta

    def unlock(self, password: str, meta: VaultMeta) -> bool:
        try:
            salt = _b64d(meta.salt)
            key = _derive_key(password, salt, int(meta.iterations or KDF_ITERATIONS))
            if _decrypt(key, meta.verifier) not in _COMPAT_VERIFIERS:
                return False
        except Exception:
            return False
        self.key = key
        self.meta = meta
        return True

    def encrypt_secret(self, plaintext: str) -> str:
        if self.key is None:
            raise VaultLocked("Хранилище заблокировано")
        return _encrypt(self.key, plaintext)

    def decrypt_secret(self, token: str) -> str:
        if self.key is None:
            raise VaultLocked("Хранилище заблокировано")
        if not token:
            return ""
        return _decrypt(self.key, token)

    def lock(self) -> None:
        self.key = None
