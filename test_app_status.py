import unittest
import json
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

import config
from app import (MAX_HEIGHT, Widget, _compact_number, _display_change, _display_price,
                 _price, _site_state_from_feed_status)


class SiteStatusTests(unittest.TestCase):
    def test_connection_transitions(self):
        self.assertEqual(_site_state_from_feed_status("正在连接行情前置"), "站点连接中")
        self.assertEqual(_site_state_from_feed_status("已连接，正在登录"), "站点已连接")
        self.assertEqual(_site_state_from_feed_status("实时行情已订阅"), "站点已连接")
        self.assertEqual(_site_state_from_feed_status("连接断开：1，等待重连"), "站点已断开")
        self.assertEqual(_site_state_from_feed_status("登录失败：2"), "站点登录失败")

    def test_news_refresh_tick_reschedules_both_sources(self):
        widget = Widget.__new__(Widget)
        widget.settings = {"refresh_seconds": 600}
        widget.refresh = Mock()
        after = Mock()
        widget.root = SimpleNamespace(after=after)
        widget._tick()
        widget.refresh.assert_called_once_with()
        after.assert_called_once_with(600000, widget._tick)

    def test_missing_market_values_except_change_render_as_dash(self):
        self.assertEqual((_display_price(""), _display_change(""),
                          _display_change("--"), _price(None),
                          _compact_number(None)), ("—", "", "", "—", "—"))
        self.assertEqual(_display_change("+1.25"), "+1.25%")

    def test_follow_toggle_persists_and_preserves_quote_list(self):
        widget = Widget.__new__(Widget)
        widget.settings = {"symbols": [
            {"symbol": "SH600519", "followed": True},
            {"symbol": "SZ000001", "followed": True}]}
        widget._draw = Mock()
        with patch("app.config.save") as save:
            widget._toggle_follow("SZ000001")
            self.assertEqual(config.followed_codes(widget.settings), {"SH600519"})
            widget._toggle_follow("SZ000001")
            self.assertEqual(config.followed_codes(widget.settings),
                             {"SH600519", "SZ000001"})
            self.assertEqual(save.call_count, 2)
        self.assertEqual(config.symbol_codes(widget.settings),
                         ["SH600519", "SZ000001"])

    def test_max_height_fits_three_full_dashboard_cards(self):
        widget = Widget.__new__(Widget)
        widget.width, widget.height, widget.page_scroll = 332, MAX_HEIGHT, 0
        widget.quotes = [SimpleNamespace(symbol=str(i)) for i in range(5)]
        widget.settings = {"symbols": [
            {"symbol": str(i), "followed": True} for i in range(5)]}
        widget.hot = {"weibo": SimpleNamespace(items=[None]*5)}
        widget.headlines = SimpleNamespace(items=[None]*5)
        self.assertLessEqual(max(y+height for _, _, y, _, height
                                 in widget._dashboard_layout())+14, MAX_HEIGHT)
        self.assertEqual(widget._dashboard_scroll_limit(), 0)

    def test_existing_hot_source_setting_migrates_to_official_band(self):
        path = Path(__file__).parent / ".build" / "test_config_hot_source.json"
        try:
            path.write_text(json.dumps({
                "hot_url": "https://solarhell.github.io/hot-search-crawler/latest.json",
                "symbols": [{"symbol": "SH600519", "followed": False}]}), encoding="utf-8")
            with patch.object(config, "CONFIG_PATH", path):
                settings = config.load()
            self.assertEqual(settings["hot_url"],
                             "https://weibo.com/ajax/statuses/hot_band")
            self.assertEqual(settings["symbols"],
                             [{"symbol": "SH600519", "followed": False}])
            self.assertNotIn("followed_symbols", settings)
        finally:
            path.unlink(missing_ok=True)

    def test_old_follow_list_is_folded_into_symbols(self):
        path = Path(__file__).parent / ".build" / "test_config_symbols.json"
        try:
            path.write_text(json.dumps({
                "symbols": ["SH600519", "SZ000001"],
                "followed_symbols": ["SZ000001"]}), encoding="utf-8")
            with patch.object(config, "CONFIG_PATH", path):
                settings = config.load()
            self.assertEqual(settings["symbols"], [
                {"symbol": "SH600519", "followed": False},
                {"symbol": "SZ000001", "followed": True}])
            self.assertNotIn("followed_symbols", settings)
            saved = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(saved["symbols"], settings["symbols"])
            self.assertNotIn("followed_symbols", saved)
        finally:
            path.unlink(missing_ok=True)


if __name__ == "__main__":
    unittest.main()
