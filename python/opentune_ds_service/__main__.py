"""Stdio RPC service for OpenTune Phase 1 bridge."""

from __future__ import annotations

import argparse
import base64
import json
import sys
from typing import Any, Dict

from opentune_ds_service.handlers.refine_durations import refine_phoneme_durations


def _read_exact(stream, n: int) -> bytes | None:
    buf = b""
    while len(buf) < n:
        chunk = stream.read(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


def _write_framed(stream, obj: Dict[str, Any]) -> None:
    raw = json.dumps(obj, ensure_ascii=False).encode("utf-8")
    stream.write(len(raw).to_bytes(4, "big"))
    stream.write(raw)
    stream.flush()


def _framed_stdio_loop() -> None:
    stdin = sys.stdin.buffer
    stdout = sys.stdout.buffer
    while True:
        hdr = _read_exact(stdin, 4)
        if hdr is None or len(hdr) < 4:
            break
        n = int.from_bytes(hdr, "big")
        if n <= 0 or n > 64 * 1024 * 1024:
            break
        body = _read_exact(stdin, n)
        if body is None:
            break
        try:
            req = json.loads(body.decode("utf-8"))
        except json.JSONDecodeError:
            _write_framed(
                stdout,
                {
                    "schema_version": "opentune.ds.v1",
                    "status": "error",
                    "document_revision": 0,
                    "request_id": 0,
                    "clip_id": 0,
                    "clip_generation": 0,
                    "error": "invalid json",
                },
            )
            continue
        if req.get("task") != "refine_durations":
            clip = req.get("clip") or {}
            _write_framed(
                stdout,
                {
                    "schema_version": "opentune.ds.v1",
                    "status": "error",
                    "document_revision": int(req.get("document_revision", 0)),
                    "request_id": int(req.get("request_id", 0)),
                    "clip_id": int((clip or {}).get("clip_id", 0)),
                    "clip_generation": int((clip or {}).get("clip_generation", 0)),
                    "error": "unsupported task",
                },
            )
            continue
        resp, _ = refine_phoneme_durations(req)
        _write_framed(stdout, resp)


def main() -> None:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--request-b64", default="")
    parser.add_argument("--framed-stdio", action="store_true")
    args, _ = parser.parse_known_args()

    if args.request_b64:
        req = json.loads(base64.b64decode(args.request_b64.encode("ascii")).decode("utf-8"))
        if req.get("task") == "refine_durations":
            resp, _ = refine_phoneme_durations(req)
        else:
            resp = {
                "schema_version": "opentune.ds.v1",
                "status": "error",
                "document_revision": int(req.get("document_revision", 0)),
                "request_id": int(req.get("request_id", 0)),
                "clip_id": int((req.get("clip") or {}).get("clip_id", 0)),
                "clip_generation": int((req.get("clip") or {}).get("clip_generation", 0)),
                "error": "unsupported task",
            }
        sys.stdout.write(json.dumps(resp, ensure_ascii=False))
        sys.stdout.flush()
        return

    if args.framed_stdio:
        _framed_stdio_loop()
        return

    _framed_stdio_loop()


if __name__ == "__main__":
    main()
