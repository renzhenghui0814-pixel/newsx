import unittest
import json
from unittest.mock import patch

import sources
from sources import (HeadlineBoard, HeadlineItem, parse_cls_article,
                     parse_cls_headlines, parse_hot_snapshot, parse_weibo_band,
                     parse_weibo_page,
                     snapshot_age_label)


class SourceParsingTests(unittest.TestCase):
    def test_official_search_preserves_comprehensive_order_and_media(self):
        def blog(name, ident, **extra):
            return {"id": ident, "bid": ident, "user": {"screen_name": name},
                    "text": "第一行<br>第二行", **extra}
        payload = {"data": {"cards": [
            {"mblog": blog("官网首条", "a", isLongText=True,
                            page_info={"type": "video", "page_pic": {
                                "url": "https://wx1.sinaimg.cn/poster.jpg"},
                                "media_info": {"stream_url":
                                    "https://f.video.weibocdn.com/movie.mp4"}})},
            {"mblog": blog("官网第二条", "b", pics=[{"large": {
                "url": "https://wx1.sinaimg.cn/photo.jpg"}}])},
            {"card_group": [{"mblog": blog("官网第三条", "c")}]},
            {"mblog": blog("第四条", "d")}]}}
        posts = sources.parse_weibo_search(payload)
        self.assertEqual([post.title for post in posts],
                         ["官网首条", "官网第二条", "官网第三条"])
        self.assertEqual(posts[0].text, "第一行\n第二行")
        self.assertFalse(posts[0].complete)
        self.assertEqual(posts[0].video_url,
                         "https://f.video.weibocdn.com/movie.mp4")
        self.assertEqual(posts[1].images,
                         ("https://wx1.sinaimg.cn/photo.jpg",))

    def test_official_search_expands_long_text(self):
        preliminary = []
        with patch.object(sources, "_weibo_cookie", return_value="cookie"), \
                patch.object(sources, "_weibo_mobile_json", side_effect=[
                    {"ok": 1, "data": {"cards": [{"mblog": {
                        "id": "abc", "bid": "abc", "user": {"screen_name": "作者"},
                        "text": "折叠", "isLongText": True}}]}},
                    {"ok": 1, "data": {"longTextContent": "完整<br>正文"}}]):
            posts = sources.fetch_weibo_posts("话题", preliminary.append)
        self.assertEqual(preliminary[0][0].text, "折叠")
        self.assertFalse(preliminary[0][0].complete)
        self.assertEqual(posts[0].text, "完整\n正文")
        self.assertTrue(posts[0].complete)

    def test_cls_headlines_keep_top_articles_and_skip_ads(self):
        payload = {"errno": 0, "data": {
            "top_article": [
                {"id": 42, "title": "头条一", "brief": "摘要一",
                 "author": "财联社", "ctime": 1794467366},
                {"id": 43, "title": "广告", "is_ad": 1},
            ],
            "depth_list": [
                {"id": 42, "title": "重复"},
                {"id": 44, "title": "头条二", "brief": "摘要二",
                 "source": "记者", "ctime": 1794467000},
            ],
        }}
        board = parse_cls_headlines(payload)
        self.assertEqual([item.id for item in board.items], [42, 44])
        self.assertEqual(board.items[0].brief, "摘要一")
        self.assertEqual(board.items[1].url, "https://www.cls.cn/detail/44")

    def test_cls_article_extracts_readable_paragraphs(self):
        payload = {"props": {"pageProps": {"articleDetail": {
            "content": "<p>第一段<strong>重点</strong></p><p>第二段</p>"
        }}}}
        page = '<script id="__NEXT_DATA__" type="application/json">' + \
            json.dumps(payload, ensure_ascii=False) + '</script>'
        self.assertEqual(parse_cls_article(page), "第一段重点\n第二段")

    def test_cls_headline_list_flattens_embedded_line_breaks(self):
        payload = {"errno": 0, "data": {"top_article": [
            {"id": 42, "title": "长标题\n第二行", "brief": "第一句\n①第二句\t第三句"}
        ], "depth_list": []}}
        item = parse_cls_headlines(payload).items[0]
        self.assertEqual(item.title, "长标题 第二行")
        self.assertEqual(item.brief, "第一句 ①第二句 第三句")

    def test_cls_fetch_uses_cache_when_endpoint_fails(self):
        cached = HeadlineBoard((HeadlineItem(42, "头条", "摘要", "财联社", 1,
                                             "https://www.cls.cn/detail/42"),),
                               "2026-11-12T09:00:00+08:00")
        with patch.object(sources, "_load_cls_cache", return_value=cached), \
                patch.object(sources, "_get_json", side_effect=OSError("offline")):
            board = sources.fetch_cls_headlines()
        self.assertEqual(board.items[0].id, 42)
        self.assertIn("显示缓存", board.error)

    def test_hot_boards_filter_bad_items_and_unsafe_links(self):
        data = {"generated_at": "2026-11-12T09:00:00+08:00", "boards": [
            {"platform": "weibo", "ok": True, "items": [
                {"rank": 1, "title": " 测试热搜 ", "url": "https://s.weibo.com/x", "hot": 1234},
                {"rank": 2, "title": "", "url": "https://example.com"},
            ]},
            {"platform": "xiaohongshu", "ok": False, "error": "限流", "items": [
                {"rank": 1, "title": "小红书话题", "url": "javascript:alert(1)", "hot_text": "99w"},
            ]},
        ]}
        boards = parse_hot_snapshot(data)
        self.assertEqual(set(boards), {"weibo"})
        self.assertEqual(len(boards["weibo"].items), 1)
        self.assertEqual(boards["weibo"].items[0].title, "测试热搜")

    def test_bad_timestamp(self):
        self.assertEqual(snapshot_age_label("bad"), "时间未知")

    def test_official_page_filters_pinned_ads_and_keeps_rank_order(self):
        page = '''<div id="pl_top_realtimehot"><table><tbody>
          <tr><td class="td-01 ranktop"></td><td class="td-02"><a href="/weibo?q=pin">置顶话题</a></td><td class="td-03"><i>热</i></td></tr>
          <tr><td class="td-01">1</td><td class="td-02"><a href="/weibo?q=one">第一名</a><span>1889496</span></td><td class="td-03"><i>新</i></td></tr>
          <tr><td class="td-01"></td><td class="td-02"><a href="/weibo?q=ad">插入项</a></td><td class="td-03"><i>上新</i></td></tr>
          <tr><td class="td-01">2</td><td class="td-02"><a href="/weibo?q=two">第二名</a><span>98万</span></td><td class="td-03"></td></tr>
          </tbody></table></div>'''
        board = parse_weibo_page(page)
        self.assertEqual([item.rank for item in board.items], [1, 2])
        self.assertEqual([item.title for item in board.items],
                         ["第一名", "第二名"])
        self.assertEqual(board.items[0].hot, "1889496")
        self.assertEqual(board.items[1].hot, "98万")
        self.assertEqual(board.origin, "官网")
        with self.assertRaises(ValueError):
            parse_weibo_page("<html>请先登录</html>")

    def test_weibo_band_preserves_real_positions(self):
        board = parse_weibo_band({"data": {"hotgov": {"word": "#置顶#"},
            "band_list": [{"word": "第一", "realpos": 1, "raw_hot": 100},
                          {"word": "插入", "realpos": 0, "icon_desc": "上新"},
                          {"word": "第二", "realpos": 2, "num": 80}]}})
        self.assertEqual([item.rank for item in board.items], [1, 2])
        self.assertEqual(board.items[0].title, "第一")
        self.assertEqual(board.origin, "官网接口")

    def test_fetch_hot_prefers_official_band(self):
        payload = {"data": {"band_list": [
            {"word": "接口话题", "realpos": 1}]}}
        with patch.object(sources, "_load_cached_boards", return_value={}), \
                patch.object(sources, "_save_cached_boards") as save, \
                patch.object(sources, "_get_json", return_value=payload), \
                patch.object(sources, "_get_text") as get_text:
            board = sources.fetch_hot()["weibo"]
        self.assertEqual(board.items[0].title, "接口话题")
        self.assertEqual(board.origin, "官网接口")
        save.assert_called_once()
        get_text.assert_not_called()

    def test_fetch_hot_falls_back_to_official_page(self):
        page = '''<div id="pl_top_realtimehot"><table><tr>
          <td class="td-01">1</td><td class="td-02"><a href="/weibo?q=x">官网话题</a><span>123</span></td>
          <td class="td-03"></td></tr></table></div>'''
        with patch.object(sources, "_load_cached_boards", return_value={}), \
                patch.object(sources, "_save_cached_boards") as save, \
                patch.object(sources, "_get_text", return_value=page), \
                patch.object(sources, "_get_json", side_effect=ValueError("blocked")) as get_json:
            board = sources.fetch_hot()["weibo"]
        self.assertEqual(board.items[0].title, "官网话题")
        self.assertEqual(board.origin, "官网")
        save.assert_called_once()
        self.assertEqual(get_json.call_count, 1)

    def test_fetch_hot_falls_back_to_snapshot(self):
        snapshot = {"boards": [{"platform": "weibo", "ok": True,
                    "items": [{"rank": 1, "title": "备用话题"}]}]}
        with patch.object(sources, "_load_cached_boards", return_value={}), \
                patch.object(sources, "_save_cached_boards"), \
                patch.object(sources, "_get_text", side_effect=ValueError("visitor")), \
                patch.object(sources, "_get_json", side_effect=[ValueError("blocked"), snapshot]) as get_json:
            board = sources.fetch_hot()["weibo"]
        self.assertEqual(board.origin, "备用源")
        self.assertEqual(board.items[0].title, "备用话题")
        self.assertEqual(get_json.call_args_list[0].args[0], sources.HOT_BAND_URL)
        self.assertEqual(get_json.call_args_list[1].args[0], sources.HOT_SNAPSHOT_URL)

    def test_failed_refresh_keeps_timestamped_cache(self):
        good = {"generated_at": "2026-11-12T09:00:00+08:00", "boards": [
            {"platform": "weibo", "ok": True, "items": [{"rank": 1, "title": "话题", "url": "https://example.com"}]},
        ]}
        failed = {"generated_at": "2026-11-12T10:00:00+08:00", "boards": [
            {"platform": "weibo", "ok": False, "error": "限流", "items": []},
        ]}
        cached = {}
        def save(boards):
            cached.update(boards)
        with patch.object(sources, "_load_cached_boards", side_effect=lambda: cached.copy()), patch.object(sources, "_save_cached_boards", side_effect=save):
            with patch.object(sources, "_get_json", return_value=good):
                first = sources.fetch_hot(sources.HOT_SNAPSHOT_URL)["weibo"]
            with patch.object(sources, "_get_json", return_value=failed):
                board = sources.fetch_hot(sources.HOT_SNAPSHOT_URL)["weibo"]
        self.assertEqual(board.items[0].title, "话题")
        self.assertEqual(board.fetched_at, first.fetched_at)
        self.assertEqual(board.source_at, "2026-11-12T09:00:00+08:00")
        self.assertIn("显示缓存", board.error)

    def test_weibo_success_uses_local_refresh_time_and_keeps_source_time(self):
        source_stamp = "2026-11-12T08:20:00+08:00"
        payload = {"boards": [{"platform": "weibo", "ok": True,
                               "fetched_at": source_stamp,
                               "items": [{"rank": 1, "title": "热点"}]}]}
        with patch.object(sources, "_load_cached_boards", return_value={}), \
                patch.object(sources, "_save_cached_boards"), \
                patch.object(sources, "_get_json", return_value=payload):
            board = sources.fetch_hot(sources.HOT_SNAPSHOT_URL)["weibo"]
        self.assertEqual(board.source_at, source_stamp)
        self.assertNotEqual(board.fetched_at, source_stamp)
        self.assertFalse(board.error)

if __name__ == "__main__":
    unittest.main()
