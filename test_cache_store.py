import json
import unittest
from pathlib import Path
from unittest.mock import patch

import cache_store


class UnifiedCacheTests(unittest.TestCase):
    def test_all_platforms_share_one_file(self):
        folder = Path(__file__).parent / ".build"
        path = folder / "test_unified_cache.json"
        try:
            with patch.object(cache_store, "APP_DIR", folder), \
                    patch.object(cache_store, "CACHE_PATH", path):
                cache_store.save_record("stocks", "2026-11-13T09:30:00+08:00",
                                        [{"symbol": "SH000001", "trading_day": "20261113"}],
                                        "20261113")
                cache_store.save_record("weibo", "2026-11-13T00:24:46+08:00",
                                        [{"title": "热搜"}],
                                        source_time="2026-11-13T00:20:00+08:00",
                                        origin="官网")
                cache_store.save_record("cls", "2026-11-13T08:00:00+08:00",
                                        [{"title": "头条"}])
                records = json.loads(path.read_text(encoding="utf-8"))
                self.assertEqual([row["platform"] for row in records],
                                 ["stocks", "weibo", "cls"])
                self.assertEqual(records[1], {"platform": "weibo",
                                              "time": "20261113-00:24:46",
                                              "items": [{"title": "热搜"}],
                                              "source_time": "20261113-00:20:00",
                                              "origin": "官网"})
                self.assertEqual(cache_store.load_record("stocks")["trading_day"],
                                 "20261113")
        finally:
            path.unlink(missing_ok=True)

    def test_legacy_files_migrate_once(self):
        folder = Path(__file__).parent / ".build"
        path = folder / "test_migrated_cache.json"
        legacy = [folder / name for name in
                  ("market_cache.json", "weibo_cache.json", "cls_headlines.json")]
        try:
            for file, payload in zip(legacy, (
                {"saved_at": "2026-11-12T09:30:00+08:00", "quotes": [{"symbol": "SH000001"}]},
                {"fetched_at": "2026-11-12T09:30:00+08:00", "items": [{"title": "微博"}]},
                {"fetched_at": "2026-11-12T09:30:00+08:00", "items": [{"title": "财联社"}]},
            )):
                file.write_text(json.dumps(payload, ensure_ascii=False), encoding="utf-8")
            with patch.object(cache_store, "APP_DIR", folder), \
                    patch.object(cache_store, "CACHE_PATH", path):
                migrated = cache_store.load_record("stocks")
                self.assertEqual(migrated["trading_day"], "")
                self.assertEqual(migrated["items"][0],
                                 {"symbol": "SH000001", "name": "SH000001",
                                  "price": "", "change_percent": "", "trading_day": ""})
                self.assertEqual(len(json.loads(path.read_text(encoding="utf-8"))), 3)
                for file in legacy:
                    file.unlink()
                self.assertEqual(cache_store.load_record("weibo")["items"][0]["title"],
                                 "微博")
        finally:
            path.unlink(missing_ok=True)
            for file in legacy:
                file.unlink(missing_ok=True)

    def test_undated_market_values_are_cleared(self):
        folder = Path(__file__).parent / ".build"
        path = folder / "test_undated_cache.json"
        try:
            path.write_text(json.dumps([{
                "platform": "stocks", "time": "20261113-08:00:00",
                "trading_day": "20261113", "items": [{
                    "symbol": "SH000001", "name": "上证指数",
                    "price": "4000.00", "change_percent": "+1.00",
                    "turnover": 100,
                }],
            }], ensure_ascii=False), encoding="utf-8")
            with patch.object(cache_store, "APP_DIR", folder), \
                    patch.object(cache_store, "CACHE_PATH", path):
                record = cache_store.load_record("stocks")
                self.assertEqual(record["trading_day"], "")
                self.assertEqual(record["items"][0]["name"], "上证指数")
                self.assertEqual(record["items"][0]["price"], "")
                self.assertNotIn("turnover", record["items"][0])
        finally:
            path.unlink(missing_ok=True)


if __name__ == "__main__":
    unittest.main()
