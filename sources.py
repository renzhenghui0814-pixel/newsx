"""Network adapters and strict parsing for the floating widget."""

from __future__ import annotations

import json
import hashlib
import re
import threading
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from html.parser import HTMLParser
from typing import Any, Callable
from urllib.parse import urlencode, urljoin, urlparse
from urllib.request import Request, urlopen

from cache_store import load_record, parse_cache_time, save_record

HOT_URL = "https://weibo.com/ajax/statuses/hot_band"
HOT_PAGE_URL = "https://s.weibo.com/top/summary?cate=realtimehot"
HOT_SUMMARY_URL = "https://s.weibo.com/top/summary/summary?cate=realtimehot"
HOT_BAND_URL = HOT_URL
HOT_SNAPSHOT_URL = "https://solarhell.github.io/hot-search-crawler/latest.json"
WEIBO_VISITOR_URL = "https://visitor.passport.weibo.cn/visitor/genvisitor2"
WEIBO_SEARCH_URL = "https://m.weibo.cn/api/container/getIndex"
WEIBO_EXTEND_URL = "https://m.weibo.cn/statuses/extend"
WEIBO_MOBILE_UA = ("Mozilla/5.0 (Linux; Android 14; Pixel 8) "
                   "AppleWebKit/537.36 (KHTML, like Gecko) "
                   "Chrome/126.0.0.0 Mobile Safari/537.36")
_visitor_cookie = ""
_visitor_lock = threading.Lock()
CLS_URL = "https://www.cls.cn/v3/depth/home/assembled/1000"
USER_AGENT = "NewsXWidget/0.1 (personal desktop widget)"


@dataclass(frozen=True)
class HotItem:
    rank: int
    title: str
    url: str
    hot: str
    tag: str = ""


@dataclass(frozen=True)
class HotBoard:
    platform: str
    items: tuple[HotItem, ...]
    fetched_at: str
    error: str = ""
    source_at: str = ""
    origin: str = "备用源"


@dataclass(frozen=True)
class HeadlineItem:
    id: int
    title: str
    brief: str
    source: str
    published_at: int
    url: str


@dataclass(frozen=True)
class HeadlineBoard:
    items: tuple[HeadlineItem, ...]
    fetched_at: str
    error: str = ""


@dataclass(frozen=True)
class WeiboPost:
    title: str
    text: str
    url: str
    images: tuple[str, ...] = ()
    video_url: str = ""
    video_poster: str = ""
    published_at: str = ""
    complete: bool = True


class _PostDescriptionParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.parts: list[str] = []
        self.images: list[str] = []
        self.video_url = ""
        self.video_poster = ""

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        values = dict(attrs)
        if tag in ("br", "p", "div"):
            self.parts.append("\n")
        if tag == "img" and values.get("width") != "0":
            url = _safe_link(values.get("src"))
            if url and url not in self.images:
                self.images.append(url)
        if tag == "video":
            self.video_url = _safe_link(values.get("src")) or self.video_url
            self.video_poster = _safe_link(values.get("poster")) or self.video_poster
        if tag == "source" and not self.video_url:
            self.video_url = _safe_link(values.get("src"))
        if tag == "a" and not self.video_url:
            href = _safe_link(values.get("href"))
            if "video.weibo.com" in href:
                self.video_url = href

    def handle_data(self, data: str) -> None:
        self.parts.append(data)


def _weibo_cookie(refresh: bool = False) -> str:
    global _visitor_cookie
    with _visitor_lock:
        if _visitor_cookie and not refresh:
            return _visitor_cookie
        url = WEIBO_VISITOR_URL + "?" + urlencode({"cb": "visitor_callback",
                                                  "from": "weibo.cn"})
        request = Request(url, headers={"User-Agent": WEIBO_MOBILE_UA})
        with urlopen(request, timeout=10) as response:
            raw = response.read(100_000).decode("utf-8", "replace")
        match = re.search(r"\((\{.*\})\)", raw)
        if not match:
            raise ValueError("微博访客会话无效")
        data = json.loads(match.group(1)).get("data") or {}
        if not data.get("sub") or not data.get("subp"):
            raise ValueError("微博访客会话无效")
        _visitor_cookie = f"SUB={data['sub']}; SUBP={data['subp']}"
        return _visitor_cookie


