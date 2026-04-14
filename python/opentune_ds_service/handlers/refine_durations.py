"""Mock refine_durations: return incremental phoneme patches (tick-aware)."""

from __future__ import annotations

import copy
import json
from typing import Any, Dict, List, Tuple

MIN_PHONEME_SEC = 0.02
SR = 44100


def _tick_to_sec(tick: int) -> float:
    return float(tick) / float(SR)


def _sec_to_tick(sec: float) -> int:
    return int(round(float(sec) * SR))


def _phoneme_bounds(p: Dict[str, Any]) -> Tuple[float, float]:
    if "start_tick" in p and "end_tick" in p:
        return _tick_to_sec(int(p["start_tick"])), _tick_to_sec(int(p["end_tick"]))
    return float(p["start_sec"]), float(p["end_sec"])


def _segment_bounds(seg: Dict[str, Any]) -> Tuple[float, float]:
    if "start_tick" in seg and "end_tick" in seg:
        return _tick_to_sec(int(seg["start_tick"])), _tick_to_sec(int(seg["end_tick"]))
    return float(seg["start_sec"]), float(seg["end_sec"])


def _phoneme_changed(before: Dict[str, Any], after: Dict[str, Any]) -> bool:
    bs0, bs1 = _phoneme_bounds(before)
    as0, as1 = _phoneme_bounds(after)
    if abs(bs0 - as0) > 1e-6 or abs(bs1 - as1) > 1e-6:
        return True
    if before.get("token", "") != after.get("token", ""):
        return True
    if bool(before.get("lock_left", False)) != bool(after.get("lock_left", False)):
        return True
    if bool(before.get("lock_right", False)) != bool(after.get("lock_right", False)):
        return True
    return False


