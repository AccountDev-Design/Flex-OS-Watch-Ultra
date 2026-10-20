#!/usr/bin/env python3
"""
ota_sign.py — claves, firma y manifiesto del OTA de Flex OS Watch Ultra (F7.2).

La referencia no traía ninguna herramienta (ota.md §6). Esta sigue el modelo de
`flexpkg keygen/sign` del ecosistema: ECDSA P-256, firma r||s de 64 B.

Qué se firma (igual que ota_sign_message() en components/watch_ota/ota_core.c):

    flexos-ota-v1\\n<producto>\\n<versión>\\n<tamaño>\\n<sha256 hex de la imagen>

Con eso una firma vieja no sirve para otra versión ni para otro tamaño.

LA CLAVE PRIVADA NUNCA VA AL REPOSITORIO (.gitignore ignora *.pem, *.key, *.p8
y keys/). Esta herramienta se niega a escribirla dentro de un árbol de git.

Uso:
    ota_sign.py keygen  --out ~/flexota/priv.pem
    ota_sign.py pubkey  --key ~/flexota/priv.pem          (cabecera C para ota_trusted_key.h)
    ota_sign.py sign    --key ~/flexota/priv.pem --bin build/flex_os_watch_ultra.bin \\
                        --url https://.../flex_os_watch_ultra.bin [--changelog notas.txt] \\
                        [--manifest ota/manifest.json]
    ota_sign.py verify  --pub <hex 65 B> --bin firmware.bin --manifest ota/manifest.json
"""
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature, encode_dss_signature
from cryptography.exceptions import InvalidSignature

PRODUCT = "flex_os_watch_ultra"
SIGALG = "ecdsa-p256-sha256"
CHIP_ID_ESP32S3 = 0x0009
P256_N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
MIN_BIN = 65536
SLOT = 0x600000  # ota_0/ota_1 en partitions.csv

VER_RE = re.compile(r"^\d+\.\d+\.\d+(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$")


def die(msg):
    print("error: " + msg, file=sys.stderr)
    sys.exit(1)


def inside_git(path):
    d = os.path.dirname(os.path.abspath(path)) or "."
    while not os.path.isdir(d):
        d = os.path.dirname(d)
    r = subprocess.run(["git", "-C", d, "rev-parse", "--is-inside-work-tree"], capture_output=True, text=True)
    return r.returncode == 0 and r.stdout.strip() == "true"


def message(ver, size, sha_hex, product=PRODUCT):
    return ("flexos-ota-v1\n%s\n%s\n%d\n%s" % (product, ver, size, sha_hex)).encode()


def pub_bytes(pub):
    return pub.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)


def sign_raw(priv, msg):
    r, s = decode_dss_signature(priv.sign(msg, ec.ECDSA(hashes.SHA256())))
    if s > P256_N // 2:  # S baja, como FlexOS_Store.cpp
        s = P256_N - s
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def verify_raw(pub65, msg, sig64):
    pub = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), pub65)
    der = encode_dss_signature(int.from_bytes(sig64[:32], "big"), int.from_bytes(sig64[32:], "big"))
    try:
        pub.verify(der, msg, ec.ECDSA(hashes.SHA256()))
        return True
    except InvalidSignature:
        return False


def image_info(data):
    """Producto y versión de la imagen (lo mismo que ota_image_check())."""
    if len(data) < 112 or data[0] != 0xE9:
        die("no es una imagen de aplicación ESP (magic 0xE9)")
    chip = struct.unpack_from("<H", data, 12)[0]
    if chip != CHIP_ID_ESP32S3:
        die("la imagen no es de un ESP32-S3 (chip_id %d)" % chip)
    if struct.unpack_from("<I", data, 32)[0] != 0xABCD5432:
        die("no se encuentra esp_app_desc_t")
    ver = data[48:80].split(b"\0")[0].decode()
    proj = data[80:112].split(b"\0")[0].decode()
    return proj, ver


def load_key(path):
    with open(path, "rb") as f:
        k = serialization.load_pem_private_key(f.read(), password=None)
    if not isinstance(k, ec.EllipticCurvePrivateKey) or k.curve.name != "secp256r1":
        die("la clave no es ECDSA P-256")
    return k


