#!/usr/bin/env python3
"""Hace de Flex Cloud Web contra el servidor REAL de Flex Cloud en modo desarrollo
(cloud/ de Flex Developer Studio, FLEX_ACCOUNT_MODE=dev):

  1. inicia sesión (POST /api/cloud/dev/login -> cookie de la web);
  2. registra la huella de una credencial de dispositivo nueva en ESA cuenta
     (POST /api/cloud/dev/devices: lo que hace Flex Account al aprobar el código
     del reloj). La credencial sale por stdout; nunca se guarda en disco;
  3. sube fotos con miniatura por la API de subidas reanudables, como la web
     (cookie + X-Flex-Cloud: 1, X-Part-SHA256 por parte).

  gal_web_seed.py <puerto> <foto> [<foto>...]  -> imprime: <credencial> <id1> <id2>...
"""
import base64
import hashlib
import http.client
import io
import json
import os
import secrets
import sys

from PIL import Image

PORT = int(sys.argv[1])
COOKIE = None


def call(method, path, body=None, ctype="application/json", extra=None):
    global COOKIE
    c = http.client.HTTPConnection("127.0.0.1", PORT, timeout=30)
    h = {"X-Flex-Cloud": "1"}
    if COOKIE:
        h["Cookie"] = COOKIE
    if body is not None:
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
        h["Content-Type"] = ctype
    if extra:
        h.update(extra)
    c.request(method, "/api/cloud" + path, body=body, headers=h)
    r = c.getresponse()
    data = r.read()
    sc = r.getheader("Set-Cookie")
    if sc:
        COOKIE = sc.split(";")[0]
    c.close()
    try:
        return r.status, json.loads(data)
    except ValueError:
        return r.status, data


def upload(path):
    data = open(path, "rb").read()
    name = os.path.basename(path)
    mime = "image/png" if name.endswith(".png") else "image/jpeg"
    sha = hashlib.sha256(data).hexdigest()
    st, j = call("POST", "/uploads", {"name": name, "size": len(data), "mimeType": mime, "sha256": sha})
    assert st in (200, 201), (st, j)
    up = j["upload"]
    cs = up["chunkSize"]
    for n in range(1, up["totalParts"] + 1):
        part = data[(n - 1) * cs:n * cs]
        st, j = call("PUT", f"/uploads/{up['uploadId']}/parts/{n}", part, "application/octet-stream",
                     {"X-Part-SHA256": hashlib.sha256(part).hexdigest()})
        assert st == 200, (st, j)
    st, j = call("POST", f"/uploads/{up['uploadId']}/complete", {"sha256": sha})
    assert st == 200, (st, j)
    fid = j["file"]["id"]
    if mime == "image/jpeg":  # la miniatura de 320 px que hace la web (thumbs.js)
        im = Image.open(io.BytesIO(data))
        im.thumbnail((320, 320))
        b = io.BytesIO()
        im.convert("RGB").save(b, "JPEG", quality=82)
        st, j = call("PUT", f"/files/{fid}/thumbnail", b.getvalue(), "image/jpeg")
        assert st == 200, (st, j)
    return fid


st, j = call("POST", "/dev/login", {"flexAddress": "ana@flex", "displayName": "Ana"})
assert st == 200, (st, j)
token = base64.urlsafe_b64encode(secrets.token_bytes(32)).decode().rstrip("=")
assert len(token) == 43
st, j = call("POST", "/dev/devices", {"tokenHash": hashlib.sha256(token.encode()).hexdigest(),
                                      "label": "Flex OS Watch Ultra"})
assert st == 200, (st, j)
ids = [upload(p) for p in sys.argv[2:]]
print(token, *ids)
