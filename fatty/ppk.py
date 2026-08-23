"""Convert OpenSSH private keys to unencrypted PuTTY PPK v2 (no puttygen)."""

from __future__ import annotations

import base64
import hashlib
import hmac
import struct
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import dsa, ec, ed25519, rsa
from cryptography.hazmat.primitives.serialization import load_ssh_private_key


class PPKError(Exception):
    pass


def _ssh_string(data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + data


def _ssh_mpint(value: int) -> bytes:
    if value == 0:
        return struct.pack(">I", 0)
    length = (value.bit_length() + 7) // 8
    raw = value.to_bytes(length, "big")
    if raw[0] & 0x80:
        raw = b"\x00" + raw
    return _ssh_string(raw)


def _b64_lines(data: bytes, width: int = 64) -> list[str]:
    text = base64.b64encode(data).decode("ascii")
    return [text[i : i + width] for i in range(0, len(text), width)] or [""]


def _mac_v2(algo: str, encryption: str, comment: str, pub: bytes, priv: bytes) -> str:
    mac_key = hashlib.sha1(b"putty-private-key-file-mac-key").digest()
    preimage = (
        _ssh_string(algo.encode("utf-8"))
        + _ssh_string(encryption.encode("utf-8"))
        + _ssh_string(comment.encode("utf-8"))
        + _ssh_string(pub)
        + _ssh_string(priv)
    )
    return hmac.new(mac_key, preimage, hashlib.sha1).hexdigest()


def _encode_ppk(algo: str, comment: str, pub: bytes, priv: bytes) -> str:
    encryption = "none"
    lines = [
        f"PuTTY-User-Key-File-2: {algo}",
        f"Encryption: {encryption}",
        f"Comment: {comment}",
    ]
    pub_lines = _b64_lines(pub)
    lines.append(f"Public-Lines: {len(pub_lines)}")
    lines.extend(pub_lines)
    priv_lines = _b64_lines(priv)
    lines.append(f"Private-Lines: {len(priv_lines)}")
    lines.extend(priv_lines)
    lines.append(f"Private-MAC: {_mac_v2(algo, encryption, comment, pub, priv)}")
    return "\n".join(lines) + "\n"


def _blobs_for_key(key) -> tuple[str, bytes, bytes]:
    if isinstance(key, ed25519.Ed25519PrivateKey):
        seed = key.private_bytes_raw()
        pub_raw = key.public_key().public_bytes_raw()
        algo = "ssh-ed25519"
        pub = _ssh_string(algo.encode()) + _ssh_string(pub_raw)
        # PuTTY: LE fixed-length integer (= SSH string of 32 seed bytes).
        priv = _ssh_string(seed)
        return algo, pub, priv

    if isinstance(key, rsa.RSAPrivateKey):
        numbers = key.private_numbers()
        pub_numbers = numbers.public_numbers
        algo = "ssh-rsa"
        pub = (
            _ssh_string(algo.encode())
            + _ssh_mpint(pub_numbers.e)
            + _ssh_mpint(pub_numbers.n)
        )
        p, q = numbers.p, numbers.q
        iqmp = pow(q, -1, p)
        priv = _ssh_mpint(numbers.d) + _ssh_mpint(p) + _ssh_mpint(q) + _ssh_mpint(iqmp)
        return algo, pub, priv

    if isinstance(key, ec.EllipticCurvePrivateKey):
        curve = key.curve
        if isinstance(curve, ec.SECP256R1):
            algo, curve_id = "ecdsa-sha2-nistp256", "nistp256"
        elif isinstance(curve, ec.SECP384R1):
            algo, curve_id = "ecdsa-sha2-nistp384", "nistp384"
        elif isinstance(curve, ec.SECP521R1):
            algo, curve_id = "ecdsa-sha2-nistp521", "nistp521"
        else:
            raise PPKError(f"ECDSA-кривая не поддерживается для PuTTY: {curve.name}")
        pub_bytes = key.public_key().public_bytes(
            serialization.Encoding.X962,
            serialization.PublicFormat.UncompressedPoint,
        )
        pub = (
            _ssh_string(algo.encode())
            + _ssh_string(curve_id.encode())
            + _ssh_string(pub_bytes)
        )
        priv = _ssh_mpint(key.private_numbers().private_value)
        return algo, pub, priv

    if isinstance(key, dsa.DSAPrivateKey):
        raise PPKError("DSA-ключи для PuTTY не поддерживаются. Укажите .ppk или пароль.")

    raise PPKError(f"Тип ключа не поддерживается для PuTTY: {type(key).__name__}")


def openssh_to_ppk_text(source: Path, *, passphrase: bytes | None = None) -> str:
    try:
        data = source.read_bytes()
    except OSError as exc:
        raise PPKError(f"Не удалось прочитать ключ: {exc}") from exc
    try:
        key = load_ssh_private_key(data, password=passphrase)
    except TypeError as exc:
        # Encrypted key, no passphrase given.
        raise PPKError(
            "SSH-ключ защищён passphrase.\n"
            "Сохраните .ppk через PuTTYgen или укажите пароль VPS."
        ) from exc
    except ValueError as exc:
        raise PPKError(f"Не удалось разобрать SSH-ключ: {exc}") from exc

    comment = source.name
    algo, pub, priv = _blobs_for_key(key)
    return _encode_ppk(algo, comment, pub, priv)


def write_openssh_as_ppk(source: Path, dest: Path, *, passphrase: bytes | None = None) -> Path:
    text = openssh_to_ppk_text(source, passphrase=passphrase)
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(text, encoding="utf-8", newline="\n")
    return dest