def warm_weibo_session() -> None:
    try:
        _weibo_cookie()
    except Exception:
        pass


def _weibo_mobile_json(url: str, cookie: str) -> dict[str, Any]:
    request = Request(url, headers={
        "User-Agent": WEIBO_MOBILE_UA, "Referer": "https://m.weibo.cn/",
        "MWeibo-Pwa": "1", "X-Requested-With": "XMLHttpRequest",
        "Accept": "application/json, text/plain, */*", "Cookie": cookie,
    })
    with urlopen(request, timeout=10) as response:
        data = response.read(2_000_001)
    if len(data) > 2_000_000:
        raise ValueError("微博响应过大")
    result = json.loads(data)
    if not isinstance(result, dict) or result.get("ok") != 1:
        raise RuntimeError("微博搜索会话失效")
    return result


def _weibo_text(html: str) -> str:
    parser = _PostDescriptionParser()
    parser.feed(html)
    return "\n".join(line.strip() for line in "".join(parser.parts).splitlines()
                     if line.strip())


def parse_weibo_search(payload: dict[str, Any]) -> tuple[WeiboPost, ...]:
    cards = (payload.get("data") or {}).get("cards") or []
    blogs: list[dict[str, Any]] = []
    for card in cards:
        candidates = [card, *(card.get("card_group") or [])]
        for candidate in candidates:
            blog = candidate.get("mblog") if isinstance(candidate, dict) else None
            if isinstance(blog, dict) and blog.get("id"):
                blogs.append(blog)
                if len(blogs) == 3:
                    break
        if len(blogs) == 3:
            break
    posts: list[WeiboPost] = []
    for blog in blogs:
        bid = str(blog.get("bid") or blog["id"])
        user = blog.get("user") or {}
        pics = blog.get("pics") or []
        images = []
        for pic in pics:
            if not isinstance(pic, dict):
                continue
            url = _safe_link((pic.get("large") or {}).get("url") or
                             (pic.get("original") or {}).get("url") or
                             pic.get("url"))
            if url and url not in images:
                images.append(url)
        page = blog.get("page_info") or {}
        media = page.get("media_info") or {}
        video = ""
        poster = ""
        if page.get("type") == "video":
            video = _safe_link(media.get("stream_url_hd") or media.get("stream_url") or
                               page.get("page_url"))
            poster = _safe_link((page.get("page_pic") or {}).get("url"))
        posts.append(WeiboPost(str(user.get("screen_name") or "微博用户"),
                               _weibo_text(str(blog.get("text") or "")),
                               f"https://m.weibo.cn/status/{bid}", tuple(images),
                               video, poster, str(blog.get("created_at") or ""),
                               not bool(blog.get("isLongText"))))
    return tuple(posts)


def fetch_weibo_posts(keyword: str,
                      on_search: Callable[[tuple[WeiboPost, ...]], None] | None = None
                      ) -> tuple[WeiboPost, ...]:
    url = WEIBO_SEARCH_URL + "?" + urlencode({
        "containerid": f"100103type=1&q={keyword}", "page_type": "searchall"})
    for attempt in range(2):
        cookie = _weibo_cookie(refresh=attempt > 0)
        try:
            result = _weibo_mobile_json(url, cookie)
            break
        except RuntimeError:
            if attempt:
                raise
    posts = list(parse_weibo_search(result))
    if not posts:
        raise ValueError("微博综合搜索没有返回微博")
    if on_search:
        on_search(tuple(posts))

    def extend(post: WeiboPost) -> WeiboPost:
        if post.complete:
            return post
        bid = post.url.rsplit("/", 1)[-1]
        try:
            payload = _weibo_mobile_json(WEIBO_EXTEND_URL + "?" + urlencode({"id": bid}),
                                         cookie)
            body = _weibo_text(str((payload.get("data") or {}).get("longTextContent") or ""))
            if body:
                return WeiboPost(post.title, body, post.url, post.images,
                                 post.video_url, post.video_poster,
                                 post.published_at, True)
        except Exception:
            pass
        return post

    with ThreadPoolExecutor(max_workers=3) as pool:
        return tuple(pool.map(extend, posts))


