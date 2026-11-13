import unittest
from pathlib import Path
from unittest.mock import patch

import cache_store
import market_cache
from market_cache import reconcile_quotes
from tora_feed import Quote


class MarketCacheTests(unittest.TestCase):
    def test_snapshot_preserves_quote_details_and_timestamp(self):
        folder = Path(__file__).parent / ".build"
        cache_path = folder / "test_unified_market_cache.json"
        try:
            quote = Quote("SH600519", "贵州茅台", "1404.08", "+0.13",
                          turnover=3535000000, bid_levels=((1404.07, 100),),
                          ask_levels=((1404.09, 200),), trading_day="20261112")
            with patch.object(cache_store, "APP_DIR", folder), \
                    patch.object(cache_store, "CACHE_PATH", cache_path):
                market_cache.save_market_cache([quote], "2026-11-12T09:12:00+08:00", "20261112")
                loaded, stamp, day = market_cache.load_market_cache(["SH600519"])
                self.assertEqual(stamp, "2026-11-12T09:12:00+08:00")
                self.assertEqual(day, "20261112")
                self.assertEqual(loaded, [quote])
                self.assertEqual(market_cache.load_market_cache(["SZ000001"])[0], [])
        finally:
            cache_path.unlink(missing_ok=True)

    def test_new_trading_day_blanks_other_quotes_before_merging(self):
        symbols = ["SH000001", "SZ000001"]
        old_index = Quote("SH000001", "上证指数", "4000.00", "+1.00",
                          turnover=100, open_price=3900, high_price=4010,
                          low_price=3890, close_price=3990,
                          bid_levels=((3999, 10),), trading_day="20261112")
        old_stock = Quote("SZ000001", "平安银行", "11.00", "+2.00",
                          turnover=200, open_price=10.5, trading_day="20261112")
        new_index = Quote("SH000001", "上证指数", "4010.00", "+0.25",
                          turnover=300, trading_day="20261113")
        rolled, blanked, merged, day = reconcile_quotes(
            symbols, [old_index, old_stock], "20261112", [new_index, old_stock])
        self.assertTrue(rolled)
        self.assertEqual(day, "20261113")
        self.assertEqual([(q.symbol, q.name) for q in blanked],
                         [(q.symbol, q.name) for q in (old_index, old_stock)])
        for quote in blanked:
            self.assertEqual((quote.price, quote.change_percent, quote.turnover,
                              quote.open_price, quote.high_price, quote.low_price,
                              quote.close_price, quote.bid_levels, quote.ask_levels),
                             ("", "", None, None, None, None, None, (), ()))
        self.assertEqual(merged[0], new_index)
        self.assertEqual(merged[1], blanked[1])
        self.assertEqual(reconcile_quotes(symbols, merged, day, [old_stock])[2], merged)
        login_rollover, login_blank, login_merged, login_day = reconcile_quotes(
            symbols, [old_index, old_stock], "20261112", [], "20261113")
        self.assertTrue(login_rollover)
        self.assertEqual(login_day, "20261113")
        self.assertEqual(login_blank, login_merged)
        self.assertTrue(all(not quote.price for quote in login_blank))


if __name__ == "__main__":
    unittest.main()
