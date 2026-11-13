import unittest
from types import SimpleNamespace

from tora_feed import ToraFeed, normalize_symbol, quote_from_market_data, validate_front


class ToraAdapterTests(unittest.TestCase):
    def test_symbols_cover_three_exchanges(self):
        self.assertEqual(normalize_symbol("SH600519"), ("SH", "600519"))
        self.assertEqual(normalize_symbol("000001.SZ"), ("SZ", "000001"))
        self.assertEqual(normalize_symbol("BJ430047"), ("BJ", "430047"))
        with self.assertRaises(ValueError):
            normalize_symbol("00000000")

    def test_market_data_change_uses_previous_close(self):
        data = SimpleNamespace(LastPrice=11.0, PreClosePrice=10.0,
                               SecurityName="测试股", TradingDay=b"20261113")
        quote = quote_from_market_data(data, "SH600000")
        self.assertEqual((quote.price, quote.change_percent), ("11.00", "+10.00"))
        self.assertEqual(quote.trading_day, "20261113")
        self.assertIsNone(quote_from_market_data(SimpleNamespace(LastPrice=float("inf"), PreClosePrice=10, SecurityName=""), "SH600000"))

    def test_market_data_preserves_detail_and_five_levels(self):
        fields = dict(LastPrice=11.0, PreClosePrice=10.0, SecurityName="测试股",
                      OpenPrice=10.1, HighestPrice=11.5, LowestPrice=9.9,
                      ClosePrice=10.8, UpperLimitPrice=12.0, LowerLimitPrice=8.0,
                      Volume=120000, Turnover=1320000)
        for i in range(1, 6):
            fields[f"BidPrice{i}"] = 11.0-i*.01
            fields[f"BidVolume{i}"] = i*100
            fields[f"AskPrice{i}"] = 11.0+i*.01
            fields[f"AskVolume{i}"] = i*200
        quote = quote_from_market_data(SimpleNamespace(**fields), "SH600000")
        self.assertEqual((quote.open_price, quote.high_price, quote.low_price,
                          quote.close_price, quote.upper_limit, quote.lower_limit),
                         (10.1, 11.5, 9.9, 10.8, 12.0, 8.0))
        self.assertEqual((quote.volume, quote.turnover), (120000, 1320000))
        self.assertEqual(quote.bid_levels[4], (10.95, 500))
        self.assertEqual(quote.ask_levels[0], (11.01, 200))

    def test_front_address_validation(self):
        self.assertEqual(validate_front("tcp://127.0.0.1:6402"), "tcp://127.0.0.1:6402")
        with self.assertRaises(ValueError):
            validate_front("https://example.com")

    def test_watchlist_changes_reuse_live_subscription(self):
        calls = []
        class Api:
            def SubscribeMarketData(self, codes, exchange):
                calls.append(("subscribe", codes, exchange))
                return 0

            def UnSubscribeMarketData(self, codes, exchange):
                calls.append(("unsubscribe", codes, exchange))
                return 0

        feed = ToraFeed("tcp://127.0.0.1:6402", ["SH600519"], lambda _: None)
        api = Api()
        feed.api = api
        feed.logged_in = True
        self.assertTrue(feed.add_symbol("SZ000001"))
        self.assertFalse(feed.add_symbol("SZ000001"))
        self.assertTrue(feed.remove_symbol("SZ000001"))
        self.assertFalse(feed.remove_symbol("SZ000001"))
        self.assertIs(feed.api, api)
        self.assertEqual([call[0] for call in calls], ["subscribe", "unsubscribe"])
        self.assertEqual(calls[0][1], [b"000001"])
        self.assertEqual(feed.watchlist, [("SH", "600519")])


if __name__ == "__main__":
    unittest.main()