def _get_json(url: str, headers: dict[str, str] | None = None) -> Any:
    parsed = urlparse(url)
    if parsed.scheme != "https" or not parsed.netloc:
        raise ValueError("接口地址必须是 HTTPS URL")
    request = Request(url, headers={"User-Agent": USER_AGENT, **(headers or {})})
    with urlopen(request, timeout=10) as response:
        if response.status != 200:
            raise RuntimeError(f"HTTP {response.status}")
        return json.load(response)


def _get_text(url: str) -> str:
    request = Request(url, headers={
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                      "AppleWebKit/537.36 Chrome/131.0.0.0 Safari/537.36",
        "Accept": "text/html,application/xhtml+xml",
        "Accept-Language": "zh-CN,zh;q=0.9",
        "Referer": "https://s.weibo.com/",
    })
    with urlopen(request, timeout=8) as response:
        if response.status != 200:
            raise RuntimeError(f"微博官网 HTTP {response.status}")
        return response.read().decode("utf-8", "replace")


def _safe_link(value: Any) -> str:
    if not isinstance(value, str):
        return ""
    parsed = urlparse(value)
    return value if parsed.scheme == "https" and parsed.netloc else ""


class _WeiboPageParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.board_depth = 0
        self.row: dict[str, Any] | None = None
        self.cell = ""
        self.in_link = False
        self.in_heat = False
        self.items: list[HotItem] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        attributes = dict(attrs)
        if tag == "div":
            if self.board_depth:
                self.board_depth += 1
            elif attributes.get("id") == "pl_top_realtimehot":
                self.board_depth = 1
        if not self.board_depth:
            return
        if tag == "tr":
            self.row = {"rank": [], "title": [], "hot": [], "tag": [],
                        "url": "", "pinned": False}
        elif self.row is not None and tag == "td":
            classes = (attributes.get("class") or "").split()
            self.cell = next((kind for kind in ("td-01", "td-02", "td-03")
                              if kind in classes), "")
            if self.cell == "td-01" and "ranktop" in classes:
                self.row["pinned"] = True
        elif self.row is not None and self.cell == "td-02" and tag == "a":
            href = attributes.get("href") or ""
            link = _safe_link(urljoin(HOT_PAGE_URL, href)) if href and not href.startswith("javascript:") else ""
            if link and not self.row["url"]:
                self.row["url"] = link
                self.in_link = True
        elif self.row is not None and self.cell == "td-02" and tag == "span":
            self.in_heat = not self.in_link
        elif self.row is not None and self.cell == "td-03" and tag == "img":
            if attributes.get("alt"):
                self.row["tag"].append(attributes["alt"])

    def handle_data(self, data: str) -> None:
        if self.row is None:
            return
        if self.cell == "td-01":
            self.row["rank"].append(data)
        elif self.cell == "td-02":
            if self.in_link:
                self.row["title"].append(data)
            elif self.in_heat:
                self.row["hot"].append(data)
        elif self.cell == "td-03":
            self.row["tag"].append(data)

    def handle_endtag(self, tag: str) -> None:
        if not self.board_depth:
            return
        if tag == "a":
            self.in_link = False
        elif tag == "span":
            self.in_heat = False
        elif tag == "td":
            self.cell = ""
        elif tag == "tr" and self.row is not None:
            row = self.row
            title = re.sub(r"\s+", " ", "".join(row["title"])).strip()
            if title and row["url"]:
                rank_text = "".join(row["rank"]).strip()
                rank = int(rank_text) if rank_text.isdecimal() else (
                    0 if row["pinned"] or "置顶" in rank_text else -1)
                self.items.append(HotItem(rank, title, row["url"],
                                          "".join(row["hot"]).strip(),
                                          "".join(row["tag"]).strip()))
            self.row = None
        elif tag == "div":
            self.board_depth -= 1