def c_header(pub65):
    rows = []
    for i in range(0, 65, 13):
        rows.append("    " + ", ".join("0x%02x" % b for b in pub65[i:i + 13]) + ",")
    fp = hashlib.sha256(pub65).hexdigest()
    return ("/* Huella SHA-256: %s */\n#define OTA_TRUSTED_KEY_SET 1\n"
            "static const uint8_t kOtaTrustedKey[65] = {\n%s\n};\n" % (fp, "\n".join(rows)))


def cmd_keygen(a):
    if inside_git(a.out):
        die("%s está dentro de un repositorio git: la clave privada nunca va a un repositorio" % a.out)
    if os.path.exists(a.out):
        die("%s ya existe: no se sobrescribe" % a.out)
    k = ec.generate_private_key(ec.SECP256R1())
    pem = k.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                          serialization.NoEncryption())
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    fd = os.open(a.out, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(pem)
    pub = pub_bytes(k.public_key())
    print("clave privada: %s (permisos 600; guárdala fuera del repositorio)" % a.out)
    print("clave pública: %s" % pub.hex())
    print("huella:        %s" % hashlib.sha256(pub).hexdigest())


def cmd_pubkey(a):
    print(c_header(pub_bytes(load_key(a.key).public_key())), end="")


def cmd_sign(a):
    k = load_key(a.key)
    data = open(a.bin, "rb").read()
    proj, ver = image_info(data)
    if proj != PRODUCT:
        die("project_name de la imagen es '%s', no '%s'" % (proj, PRODUCT))
    if not VER_RE.match(ver) or len(ver) > 23:
        die("la versión de la imagen '%s' no es X.Y.Z[-pre] (version.txt)" % ver)
    if not (MIN_BIN <= len(data) <= SLOT):
        die("tamaño %d fuera de [%d, %d]" % (len(data), MIN_BIN, SLOT))
    if not a.url.startswith("https://") or len(a.url) > 255:
        die("la URL tiene que ser https:// y de 255 caracteres como mucho")
    sha = hashlib.sha256(data).hexdigest()
    sig = sign_raw(k, message(ver, len(data), sha))
    assert verify_raw(pub_bytes(k.public_key()), message(ver, len(data), sha), sig)
    block = {
        "version": ver,
        "url": a.url,
        "size": len(data),
        "md5": hashlib.md5(data).hexdigest(),
        "sha256": sha,
        "changelog": open(a.changelog, encoding="utf-8").read().strip() if a.changelog else "",
        "sig": sig.hex(),
        "sigalg": SIGALG,
    }
    man = {}
    if a.manifest and os.path.exists(a.manifest):
        man = json.load(open(a.manifest, encoding="utf-8"))
    man[PRODUCT] = block
    text = json.dumps(man, ensure_ascii=False, indent=2) + "\n"
    if len(text.encode()) > 4095:
        die("el manifiesto pasa de 4095 B (OTA_MANIFEST_MAX): acorta las novedades")
    if a.manifest:
        with open(a.manifest, "w", encoding="utf-8") as f:
            f.write(text)
        print("manifiesto escrito: %s (versión %s, %d B)" % (a.manifest, ver, len(data)))
    else:
        sys.stdout.write(text)


def cmd_verify(a):
    pub = bytes.fromhex(a.pub)
    data = open(a.bin, "rb").read()
    b = json.load(open(a.manifest, encoding="utf-8"))[PRODUCT]
    sha = hashlib.sha256(data).hexdigest()
    ok = verify_raw(pub, message(b["version"], len(data), sha), bytes.fromhex(b["sig"]))
    print("firma %s" % ("válida" if ok else "NO válida"))
    sys.exit(0 if ok else 1)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("keygen")
    s.add_argument("--out", required=True)
    s = sub.add_parser("pubkey")
    s.add_argument("--key", required=True)
    s = sub.add_parser("sign")
    s.add_argument("--key", required=True)
    s.add_argument("--bin", required=True)
    s.add_argument("--url", required=True)
    s.add_argument("--changelog")
    s.add_argument("--manifest")
    s = sub.add_parser("verify")
    s.add_argument("--pub", required=True)
    s.add_argument("--bin", required=True)
    s.add_argument("--manifest", required=True)
    a = p.parse_args()
    {"keygen": cmd_keygen, "pubkey": cmd_pubkey, "sign": cmd_sign, "verify": cmd_verify}[a.cmd](a)


if __name__ == "__main__":
    main()