def refine_phoneme_durations(req: Dict[str, Any]) -> Tuple[Dict[str, Any], int]:
    clip = req.get("clip") or {}
    request_id = int(req.get("request_id", 0))
    clip_id = int(clip.get("clip_id", 0))
    clip_generation = int(clip.get("clip_generation", 0))
    doc_rev = int(req.get("document_revision", 0))

    if req.get("schema_version") != "opentune.ds.v1":
        return {
            "schema_version": "opentune.ds.v1",
            "status": "error",
            "document_revision": doc_rev,
            "request_id": request_id,
            "clip_id": clip_id,
            "clip_generation": clip_generation,
            "error": "bad schema_version",
        }, 200

    segments: List[Dict[str, Any]] = list(req.get("segments") or [])
    phonemes: List[Dict[str, Any]] = [dict(p) for p in (req.get("phonemes") or [])]
    if not phonemes:
        return {
            "schema_version": "opentune.ds.v1",
            "status": "ok",
            "document_revision": doc_rev,
            "request_id": request_id,
            "clip_id": clip_id,
            "clip_generation": clip_generation,
            "error": None,
            "patched_phonemes": [],
            "affected_object_ids": [],
            "conflicts": [],
            "provenance": {"source": "model", "impl": "mock_empty"},
        }, 200

    originals = {int(p.get("id", 0)): copy.deepcopy(p) for p in phonemes if int(p.get("id", 0)) != 0}

    for seg in segments:
        s0, s1 = _segment_bounds(seg)
        if s1 <= s0 + 1e-9:
            continue
        seg_id = int(seg.get("id", 0))

        inside: List[int] = []
        for i, p in enumerate(phonemes):
            st, en = _phoneme_bounds(p)
            if st >= s0 - 1e-9 and en <= s1 + 1e-9:
                inside.append(i)
        if not inside:
            continue

        chain_start, _ = _phoneme_bounds(phonemes[inside[0]])
        _, chain_end = _phoneme_bounds(phonemes[inside[-1]])
        target = s1 - s0
        cur_span = chain_end - chain_start
        if cur_span < MIN_PHONEME_SEC:
            return {
                "schema_version": "opentune.ds.v1",
                "status": "error",
                "document_revision": doc_rev,
                "request_id": request_id,
                "clip_id": clip_id,
                "clip_generation": clip_generation,
                "error": json.dumps({"code": "INSUFFICIENT_DOFS", "segment_id": seg_id}),
            }, 200

        stretchable = 0.0
        for i in inside:
            p = phonemes[i]
            st, en = _phoneme_bounds(p)
            dur = en - st
            if dur < MIN_PHONEME_SEC - 1e-9:
                return {
                    "schema_version": "opentune.ds.v1",
                    "status": "error",
                    "document_revision": doc_rev,
                    "request_id": request_id,
                    "clip_id": clip_id,
                    "clip_generation": clip_generation,
                    "error": json.dumps({"code": "INSUFFICIENT_DOFS", "segment_id": seg_id}),
                }, 200
            if not p.get("lock_right"):
                stretchable += dur

        if stretchable < 1e-9:
            if abs(cur_span - target) > 0.05:
                return {
                    "schema_version": "opentune.ds.v1",
                    "status": "error",
                    "document_revision": doc_rev,
                    "request_id": request_id,
                    "clip_id": clip_id,
                    "clip_generation": clip_generation,
                    "error": json.dumps({"code": "INSUFFICIENT_DOFS", "segment_id": seg_id}),
                }, 200
            continue

        scale = target / cur_span
        offset = s0 - chain_start

        for k, i in enumerate(inside):
            p = phonemes[i]
            st, en = _phoneme_bounds(p)
            dur = en - st

            new_st = st if bool(p.get("lock_left")) else st + offset
            if bool(p.get("lock_right")):
                new_en = en + offset
            else:
                new_en = new_st + max(MIN_PHONEME_SEC, dur * scale)

            p["start_tick"] = _sec_to_tick(new_st)
            p["end_tick"] = _sec_to_tick(new_en)
            p["start_sec"] = new_st
            p["end_sec"] = new_en

            if k + 1 < len(inside):
                nxt = phonemes[inside[k + 1]]
                nxt["start_tick"] = int(p["end_tick"])
                nxt["start_sec"] = float(p["end_sec"])

        last = phonemes[inside[-1]]
        if not bool(last.get("lock_right")):
            _, last_end = _phoneme_bounds(last)
            new_end = min(s1, float(last_end))
            last["end_tick"] = _sec_to_tick(new_end)
            last["end_sec"] = new_end

    patched: List[Dict[str, Any]] = []
    affected_ids: List[int] = []
    per_item: List[Dict[str, Any]] = []
    for p in phonemes:
        pid = int(p.get("id", 0))
        before = originals.get(pid)
        if before is None or not _phoneme_changed(before, p):
            continue
        st, en = _phoneme_bounds(p)
        patched.append(
            {
                "id": pid,
                "start_tick": int(p.get("start_tick", _sec_to_tick(st))),
                "end_tick": int(p.get("end_tick", _sec_to_tick(en))),
                "start_sec": float(st),
                "end_sec": float(en),
                "token": p.get("token", ""),
                "lock_left": bool(p.get("lock_left", False)),
                "lock_right": bool(p.get("lock_right", False)),
            }
        )
        affected_ids.append(pid)
        per_item.append({"phoneme_id": pid, "source": "model"})

    return {
        "schema_version": "opentune.ds.v1",
        "status": "ok",
        "document_revision": doc_rev,
        "request_id": request_id,
        "clip_id": clip_id,
        "clip_generation": clip_generation,
        "error": None,
        "patched_phonemes": patched,
        "affected_object_ids": affected_ids,
        "conflicts": [],
        "provenance": {"source": "model", "impl": "mock_proportional_refine", "per_item": per_item},
    }, 200


def handle_post_refine(body: str) -> Tuple[str, int, str]:
    try:
        req = json.loads(body)
    except json.JSONDecodeError:
        return (
            json.dumps({"schema_version": "opentune.ds.v1", "status": "error", "error": "invalid json"}),
            400,
            "application/json",
        )
    resp, code = refine_phoneme_durations(req)
    return json.dumps(resp), code, "application/json"