def parse_weibo_page(page: str) -> HotBoard:
    parser = _WeiboPageParser()
    parser.feed(page)
    if not parser.items or not any(item.rank > 0 for item in parser.items):
        raise ValueError("微博官网未返回热搜榜")
    now = datetime.now(timezone.utc).isoformat()
    return HotBoard("weibo", tuple(item for item in parser.items if item.rank > 0)[:60], now,
                    source_at=now, origin="官网")


def parse_weibo_band(payload: Any) -> HotBoard:
    data = payload.get("data") if isinstance(payload, dict) else None
    if not isinstance(data, dict) or not isinstance(data.get("band_list"), list):
        raise ValueError("微博榜单接口格式错误")
    rows: list[tuple[dict[str, Any], bool]] = []
    pinned = data.get("hotgovs") or data.get("hotgov") or []
    if isinstance(pinned, dict):
        pinned = [pinned]
    if isinstance(pinned, list):
        rows.extend((row, True) for row in pinned if isinstance(row, dict))
    rows.extend((row, False) for row in data["band_list"] if isinstance(row, dict))
    items: list[HotItem] = []
    seen: set[str] = set()
    for row, is_pinned in rows:
        raw_word = str(row.get("word") or "").strip()
        title = raw_word
        if title.startswith("#") and title.endswith("#"):
            title = title[1:-1]
        try:
            rank = 0 if is_pinned else int(row.get("realpos") or -1)
        except (TypeError, ValueError):
            rank = -1
        if not is_pinned and rank <= 0:
            rank = -1
        if rank <= 0 or not title or title in seen:
            continue
        seen.add(title)
        query = str(row.get("word_scheme") or raw_word)
        url = "https://s.weibo.com/weibo?" + urlencode({"q": query})
        hot = str(row.get("raw_hot") or row.get("num") or "") if rank > 0 else ""
        tag = str(row.get("label_name") or row.get("icon_desc") or "")
        items.append(HotItem(rank, title, url, hot, tag))
    if not items or not any(item.rank > 0 for item in items):
        raise ValueError("微博榜单接口未返回热搜")
    now = datetime.now(timezone.utc).isoformat()
    return HotBoard("weibo", tuple(items[:60]), now,
                    source_at=now, origin="官网接口")


def parse_hot_snapshot(payload: Any) -> dict[str, HotBoard]:
    if not isinstance(payload, dict) or not isinstance(payload.get("boards"), list):
        raise ValueError("热榜响应格式错误")
    result: dict[str, HotBoard] = {}
    generated = str(payload.get("generated_at", ""))
    for raw in payload["boards"]:
        if not isinstance(raw, dict):
            continue
        platform = raw.get("platform")
        if platform != "weibo":
            continue
        items = []
        for item in raw.get("items", []):
            if not isinstance(item, dict) or not isinstance(item.get("title"), str):
                continue
            title = item["title"].strip()
            if not title:
                continue
            try:
                rank = int(item.get("rank", len(items) + 1))
            except (ValueError, TypeError):
                rank = len(items) + 1
            hot = item.get("hot_text") or item.get("hot") or ""
            if rank > 0:
                items.append(HotItem(rank, title, _safe_link(item.get("url")), str(hot), str(item.get("tag") or "")))
        error = str(raw.get("error") or "") if not raw.get("ok", False) else ""
        stamp = str(raw.get("fetched_at") or generated)
        result[platform] = HotBoard(platform, tuple(items[:30]), stamp, error, stamp)
    return result


