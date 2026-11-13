"""One atomic cache file for stocks, Weibo, and CLS headlines."""

from __future__ import annotations

import json
import threading
from datetime import datetime
from pathlib import Path
from typing import Any

from config import APP_DIR

CACHE_PATH = APP_DIR / "cache.json"
_lock = threading.RLock()
_PLATFORMS = ("stocks", "weibo", "cls")


def _cache_time(value: str) -> str:
    try:
        stamp = datetime.fromisoformat(value.replace("Z", "+00:00"))
        return stamp.astimezone().strftime("%Y%m%d-%H:%M:%S")
    except (TypeError, ValueError):
        return datetime.now().astimezone().strftime("%Y%m%d-%H:%M:%S")


def parse_cache_time(value: str) -> str:
    """Return an ISO timestamp for the existing UI formatters."""
    try:
        return datetime.strptime(value, "%Y%m%d-%H:%M:%S").astimezone().isoformat()
    except (TypeError, ValueError):
        return ""


def _legacy_records() -> list[dict[str, Any]]:
    records = []
    legacy = {
        "stocks": (APP_DIR / "market_cache.json", "saved_at", "quotes"),
        "weibo": (APP_DIR / "weibo_cache.json", "fetched_at", "items"),
        "cls": (APP_DIR / "cls_headlines.json", "fetched_at", "items"),
    }
    for platform, (path, time_key, items_key) in legacy.items():
        try:
            raw = json.loads(path.read_text(encoding="utf-8"))
            if not isinstance(raw, dict) or not isinstance(raw.get(items_key), list):
                continue
            time = _cache_time(raw.get(time_key, ""))
            record: dict[str, Any] = {"platform": platform, "time": time,
                                      "items": raw[items_key]}
            if platform == "stocks":
                record["items"] = [
                    {"symbol": row["symbol"], "name": row.get("name") or row["symbol"],
                     "price": "", "change_percent": "", "trading_day": ""}
                    for row in raw[items_key]
                    if isinstance(row, dict) and isinstance(row.get("symbol"), str)
                ]
                record["trading_day"] = ""
            records.append(record)
        except (OSError, ValueError, TypeError):
            continue
    if not any(row["platform"] == "weibo" for row in records):
        try:
            raw = json.loads((APP_DIR / "hot_cache.json").read_text(encoding="utf-8"))
            board = raw.get("weibo")
            if isinstance(board, dict) and isinstance(board.get("items"), list):
                records.append({"platform": "weibo",
                                "time": _cache_time(board.get("fetched_at", "")),
                                "items": board["items"]})
        except (OSError, ValueError, TypeError, AttributeError):
            pass
    return records


def _read() -> list[dict[str, Any]]:
    try:
        data = json.loads(CACHE_PATH.read_text(encoding="utf-8"))
        if isinstance(data, list):
            records = [row for row in data if isinstance(row, dict) and
                       row.get("platform") in _PLATFORMS and
                       isinstance(row.get("items"), list)]
            repaired = False
            for record in records:
                if record["platform"] != "stocks":
                    continue
                items = record["items"]
                missing = [item for item in items if isinstance(item, dict) and
                           "trading_day" not in item]
                if missing:
                    record["items"] = [
                        ({"symbol": item["symbol"],
                          "name": item.get("name") or item["symbol"],
                          "price": "", "change_percent": "", "trading_day": ""}
                         if "trading_day" not in item else item)
                        for item in items if isinstance(item, dict) and
                        isinstance(item.get("symbol"), str)
                    ]
                    if len(missing) == len(items):
                        record["trading_day"] = ""
                    repaired = True
            if repaired:
                _write(records)
            return records
    except (OSError, ValueError, TypeError):
        pass
    records = _legacy_records()
    if records:
        _write(records)
    return records


def _write(records: list[dict[str, Any]]) -> None:
    try:
        CACHE_PATH.parent.mkdir(parents=True, exist_ok=True)
        temporary = CACHE_PATH.with_suffix(".tmp")
        temporary.write_text(json.dumps(records, ensure_ascii=False), encoding="utf-8")
        temporary.replace(CACHE_PATH)
    except OSError:
        pass


def load_record(platform: str) -> dict[str, Any] | None:
    with _lock:
        return next((row for row in _read() if row.get("platform") == platform), None)


def save_record(platform: str, stamp: str, items: list[dict[str, Any]],
                trading_day: str = "", source_time: str = "",
                origin: str = "") -> None:
    if platform not in _PLATFORMS:
        raise ValueError("未知缓存平台")
    with _lock:
        records = [row for row in _read() if row.get("platform") != platform]
        record: dict[str, Any] = {"platform": platform, "time": _cache_time(stamp),
                                  "items": items}
        if platform == "stocks":
            record["trading_day"] = trading_day
        if platform == "weibo" and source_time:
            record["source_time"] = _cache_time(source_time)
        if platform == "weibo" and origin:
            record["origin"] = origin
        records.append(record)
        records.sort(key=lambda row: _PLATFORMS.index(row["platform"]))
        _write(records)
