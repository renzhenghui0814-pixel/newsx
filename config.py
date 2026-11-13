"""Local preferences. Secrets are never shipped in the repository."""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any

APP_DIR = Path(os.environ.get("APPDATA", Path.home())) / "NewsXWidget"
CONFIG_PATH = APP_DIR / "config.json"

DEFAULTS: dict[str, Any] = {
    "theme": "dark",
    "opacity": 0.86,
    "always_on_top": True,
    "x": None,
    "y": None,
    "width": 332,
    "height": 390,
    "symbols": [
        {"symbol": "SH600519", "followed": True},
        {"symbol": "SZ000001", "followed": True},
    ],
    "tora_front": "",
    "hot_url": "https://weibo.com/ajax/statuses/hot_band",
    "refresh_seconds": 600,
}


def load() -> dict[str, Any]:
    try:
        user = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
        if not isinstance(user, dict):
            raise ValueError("配置格式错误")
    except (OSError, ValueError):
        user = {}
    old_symbols = user.get("symbols")
    needs_save = ("followed_symbols" in user or
                  (isinstance(old_symbols, list) and
                   any(isinstance(row, str) for row in old_symbols)) or
                  user.get("hot_url") in (
                      "https://solarhell.github.io/hot-search-crawler/latest.json",
                      "https://s.weibo.com/top/summary?cate=realtimehot"))
    settings = {**DEFAULTS, **user}
    if settings["hot_url"] in (
            "https://solarhell.github.io/hot-search-crawler/latest.json",
            "https://s.weibo.com/top/summary?cate=realtimehot"):
        settings["hot_url"] = DEFAULTS["hot_url"]
    settings["theme"] = settings["theme"] if settings["theme"] in ("dark", "light") else "dark"
    try:
        settings["opacity"] = max(0.45, min(1.0, float(settings["opacity"])))
        settings["refresh_seconds"] = max(60, int(settings["refresh_seconds"]))
        settings["width"] = max(332, min(900, int(settings["width"])))
        settings["height"] = max(390, min(960, int(settings["height"])))
    except (TypeError, ValueError):
        settings["opacity"] = DEFAULTS["opacity"]
        settings["refresh_seconds"] = DEFAULTS["refresh_seconds"]
        settings["width"] = DEFAULTS["width"]
        settings["height"] = DEFAULTS["height"]
    rows = settings["symbols"] if isinstance(settings["symbols"], list) else []
    old_followed = settings.get("followed_symbols")
    old_followed_set = ({item for item in old_followed if isinstance(item, str)}
                        if isinstance(old_followed, list) else None)
    symbols = []
    seen = set()
    for row in rows:
        if isinstance(row, str):
            row = {"symbol": row, "followed": (row in old_followed_set
                   if old_followed_set is not None else True)}
        if not isinstance(row, dict) or not isinstance(row.get("symbol"), str):
            continue
        symbol = row["symbol"]
        if symbol in seen:
            continue
        seen.add(symbol)
        symbols.append({"symbol": symbol, "followed": bool(row.get("followed", True))})
    settings["symbols"] = symbols
    settings.pop("followed_symbols", None)
    if needs_save:
        try:
            save(settings)
        except OSError:
            pass
    return settings


def symbol_codes(settings: dict[str, Any]) -> list[str]:
    return [row["symbol"] for row in settings["symbols"]]


def followed_codes(settings: dict[str, Any]) -> set[str]:
    return {row["symbol"] for row in settings["symbols"] if row["followed"]}


def save(settings: dict[str, Any]) -> None:
    CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    temporary = CONFIG_PATH.with_suffix(".tmp")
    temporary.write_text(json.dumps(settings, ensure_ascii=False, indent=2), encoding="utf-8")
    temporary.replace(CONFIG_PATH)