def _fetch_hot_snapshot(url: str, cached: dict[str, HotBoard]) -> dict[str, HotBoard]:
    try:
        current = parse_hot_snapshot(_get_json(url))
    except Exception as exc:
        if not cached:
            raise
        return {key: HotBoard(key, board.items, board.fetched_at,
                              f"获取失败，显示缓存：{exc}", board.source_at,
                              board.origin)
                for key, board in cached.items()}
    merged: dict[str, HotBoard] = {}
    board = current.get("weibo")
    if board and board.items and not board.error:
        refreshed = HotBoard("weibo", board.items, datetime.now(timezone.utc).isoformat(),
                             source_at=board.source_at, origin="备用源")
        merged["weibo"] = refreshed
        cached["weibo"] = refreshed
    elif "weibo" in cached:
        old = cached["weibo"]
        merged["weibo"] = HotBoard("weibo", old.items, old.fetched_at,
                                    (board.error if board else "采集失败") + "；显示缓存",
                                    old.source_at, old.origin)
    elif board:
        merged["weibo"] = board
    _save_cached_boards(cached)
    return merged


def fetch_hot(url: str = HOT_URL) -> dict[str, HotBoard]:
    cached = _load_cached_boards()
    if urlparse(url).hostname in ("weibo.com", "s.weibo.com"):
        try:
            board = parse_weibo_band(_get_json(
                HOT_BAND_URL, {"User-Agent": "Mozilla/5.0", "Referer": "https://weibo.com/"}))
        except Exception:
            pass
        else:
            result = {"weibo": board}
            _save_cached_boards(result)
            return result
        for page_url in (HOT_PAGE_URL, HOT_SUMMARY_URL):
            try:
                board = parse_weibo_page(_get_text(page_url))
            except Exception:
                continue
            result = {"weibo": board}
            _save_cached_boards(result)
            return result
        return _fetch_hot_snapshot(HOT_SNAPSHOT_URL, cached)
    return _fetch_hot_snapshot(url, cached)


def _cls_signed_url() -> str:
    parameters = urlencode(sorted({"appName": "CailianpressWeb", "os": "web",
                                   "sv": "8.7.9"}.items()))
    digest = hashlib.sha1(parameters.encode("utf-8")).hexdigest()
    signature = hashlib.md5(digest.encode("ascii")).hexdigest()
    return f"{CLS_URL}?{parameters}&sign={signature}"


def parse_cls_headlines(payload: Any) -> HeadlineBoard:
    if not isinstance(payload, dict) or str(payload.get("errno")) != "0":
        raise ValueError("财联社头条响应错误")
    data = payload.get("data")
    if not isinstance(data, dict):
        raise ValueError("财联社头条缺少数据")
    top_rows, depth_rows = data.get("top_article"), data.get("depth_list")
    if not isinstance(top_rows, list) or not isinstance(depth_rows, list):
        raise ValueError("财联社头条格式错误")
    rows = top_rows + depth_rows
    items: list[HeadlineItem] = []
    seen: set[int] = set()
    for row in rows:
        if not isinstance(row, dict) or row.get("is_ad"):
            continue
        try:
            article_id = int(row["id"])
            published_at = int(row.get("ctime", 0))
        except (KeyError, TypeError, ValueError):
            continue
        title = re.sub(r"\s+", " ", str(row.get("title") or row.get("brief") or "")).strip()
        if article_id <= 0 or not title or article_id in seen:
            continue
        seen.add(article_id)
        items.append(HeadlineItem(
            article_id, title, re.sub(r"\s+", " ", str(row.get("brief") or "")).strip(),
            str(row.get("source") or row.get("author") or "财联社").strip(),
            published_at, f"https://www.cls.cn/detail/{article_id}"))
        if len(items) >= 30:
            break
    if not items:
        raise ValueError("财联社头条暂时为空")
    return HeadlineBoard(tuple(items), datetime.now(timezone.utc).isoformat())


def _load_cls_cache() -> HeadlineBoard | None:
    try:
        raw = load_record("cls")
        if not raw:
            return None
        items = tuple(HeadlineItem(**item) for item in raw["items"]
                      if isinstance(item, dict))
        return HeadlineBoard(items, parse_cache_time(raw["time"])) if items else None
    except (KeyError, ValueError, TypeError):
        return None


def load_cached_headlines() -> HeadlineBoard | None:
    """Read the persisted headline snapshot without a network request."""
    return _load_cls_cache()


def _save_cls_cache(board: HeadlineBoard) -> None:
    save_record("cls", board.fetched_at, [asdict(item) for item in board.items])


def fetch_cls_headlines() -> HeadlineBoard:
    cached = _load_cls_cache()
    try:
        board = parse_cls_headlines(_get_json(
            _cls_signed_url(), {"User-Agent": "Mozilla/5.0", "Referer": "https://www.cls.cn/"}))
    except Exception as exc:
        if cached:
            return HeadlineBoard(cached.items, cached.fetched_at,
                                 f"获取失败，显示缓存：{exc}")
        raise
    _save_cls_cache(board)
    return board


class _ArticleText(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.parts: list[str] = []
        self.hidden = 0

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag in {"script", "style"}:
            self.hidden += 1
        elif tag in {"p", "h1", "h2", "h3", "li", "blockquote", "br"}:
            self.parts.append("\n")

    def handle_endtag(self, tag: str) -> None:
        if tag in {"script", "style"}:
            self.hidden = max(0, self.hidden-1)
        elif tag in {"p", "h1", "h2", "h3", "li", "blockquote"}:
            self.parts.append("\n")

    def handle_data(self, data: str) -> None:
        if not self.hidden:
            self.parts.append(data)

    def text(self) -> str:
        lines = [re.sub(r"\s+", " ", line).strip()
                 for line in "".join(self.parts).splitlines()]
        return "\n".join(line for line in lines if line)[:20000]


def parse_cls_article(page: str) -> str:
    match = re.search(r'<script id="__NEXT_DATA__" type="application/json">(.*?)</script>',
                      page, re.DOTALL)
    if not match:
        raise ValueError("财联社正文不可用")
    payload = json.loads(match.group(1))
    article = payload.get("props", {}).get("pageProps", {}).get("articleDetail", {})
    content = article.get("content", "") if isinstance(article, dict) else ""
    if not isinstance(content, str) or not content:
        raise ValueError("财联社正文不可用")
    parser = _ArticleText()
    parser.feed(content)
    result = parser.text()
    if not result:
        raise ValueError("财联社正文不可用")
    return result


def fetch_cls_article(item: HeadlineItem) -> str:
    if not item.url.startswith("https://www.cls.cn/detail/"):
        raise ValueError("文章链接无效")
    request = Request(item.url, headers={"User-Agent": "Mozilla/5.0",
                                         "Referer": "https://www.cls.cn/"})
    with urlopen(request, timeout=12) as response:
        if response.status != 200:
            raise RuntimeError(f"HTTP {response.status}")
        return parse_cls_article(response.read().decode("utf-8", "replace"))


def _load_cached_boards() -> dict[str, HotBoard]:
    value = load_record("weibo")
    if not value:
        return {}
    try:
        items = tuple(HotItem(**item) for item in value["items"]
                      if isinstance(item, dict) and item.get("rank", 0) > 0)
        return {"weibo": HotBoard("weibo", items, parse_cache_time(value["time"]),
                                   source_at=parse_cache_time(value.get("source_time", "")),
                                   origin=str(value.get("origin") or "备用源"))} if items else {}
    except (KeyError, ValueError, TypeError):
        return {}


def load_cached_boards() -> dict[str, HotBoard]:
    """Read persisted hot boards before the first asynchronous refresh."""
    return _load_cached_boards()


def _save_cached_boards(boards: dict[str, HotBoard]) -> None:
    board = boards.get("weibo")
    if board:
        save_record("weibo", board.fetched_at, [asdict(item) for item in board.items],
                    source_time=board.source_at, origin=board.origin)


def snapshot_age_label(value: str) -> str:
    try:
        timestamp = datetime.fromisoformat(value.replace("Z", "+00:00"))
        if timestamp.tzinfo is None:
            return "时间未知"
        minutes = max(0, int((datetime.now(timezone.utc) - timestamp).total_seconds() // 60))
        return "刚刚" if minutes < 1 else f"{minutes} 分钟前" if minutes < 60 else f"{minutes // 60} 小时前"
    except ValueError:
        return "时间未知"
