"""Resizable Windows desktop market and trending-topic widget."""

from __future__ import annotations

import queue
import io
import subprocess
import sys
import ctypes
import os
import re
import time
import winreg
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
import tkinter as tk
import tkinter.font as tkfont
import webbrowser
from datetime import datetime, timezone

import config
from market_cache import load_market_cache, reconcile_quotes, save_market_cache
from sources import (HeadlineBoard, HeadlineItem, HotBoard, HotItem, WeiboPost,
                     fetch_weibo_posts, fetch_cls_article,
                     fetch_cls_headlines, fetch_hot, load_cached_boards,
                     load_cached_headlines, warm_weibo_session)
from tora_feed import Quote, ToraFeed, normalize_symbol, validate_front

MIN_WIDTH, MIN_HEIGHT = 332, 390
MAX_WIDTH, MAX_HEIGHT = 900, 960
KEY_COLOR = "#ff00ff"
FONT = "Microsoft YaHei UI"
NUMBER_FONT = "Segoe UI"
HOT_NUMBER_FONT = "Bahnschrift SemiBold"
ICON_FONT = "Segoe MDL2 Assets"
HEADER_HEIGHT = 54
THEMES = {
    "light": {
        "bg": "#EFF3F6", "header": "#EFF3F6", "surface": "#F9FBFC",
        "text": "#213340", "muted": "#617480", "subtle": "#728590",
        "line": "#D8E2E8", "accent": "#397FBD", "hover": "#DCEAF4",
        "up": "#B54450", "down": "#228268", "rim": "#C9D4DC",
        "shadow": "#DDE5EA", "shadow_far": "#E4EAEF",
        "card_edge": "#F7FAFB",
        "book_track": "#D3DEE4", "sell_bar": "#C85E6B",
        "buy_bar": "#2F9B78",
        "input": "#FFFFFF", "segment_bg": "#E7EDF1",
        "segment_active": "#FFFFFF",
        "button_hover": "#2C70A9",
        "hot_feature": "#A64D2C", "hot_tag_bg": "#F7E3D7",
        "hot_first": "#C52B3B", "hot_first_tag_bg": "#FBE5E8",
    },
    "dark": {
        "bg": "#17242D", "header": "#17242D", "surface": "#344854",
        "text": "#F4F7F9", "muted": "#BDD0D9", "subtle": "#9CB4C0",
        "line": "#4C616D", "accent": "#8CC8EF", "hover": "#4D6778",
        "up": "#FF9DA5", "down": "#80DCB9", "rim": "#586772",
        "shadow": "#1D242A", "shadow_far": "#20262C",
        "card_edge": "#3C4953",
        "book_track": "#364853", "sell_bar": "#D47C87",
        "buy_bar": "#57BA99",
        "input": "#20313C", "segment_bg": "#20313C",
        "segment_active": "#4A6879",
        "button_hover": "#B9E1F6",
        "hot_feature": "#FFB987", "hot_tag_bg": "#59483F",
        "hot_first": "#FF6B78", "hot_first_tag_bg": "#59373F",
    },
}
ICON_TIPS = {
    "theme": "切换主题", "opacity": "调整透明度", "close": "关闭组件",
    "site": "设置行情站点", "add": "添加股票", "refresh": "刷新数据",
    "refresh_weibo": "刷新数据",
    "open_article": "在财联社打开",
    "open_weibo": "在微博打开",
    "back": "返回卡片", "remove": "移出自选",
}


def _compact_number(value: float | None) -> str:
    if value is None:
        return "—"
    if value >= 1e8:
        return f"{value / 1e8:.2f}亿"
    if value >= 1e4:
        return f"{value / 1e4:.1f}万"
    return f"{value:,.0f}"


def _price(value: float | None) -> str:
    return f"{value:.2f}" if value is not None else "—"


def _display_price(value: str) -> str:
    return value or "—"


def _display_change(value: str) -> str:
    if value in ("", "--"):
        return ""
    return value if value.endswith("%") else value + "%"


def _site_state_from_feed_status(status: str) -> str:
    if status.startswith("连接断开"):
        return "站点已断开"
    if status.startswith(("登录失败", "登录请求失败")):
        return "站点登录失败"
    if status.startswith("正在连接"):
        return "站点连接中"
    return "站点已连接"


class Widget:
    def __init__(self) -> None:
        self.settings = config.load()
        self.width = self.settings["width"]
        self.height = self.settings["height"]
        self.root = tk.Tk()
        self.root.withdraw()
        self.root.title("NewsX")
        self.root.overrideredirect(True)
        self.root.configure(bg=self.colors["bg"])
        self.root.attributes("-alpha", self.settings["opacity"])
        self.root.attributes("-topmost", self.settings["always_on_top"])
        self.root.resizable(False, False)
        x, y = self.settings["x"], self.settings["y"]
        if not isinstance(x, int) or not isinstance(y, int):
            x, y = self.root.winfo_screenwidth() - self.width - 36, 60
        self.root.geometry(f"{self.width}x{self.height}+{x}+{y}")
        self.canvas = tk.Canvas(self.root, width=self.width, height=self.height,
                                bg=self.colors["bg"], highlightthickness=0)
        self.canvas.pack()
        self.canvas.bind("<ButtonPress-1>", self._press)
        self.canvas.bind("<B1-Motion>", self._drag)
        self.canvas.bind("<ButtonRelease-1>", self._release)
        self.canvas.bind("<MouseWheel>", self._wheel)
        self.canvas.bind("<Motion>", self._motion)
        self.canvas.bind("<Leave>", self._leave)
        self.root.bind("<Escape>", self._escape)
        self.root.bind("<Control-Key-1>", lambda _e: self._select_tab("stocks"))
        self.root.bind("<Control-Key-2>", lambda _e: self._select_tab("weibo"))
        self.root.bind("<Control-Key-3>", lambda _e: self._select_tab("cls"))
        self.root.bind("<Control-Key-4>", lambda _e: self._select_tab("cls"))
        self.root.bind("<Control-r>", lambda _e: self.refresh())
        self.root.bind("<Down>", lambda _e: self._scroll_by(1))
        self.root.bind("<Up>", lambda _e: self._scroll_by(-1))
        self.tab = "stocks"
        self.focus_card: str | None = None
        self.page_scroll = 0
        self.icon_locations: dict[str, tuple[float, float]] = {}
        self.detail_symbol: str | None = None
        self.hot: dict[str, HotBoard] = {}
        self.headlines: HeadlineBoard | None = None
        self.headline_error = ""
        self.headline_busy = False
        self.refresh_status: dict[str, str] = {}
        self.headline_detail_index: int | None = None
        self.headline_detail_item: HeadlineItem | None = None
        self.weibo_detail_item: HotItem | None = None
        self.weibo_posts: tuple[WeiboPost, ...] = ()
        self.weibo_posts_loading = False
        self.weibo_posts_error = ""
        self.weibo_request_token = 0
        self.weibo_scroll = 0
        self.weibo_image_bytes: dict[str, bytes] = {}
        self.weibo_images: dict[tuple[str, int, int, str], object] = {}
        self.weibo_expanded: set[int] = set()
        self.weibo_post_cache: dict[str, tuple[float, tuple[WeiboPost, ...]]] = {}
        self.weibo_preview: tuple[int, int] | None = None
        self.weibo_layout_cache: tuple[object, list[tuple[int, str, object]], int] | None = None
        self.weibo_body_font = tkfont.Font(root=self.root, family=FONT, size=10)
        self.weibo_title_font = tkfont.Font(root=self.root, family=FONT, size=11, weight="bold")
        self.weibo_playing_video: int | None = None
        self.weibo_video_downloading: int | None = None
        self.weibo_video_file = ""
        self.weibo_video_error = ""
        self.weibo_video_error_index: int | None = None
        self.weibo_video_token = 0
        self.weibo_video_temp_files: set[str] = set()
        self.weibo_video_cache: dict[str, str] = {}
        self.weibo_video_frame: tk.Frame | None = None
        self.weibo_video_process: subprocess.Popen | None = None
        self.headline_body = ""
        self.headline_loading = False
        self.headline_article_error = ""
        self.headline_request_token = 0
        self.article_scroll = 0
        self._article_layout_cache: tuple[object, ...] | None = None
        self.quotes: list[Quote] = []
        self.hot_error = ""
        self.stock_error = ""
        self.site_state = "站点未连接" if self.settings["tora_front"] else "站点未配置"
        self.hot_busy = False
        self.feed: ToraFeed | None = None
        self.feed_version = -1
        self.feed_token = 0
        self.last_stock_update = ""
        self.stock_cache_at = ""
        self.market_day = ""
        self._last_market_save = 0.0
        self.hot = load_cached_boards()
        self.headlines = load_cached_headlines()
        self.quotes, self.stock_cache_at, self.market_day = load_market_cache(
            config.symbol_codes(self.settings))
        cached_quotes = {quote.symbol: quote for quote in self.quotes}
        self.quotes = [cached_quotes.get(symbol) or
                       Quote(symbol, symbol, "", "", trading_day=self.market_day)
                       for symbol in config.symbol_codes(self.settings)]
        if self.stock_cache_at:
            self.last_stock_update = self._format_cache_time(self.stock_cache_at,
                                                             "%H:%M:%S")
        for source in self.hot:
            self.refresh_status[source] = "已加载缓存"
        if self.headlines:
            self.refresh_status["cls"] = "已加载缓存"
        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.drag_origin: tuple[int, int] | None = None
        self.resize_origin: tuple[int, int, int, int, int, int, str] | None = None
        self.scroll = 0
        self.hover_tag = ""
        self.tooltip_tag = ""
        self.tooltip_after: str | None = None
        self.inside = False
        title_font = tkfont.Font(root=self.root, family=NUMBER_FONT,
                                 size=14, weight="bold")
        self._brand_width = 35 + title_font.measure("NewsX")
        self._brand_x = (self.width-self._brand_width)/2
        self._brand_after: str | None = None
        self._brand_from = self._brand_x
        self._brand_to = self._brand_x
        self._brand_started = 0.0
        self.popup = ""
        self.popup_entry: tk.Entry | None = None
        self.exchange_choice = tk.StringVar(value="沪")
        self.popup_error = ""
        self.opacity_drag = False
        self._glass_key: tuple[object, ...] | None = None
        self._glass_image = None
        self._glass_header_image = None
        self._desktop_capture = None
        self._shape_cache: dict[tuple[object, ...], object] = {}
        self._clip_fonts: dict[tuple[int, str], tkfont.Font] = {}
        self._headline_clip_cache: dict[tuple[str, int, int, str], str] = {}
        self._hot_value_fonts = {
            False: tkfont.Font(root=self.root, family=NUMBER_FONT, size=9),
            True: tkfont.Font(root=self.root, family=HOT_NUMBER_FONT,
                              size=10, weight="bold"),
        }
        self._draw()
        self.root.update_idletasks()
        self._round_window()
        self.root.deiconify()
        self.root.after(50, self._process_events)
        self.root.after(250, self.refresh)
        self.root.after(300, self._start_feed)
        threading.Thread(target=warm_weibo_session, daemon=True).start()
        self.root.after(self.settings["refresh_seconds"] * 1000, self._tick)

    @property
    def colors(self) -> dict[str, str]:
        return THEMES[self.settings["theme"]]

    def _round_window(self) -> None:
        """Clip the frameless native window, including its background, to a round region."""
        try:
            user32, gdi32 = ctypes.windll.user32, ctypes.windll.gdi32
            user32.GetParent.argtypes = [ctypes.c_void_p]
            user32.GetParent.restype = ctypes.c_void_p
            user32.SetWindowRgn.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                            ctypes.c_int]
            user32.SetWindowRgn.restype = ctypes.c_int
            gdi32.CreateRoundRectRgn.argtypes = [ctypes.c_int]*6
            gdi32.CreateRoundRectRgn.restype = ctypes.c_void_p
            gdi32.DeleteObject.argtypes = [ctypes.c_void_p]
            hwnd = self.root.winfo_id()
            parent = user32.GetParent(hwnd)
            if parent:
                hwnd = parent
            region = gdi32.CreateRoundRectRgn(
                0, 0, self.width+1, self.height+1, 30, 30)
            if not user32.SetWindowRgn(hwnd, region, True):
                gdi32.DeleteObject(region)
        except (AttributeError, OSError):
            pass

    def _frosted_background(self):
        """Use a cool-gray light panel or blur wallpaper under the dark panel."""
        if self.resize_origin:
            self._glass_header_image = None
            return None
        key = (self.width, self.height, self.root.winfo_x(), self.root.winfo_y(),
               self.settings["theme"])
        if key == self._glass_key:
            return self._glass_image
        self._glass_key, self._glass_image = key, None
        self._glass_header_image = None
        if self.settings["theme"] == "light":
            return None
        try:
            from PIL import Image, ImageFilter, ImageGrab, ImageOps, ImageTk
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Control Panel\Desktop") as registry:
                path = winreg.QueryValueEx(registry, "Wallpaper")[0]
            screen_w = self.root.winfo_screenwidth()
            screen_h = self.root.winfo_screenheight()
            x = max(0, min(screen_w-self.width, self.root.winfo_x()))
            y = max(0, min(screen_h-self.height, self.root.winfo_y()))
            if path and os.path.isfile(path):
                with Image.open(path) as wallpaper:
                    fitted = ImageOps.fit(wallpaper.convert("RGB"), (screen_w, screen_h))
                    crop = fitted.crop((x, y, x+self.width, y+self.height))
            else:
                if self._desktop_capture is None:
                    self._desktop_capture = ImageGrab.grab().convert("RGB")
                crop = self._desktop_capture.crop((x, y, x+self.width, y+self.height))
            blurred = crop.filter(ImageFilter.GaussianBlur(22))
            color = self.colors["bg"].lstrip("#")
            tint = Image.new("RGB", blurred.size,
                             tuple(int(color[i:i+2], 16) for i in (0, 2, 4)))
            frosted = Image.blend(blurred, tint, .66)
            self._glass_image = ImageTk.PhotoImage(frosted, master=self.root)
            self._glass_header_image = ImageTk.PhotoImage(
                frosted.crop((0, 0, self.width, HEADER_HEIGHT)), master=self.root)
        except (OSError, ImportError, ValueError):
            pass
        return self._glass_image

    def _round(self, x1: float, y1: float, x2: float, y2: float, r: int,
               color: str, tag: str = "", alpha: int = 255,
               shadow: bool = False) -> int:
        """Draw a single antialiased translucent surface instead of joined arcs."""
        width, height = max(1, round(x2-x1)), max(1, round(y2-y1))
        key = (width, height, r, color, alpha, shadow)
        image = self._shape_cache.get(key)
        if image is None:
            try:
                from PIL import Image, ImageDraw, ImageFilter, ImageTk
                scale, pad = 3, (9 if shadow else 0)
                size = ((width+pad*2)*scale, (height+pad*2)*scale)
                layer = Image.new("RGBA", size)
                draw = ImageDraw.Draw(layer)
                box = (pad*scale, pad*scale,
                       (pad+width)*scale-1, (pad+height)*scale-1)
                rgb = tuple(int(color[i:i+2], 16) for i in (1, 3, 5))
                if shadow:
                    shade = Image.new("RGBA", size)
                    ImageDraw.Draw(shade).rounded_rectangle(
                        (box[0], box[1]+3*scale, box[2], box[3]+3*scale),
                        radius=r*scale,
                        fill=(5, 20, 29,
                              58 if self.settings["theme"] == "dark" else 30))
                    layer.alpha_composite(shade.filter(ImageFilter.GaussianBlur(5*scale)))
                draw = ImageDraw.Draw(layer)
                draw.rounded_rectangle(box, radius=r*scale, fill=(*rgb, alpha))
                image = ImageTk.PhotoImage(layer.resize(
                    (width+pad*2, height+pad*2), Image.Resampling.LANCZOS),
                    master=self.root)
                self._shape_cache[key] = image
                return self.canvas.create_image(round(x1)-pad, round(y1)-pad,
                                                image=image, anchor="nw", tags=tag)
            except ImportError:
                pass
        if image is not None:
            pad = 9 if shadow else 0
            return self.canvas.create_image(round(x1)-pad, round(y1)-pad,
                                            image=image, anchor="nw", tags=tag)
        else:
            # Pillow is optional for source runs; keep a functional fallback.
            return self.canvas.create_rectangle(x1, y1, x2, y2, fill=color,
                                                outline="", tags=tag)

    def _text(self, x: float, y: float, value: str, size: int = 10,
              color: str | None = None, weight: str = "normal",
              anchor: str = "w", tag: str = "", font: str = FONT) -> None:
        self.canvas.create_text(x, y, text=value, font=(font, size, weight),
                                fill=color or self.colors["text"], anchor=anchor,
                                tags=tag)

    def _clip(self, value: str, limit: float, size: int = 10,
              weight: str = "normal") -> str:
        value = re.sub(r"\s+", " ", value).strip()
        key = (size, weight)
        font = self._clip_fonts.get(key)
        if font is None:
            font = tkfont.Font(root=self.root, family=FONT, size=size, weight=weight)
            self._clip_fonts[key] = font
        if font.measure(value) <= limit:
            return value
        low, high = 0, len(value)
        while low < high:
            middle = (low+high+1)//2
            if font.measure(value[:middle] + "…") <= limit:
                low = middle
            else:
                high = middle-1
        return value[:low].rstrip() + "…"

    def _headline_clip(self, value: str, limit: float, size: int,
                       weight: str = "normal") -> str:
        # A 16 px width bucket avoids measuring every title again on each
        # two-pixel resize step. Round down so text never overflows.
        width = max(0, int(limit)//16*16)
        key = (value, width, size, weight)
        clipped = self._headline_clip_cache.get(key)
        if clipped is None:
            clipped = self._clip(value, width, size, weight)
            if len(self._headline_clip_cache) > 1000:
                self._headline_clip_cache.clear()
            self._headline_clip_cache[key] = clipped
        return clipped

    @staticmethod
    def _hot_value(value: str) -> str:
        raw = str(value).strip()
        if not raw:
            return "—"
        if re.fullmatch(r"\d+(?:\.\d+)?", raw):
            number = float(raw)
            if number >= 100000000:
                return f"{number/100000000:.1f}亿"
            return f"{number/10000:.1f}万" if number >= 10000 else f"{number:g}"
        return raw

    def _draw_weibo_row(self, item: HotItem, left: int, right: int,
                        y: int, height: int, tag: str,
                        featured: bool, compact: bool = False) -> None:
        colors = self.colors
        first = item.rank in (0, 1)
        ink = (colors["hot_first"] if first else
               colors["hot_feature"] if featured else colors["subtle"])
        title_x = left+34 if compact else left+40
        center_y = y+height/2
        value = self._hot_value(item.hot) if item.hot else ""
        value_size = 10 if featured else 9
        label = re.sub(r"\s+", " ", str(item.tag or "")).strip()
        # Reserve the tag column even when the source has no tag, so every
        # heat value ends at the same x coordinate.
        tag_left = right-42
        heat_right = tag_left-9
        if label:
            label = self._clip(label, 32, 8, "bold")
        heat_width = self._hot_value_fonts[featured].measure(value)
        self._hit(left-5, y, right+5, y+height-2, tag,
                  colors["hover"] if self.hover_tag == tag else None)
        rank_label = "置顶" if item.rank == 0 else "·" if item.rank < 0 else f"{item.rank:02d}"
        self._text(left, center_y, rank_label,
                   9 if item.rank == 0 else 13 if featured else 10, ink,
                   "bold" if featured else "normal", tag=tag,
                   font=HOT_NUMBER_FONT if featured else NUMBER_FONT)
        self._text(title_x, center_y,
                   self._clip(item.title, heat_right-heat_width-title_x-13, 10),
                   10, colors["text"], tag=tag)
        self._text(heat_right, center_y, value, value_size,
                   ink if first or featured else colors["muted"],
                   "bold" if featured else "normal", anchor="e", tag=tag,
                   font=HOT_NUMBER_FONT if featured else NUMBER_FONT)
        if label:
            self._round(tag_left, center_y-9, right, center_y+9, 5,
                        colors["hot_first_tag_bg"] if first else
                        colors["hot_tag_bg"] if featured else colors["segment_bg"],
                        tag=tag, alpha=220)
            self._text((tag_left+right)/2, center_y, label, 8,
                       ink if first or featured else colors["muted"],
                       "bold", anchor="center", tag=tag)

    def _hit(self, x1: float, y1: float, x2: float, y2: float,
             tag: str, fill: str | None = None) -> None:
        if fill and fill == self.colors["hover"]:
            self._round(x1, y1, x2, y2, 8, fill, tag, alpha=115)
        self.canvas.create_rectangle(x1, y1, x2, y2,
                                     fill="",
                                     outline="", tags=tag)

    def _icon(self, kind: str, x: float, y: float,
              background: str | None = None, tag: str | None = None) -> None:
        """Use one system icon family, rasterized by Windows font rendering."""
        colors = self.colors
        glyphs = {
            "theme": "\ue706" if self.settings["theme"] == "dark" else "\ue708",
            "close": "\ue8bb", "site": "\ue774",
            "add": "\ue710", "refresh": "\ue72c", "back": "\ue72b",
            "remove": "\ue74d", "open": "\ue8a7",
        }
        control = tag or kind
        self.icon_locations[control] = (x, y)
        self._hit(x-14, y-14, x+14, y+14, control,
                  colors["hover"] if self.hover_tag == control else None)
        ink = ((colors["hot_first"] if kind == "remove" else colors["accent"])
               if self.hover_tag == control else colors["muted"])
        if kind == "remove":
            ink = colors["hot_first"]
            self.canvas.create_oval(x-7, y-7, x+7, y+7, outline=ink,
                                    width=1.8, tags=control)
            self.canvas.create_line(x-4, y, x+4, y, fill=ink,
                                    width=1.8, capstyle="round", tags=control)
        elif kind == "opacity":
            self._opacity_glyph(x, y, ink, control)
        else:
            self.canvas.create_text(x, y, text=glyphs[kind],
                                    font=(ICON_FONT, 13), fill=ink,
                                    anchor="center", tags=control)

    def _opacity_glyph(self, x: float, y: float, color: str, tag: str) -> None:
        """A half-filled circle conveys transparency without a font glyph."""
        key = ("opacity_glyph", color)
        image = self._shape_cache.get(key)
        if image is None:
            try:
                from PIL import Image, ImageDraw, ImageTk
                scale = 4
                icon = Image.new("RGBA", (20*scale, 20*scale))
                draw = ImageDraw.Draw(icon)
                rgb = tuple(int(color[i:i+2], 16) for i in (1, 3, 5))
                draw.pieslice((4*scale, 4*scale, 16*scale, 16*scale),
                              90, 270, fill=(*rgb, 170))
                draw.ellipse((2*scale, 2*scale, 18*scale, 18*scale),
                             outline=(*rgb, 255), width=6)
                image = ImageTk.PhotoImage(icon.resize(
                    (20, 20), Image.Resampling.LANCZOS), master=self.root)
                self._shape_cache[key] = image
            except ImportError:
                pass
        if image is not None:
            self.canvas.create_image(x, y, image=image,
                                     anchor="center", tags=tag)
        else:
            self.canvas.create_oval(x-8, y-8, x+8, y+8,
                                    outline=color, width=2, tags=tag)
            self.canvas.create_arc(x-6, y-6, x+6, y+6, start=90, extent=180,
                                   style="pieslice", outline="", fill=color,
                                   tags=tag)

    def _draw(self) -> None:
        c, colors, w, h = self.canvas, self.colors, self.width, self.height
        c.delete("all")
        if len(self._shape_cache) > 160:
            self._shape_cache.clear()
        self.icon_locations.clear()
        glass = self._frosted_background()
        if glass:
            c.create_image(0, 0, image=glass, anchor="nw")
        else:
            c.create_rectangle(0, 0, w, h, fill=colors["bg"], outline="")
        # Expanded views sit directly on the frosted panel. Only the dashboard
        # and transient popups use separate card surfaces.
        if self.detail_symbol and self.tab == "stocks":
            self._draw_detail()
            self._draw_status()
        elif self.headline_detail_item is not None and self.tab == "cls":
            self._draw_headline_detail()
            self._draw_status()
        elif self.weibo_detail_item is not None and self.tab == "weibo":
            self._draw_weibo_detail()
            self._draw_status()
        elif self.focus_card:
            self._draw_focus_card()
            self._draw_status()
        else:
            self._draw_dashboard()
        # Canvas has no clipping path. Restore the frosted header above scrolled
        # cards so their text and hit targets cannot show through the toolbar.
        if self._glass_header_image:
            c.create_image(0, 0, image=self._glass_header_image, anchor="nw")
        else:
            c.create_rectangle(0, 0, w, HEADER_HEIGHT,
                               fill=colors["bg"], outline="")
        brand_x = self._brand_x
        self._round(brand_x, 13, brand_x+26, 39, 8, colors["accent"],
                    tag="brand", alpha=225)
        self._text(brand_x+13, 26, "N", 12,
                   "#FFFFFF" if self.settings["theme"] == "light"
                   else colors["surface"], "bold",
                   anchor="center", tag="brand", font=NUMBER_FONT)
        self._text(brand_x+35, 26, "NewsX", 14, colors["text"], "bold",
                   tag="brand", font=NUMBER_FONT)
        if self.inside:
            for kind, x in (("theme", w-95), ("opacity", w-63), ("close", w-31)):
                self._icon(kind, x, 26)
        if self.popup:
            self._draw_popup()
        elif self.tooltip_tag:
            self._draw_tooltip()

    def _card(self, x: int, y: int, width: int, height: int) -> None:
        colors = self.colors
        self._round(x, y, x+width, y+height, 18, colors["surface"],
                    alpha=207 if self.settings["theme"] == "dark" else 255,
                    shadow=True)

    @staticmethod
    def _format_cache_time(value: str, pattern: str = "%m-%d %H:%M") -> str:
        try:
            stamp = datetime.fromisoformat(value.replace("Z", "+00:00"))
            return stamp.astimezone().strftime(pattern)
        except (ValueError, TypeError):
            return "时间未知"

    def _card_timestamp(self, x: int, y: int, width: int, value: str,
                        failed: bool = False) -> None:
        if value:
            label = self._format_cache_time(value)
            if failed:
                label = "失败 · " + label
            self._text(x+width-34, y+29, label, 8,
                       self.colors["muted"], anchor="e", font=NUMBER_FONT)

    def _dashboard_layout(self) -> list[tuple[str, int, int, int, int]]:
        gap, x = 12, 14
        width = self.width-28
        followed = config.followed_codes(self.settings)
        stock_count = min(5, sum(q.symbol in followed for q in self.quotes))
        stock_height = max(160, 92+stock_count*48)
        hot_board = self.hot.get("weibo")
        hot_count = min(5, len(hot_board.items)) if hot_board else 0
        hot_height = max(108, 64+hot_count*37)
        headline_count = min(5, len(self.headlines.items)) if self.headlines else 0
        headline_height = max(108, 66+headline_count*32)
        top = 64-self.page_scroll
        cards = [("stocks", x, top, width, stock_height)]
        hot_top = top+stock_height+gap
        if self.width >= 560:
            hot_width = (width-gap)//2
            cards.extend([
                ("weibo", x, hot_top, hot_width, hot_height),
                ("cls", x+hot_width+gap, hot_top, width-hot_width-gap, headline_height),
            ])
        else:
            cards.extend([
                ("weibo", x, hot_top, width, hot_height),
                ("cls", x, hot_top+hot_height+gap, width, headline_height),
            ])
        return cards

    def _dashboard_scroll_limit(self) -> int:
        layout = self._dashboard_layout()
        last = max(y+self.page_scroll+height for _, _, y, _, height in layout)
        return max(0, last+14-self.height)

    def _draw_dashboard(self) -> None:
        layout = self._dashboard_layout()
        for key, x, y, width, height in layout:
            if y > self.height or y+height < HEADER_HEIGHT:
                continue
            self._card(x, y, width, height)
            if key == "stocks":
                self._draw_stock_card(x, y, width, height)
            elif key == "cls":
                self._draw_headline_card(x, y, width, height)
            else:
                self._draw_hot_card(key, x, y, width, height)

    def _draw_stock_card(self, x: int, y: int, width: int, height: int) -> None:
        colors, c = self.colors, self.canvas
        self._hit(x+16, y+14, x+width-16, y+44,
                  "card_stocks", colors["surface"])
        self._text(x+18, y+29, "自选行情", 12, colors["text"], "bold",
                   tag="card_stocks")
        self._card_timestamp(x, y, width, self.stock_cache_at)
        self._text(x+width-20, y+29, "›", 15, colors["subtle"],
                   anchor="e", tag="card_stocks")
        c.create_line(x+18, y+49, x+width-18, y+49, fill=colors["line"])
        price_x, amount_x = x+width-108, x+width-18
        self._text(x+18, y+67, "名称 / 代码", 9, colors["subtle"])
        self._text(price_x, y+67, "最新价 / 涨跌幅", 9,
                   colors["subtle"], anchor="e")
        self._text(amount_x, y+67, "成交额", 9, colors["subtle"], anchor="e")
        followed = config.followed_codes(self.settings)
        featured_quotes = [(index, quote) for index, quote in enumerate(self.quotes)
                           if quote.symbol in followed][:5]
        if not featured_quotes:
            self._text(x+18, y+125, self._clip(
                ("在自选行情中点击五角星添加关注" if self.quotes else
                 self.stock_error or ("正在等待行情推送" if self.settings["tora_front"]
                                      else "连接 TORA 行情以查看自选")), width-36, 10),
                10, colors["muted"])
        else:
            for row_index, (quote_index, quote) in enumerate(featured_quotes):
                row_y = y+80+row_index*48
                tag = f"stock_{quote_index}"
                self._hit(x+13, row_y, x+width-13, row_y+45, tag,
                          colors["hover"] if self.hover_tag == tag
                          else colors["surface"])
                tone = colors["down"] if quote.change_percent.startswith("-") else (
                    colors["up"] if quote.change_percent not in ("", "--")
                    else colors["text"])
                self._text(x+18, row_y+15,
                           self._clip(quote.name, price_x-x-68, 10, "bold"),
                           10, colors["text"], "bold", tag=tag)
                self._text(x+18, row_y+34, quote.symbol, 8,
                           colors["muted"], tag=tag, font=NUMBER_FONT)
                self._text(price_x, row_y+15, _display_price(quote.price), 11,
                           tone, "bold", "e", tag, NUMBER_FONT)
                self._text(price_x, row_y+34, _display_change(quote.change_percent), 9,
                           tone, anchor="e", tag=tag, font=NUMBER_FONT)
                self._text(amount_x, row_y+24,
                           _compact_number(quote.turnover), 9, tone,
                           "bold", "e", tag, NUMBER_FONT)
                if row_index < len(featured_quotes)-1:
                    c.create_line(x+18, row_y+47, x+width-18, row_y+47,
                                  fill=colors["line"])

    def _draw_hot_card(self, key: str, x: int, y: int,
                       width: int, height: int) -> None:
        colors, c = self.colors, self.canvas
        board = self.hot.get(key)
        self._hit(x+16, y+14, x+width-16, y+44,
                  f"card_{key}", colors["surface"])
        self._text(x+18, y+29, "微博热搜", 12, colors["text"], "bold",
                   tag=f"card_{key}")
        if board:
            self._text(x+104, y+29, board.origin, 8, colors["subtle"])
            self._card_timestamp(x, y, width, board.fetched_at,
                                 bool(board.error or "失败" in self.refresh_status.get("weibo", "")))
        self._text(x+width-18, y+29, "›", 15, colors["subtle"],
                   anchor="e", tag=f"card_{key}")
        c.create_line(x+18, y+49, x+width-18, y+49, fill=colors["line"])
        if board and board.items:
            row_height = 37
            count = min(len(board.items), 5)
            for index, item in enumerate(board.items[:count]):
                row_y = y+54+index*row_height
                tag = f"item_{key}_{index}"
                self._draw_weibo_row(item, x+18, x+width-18,
                                     row_y, row_height, tag,
                                     featured=index < 3, compact=True)
        else:
            self._text(x+18, y+88,
                       "暂时取不到热榜" if (board and board.error) or self.hot_error
                       else "正在获取热榜", 10, colors["muted"])

    @staticmethod
    def _headline_time(item: HeadlineItem) -> str:
        try:
            return datetime.fromtimestamp(item.published_at).strftime("%H:%M")
        except (OSError, OverflowError, ValueError):
            return "--:--"

    def _draw_headline_card(self, x: int, y: int,
                            width: int, height: int) -> None:
        colors, c = self.colors, self.canvas
        self._hit(x+16, y+14, x+width-16, y+44,
                  "card_cls", colors["surface"])
        self._text(x+18, y+29, "财联社头条", 12, colors["text"], "bold",
                   tag="card_cls")
        if self.headlines:
            self._card_timestamp(x, y, width, self.headlines.fetched_at,
                                 bool(self.headlines.error or
                                      "失败" in self.refresh_status.get("cls", "")))
        self._text(x+width-18, y+29, "›", 15, colors["subtle"],
                   anchor="e", tag="card_cls")
        c.create_line(x+18, y+49, x+width-18, y+49, fill=colors["line"])
        if not self.headlines or not self.headlines.items:
            self._text(x+18, y+88,
                       "暂时取不到头条" if self.headline_error else "正在获取头条",
                       10, colors["muted"])
            return
        for index, item in enumerate(self.headlines.items[:5]):
            row_y = y+56+index*32
            tag = f"item_cls_{index}"
            self._hit(x+13, row_y, x+width-13, row_y+30, tag,
                      colors["hover"] if self.hover_tag == tag else None)
            self._text(x+18, row_y+15, self._headline_time(item), 9,
                       colors["accent"], tag=tag, font=NUMBER_FONT)
            self._text(x+62, row_y+15,
                       self._clip(item.title, width-82, 10),
                       10, colors["text"], tag=tag)

    def _draw_focus_card(self) -> None:
        colors, w = self.colors, self.width
        self._icon("back", 30, 75, colors["surface"])
        self._text(54, 75, {"stocks": "自选行情", "weibo": "微博热搜",
                            "cls": "财联社头条"}[self.tab],
                   12, colors["text"], "bold")
        self.canvas.create_line(18, 91, w-18, 91, fill=colors["line"])
        if self.tab == "stocks":
            self._draw_stocks()
        elif self.tab == "cls":
            self._draw_headline_list()
        else:
            self._draw_hot()

    def _headline_visible_rows(self) -> int:
        return max(1, (self.height-145)//43)

    def _draw_headline_list(self) -> None:
        colors, w = self.colors, self.width
        if not self.headlines or not self.headlines.items:
            self._empty("暂时取不到头条",
                        self.headline_error or "刷新后会显示财联社最新头条。")
            return
        for index, item in enumerate(self.headlines.items[
                self.scroll:self.scroll+self._headline_visible_rows()]):
            row_y = 96+index*43
            tag = f"item_{self.scroll+index}"
            if not self.resize_origin:
                hover = self._round(15, row_y+2, w-15, row_y+41, 8,
                                    colors["hover"], tag=f"headline_hover_{tag}",
                                    alpha=115)
                if self.hover_tag != tag:
                    self.canvas.itemconfigure(hover, state="hidden")
            self._hit(15, row_y, w-15, row_y+41, tag)
            self._text(20, row_y+20, self._headline_time(item), 9,
                       colors["accent"], tag=tag, font=NUMBER_FONT)
            self._text(70, row_y+20, self._headline_clip(item.title, w-92, 10, "bold"),
                       10, colors["text"], "bold", tag=tag)
            self.canvas.create_line(20, row_y+42, w-20, row_y+42,
                                    fill=colors["line"])

    @staticmethod
    def _wrap_article(text: str, font: tkfont.Font,
                      max_width: int) -> list[str]:
        lines: list[str] = []
        current, width = "", 0
        for char in text.strip():
            char_width = font.measure(char)
            if current and width+char_width > max_width:
                lines.append(current.strip())
                current, width = "", 0
            current += char
            width += char_width
        if current.strip():
            lines.append(current.strip())
        return lines

    def _headline_article_layout(self) -> tuple[list[tuple[int, str, int, str, str]], int]:
        if self.headline_detail_item is None:
            return [], 0
        item = self.headline_detail_item
        body = self.headline_body or item.brief or "正文加载中…"
        if self.headline_loading:
            body += "\n正文加载中…"
        elif self.headline_article_error:
            body += "\n正文暂不可用，可点击右上角图标查看原文。"
        key = (item.id, body, self.width)
        if self._article_layout_cache and self._article_layout_cache[0] == key:
            return self._article_layout_cache[1], self._article_layout_cache[2]  # type: ignore[return-value]
        title_font = tkfont.Font(root=self.root, family=FONT, size=13,
                                 weight="bold")
        body_font = tkfont.Font(root=self.root, family=FONT, size=10)
        rows: list[tuple[int, str, int, str, str]] = []
        offset = 0
        for line in self._wrap_article(item.title, title_font, self.width-40):
            rows.append((offset, line, 13, "text", "bold"))
            offset += 23
        offset += 8
        try:
            stamp = datetime.fromtimestamp(item.published_at).strftime("%m-%d %H:%M")
        except (OSError, OverflowError, ValueError):
            stamp = "时间未知"
        rows.append((offset, f"{item.source}  ·  {stamp}", 9, "muted", "normal"))
        offset += 31
        for paragraph in body.splitlines():
            if not paragraph.strip():
                offset += 7
                continue
            for line in self._wrap_article(paragraph, body_font, self.width-40):
                rows.append((offset, line, 10, "text", "normal"))
                offset += 20
            offset += 9
        self._article_layout_cache = (key, rows, offset)
        return rows, offset

    def _draw_headline_detail(self) -> None:
        if self.headline_detail_item is None:
            return
        colors, w, h = self.colors, self.width, self.height
        self._icon("back", 30, 75)
        self._text(54, 75, "财联社头条", 12, colors["text"], "bold")
        self._icon("open", w-30, 75, tag="open_article")
        self.canvas.create_line(18, 91, w-18, 91, fill=colors["line"])
        rows, _ = self._headline_article_layout()
        for offset, value, size, color, weight in rows:
            y = 108+offset-self.article_scroll
            if 98 <= y <= h-70:
                self._text(20, y, value, size, colors[color], weight,
                           anchor="nw")

    def _open_headline(self, index: int) -> None:
        if not self.headlines or not 0 <= index < len(self.headlines.items):
            return
        self.tab = self.focus_card = "cls"
        self.headline_detail_index = index
        self.headline_detail_item = self.headlines.items[index]
        self.headline_body = self.headline_detail_item.brief
        self.headline_loading = True
        self.headline_article_error = ""
        self.article_scroll = 0
        self._article_layout_cache = None
        self.headline_request_token += 1
        token = self.headline_request_token
        item = self.headline_detail_item
        self._draw()
        threading.Thread(target=self._fetch_headline_article,
                         args=(item, token), daemon=True).start()

    def _fetch_headline_article(self, item: HeadlineItem, token: int) -> None:
        try:
            self.events.put(("cls_article", (token, fetch_cls_article(item), "")))
        except Exception as exc:
            self.events.put(("cls_article", (token, "", str(exc))))

    def _open_weibo(self, index: int) -> None:
        board = self.hot.get("weibo")
        if not board or not 0 <= index < len(board.items):
            return
        self._stop_weibo_video()
        self.tab = self.focus_card = "weibo"
        self.weibo_detail_item = board.items[index]
        self.weibo_preview = None
        self.weibo_posts = ()
        self.weibo_expanded.clear()
        self.weibo_layout_cache = None
        self.weibo_posts_loading = True
        self.weibo_posts_error = ""
        self.weibo_scroll = 0
        self.weibo_image_bytes.clear()
        self.weibo_images.clear()
        self.weibo_request_token += 1
        token = self.weibo_request_token
        cached = self.weibo_post_cache.get(self.weibo_detail_item.title)
        if cached and time.monotonic()-cached[0] < 60:
            self.weibo_posts = cached[1]
            self.weibo_posts_loading = False
            self._draw()
            threading.Thread(target=self._fetch_weibo_images,
                             args=(cached[1], token), daemon=True).start()
            return
        self._draw()
        threading.Thread(target=self._fetch_weibo_posts,
                         args=(self.weibo_detail_item.title, token), daemon=True).start()

    def _fetch_weibo_posts(self, keyword: str, token: int) -> None:
        try:
            def on_search(posts: tuple[WeiboPost, ...]) -> None:
                self.events.put(("weibo_posts", (token, keyword, posts, "", False)))
                threading.Thread(target=self._fetch_weibo_images,
                                 args=(posts, token), daemon=True).start()
            posts = fetch_weibo_posts(keyword, on_search=on_search)
            self.events.put(("weibo_posts", (token, keyword, posts, "", True)))
        except Exception as exc:
            self.events.put(("weibo_posts", (token, keyword, (), str(exc), True)))

    def _fetch_weibo_images(self, posts: tuple[WeiboPost, ...], token: int) -> None:
        from urllib.parse import urlparse
        from urllib.request import Request, urlopen
        urls = list(dict.fromkeys(url for post in posts
                                  for url in (*post.images, post.video_poster) if url))

        def load(url: str) -> tuple[str, bytes] | None:
            if token != self.weibo_request_token:
                return None
            host = (urlparse(url).hostname or "").lower()
            if not (host.endswith(".sinaimg.cn") or host == "sinaimg.cn"):
                return None
            try:
                request = Request(url, headers={"User-Agent": "Mozilla/5.0",
                                                "Referer": "https://weibo.com/"})
                with urlopen(request, timeout=8) as response:
                    data = response.read(8_000_001)
                return (url, data) if len(data) <= 8_000_000 else None
            except Exception:
                return None

        with ThreadPoolExecutor(max_workers=4) as pool:
            for future in as_completed([pool.submit(load, url) for url in urls]):
                result = future.result()
                if result and token == self.weibo_request_token:
                    self.events.put(("weibo_image", (token, *result)))

    def _weibo_detail_layout(self) -> tuple[list[tuple[int, str, object]], int]:
        """Return vertically placed text, media and action rows."""
        if not self.weibo_detail_item:
            return [], 0
        key = (self.weibo_detail_item.title, id(self.weibo_posts), self.width,
               self.height,
               tuple(sorted(self.weibo_expanded)), self.weibo_preview,
               self.weibo_posts_loading, self.weibo_posts_error,
               self.weibo_playing_video, self.weibo_video_downloading,
               self.weibo_video_error)
        if self.weibo_layout_cache and self.weibo_layout_cache[0] == key:
            return self.weibo_layout_cache[1], self.weibo_layout_cache[2]
        rows: list[tuple[int, str, object]] = []
        offset = 0
        thumb_h = self._weibo_thumb_height()
        video_h = self._weibo_video_height()
        preview_h = self._weibo_preview_height()
        for line in self._wrap_article(self.weibo_detail_item.title, self.weibo_title_font,
                                       self.width-42):
            rows.append((offset, "heading", line))
            offset += 24
        offset += 13
        if self.weibo_posts_loading:
            rows.append((offset, "hint", "正在获取微博…"))
            offset += 25
        elif self.weibo_posts_error or not self.weibo_posts:
            rows.append((offset, "hint", "暂时取不到相关微博，可在微博查看。"))
            offset += 25
        for index, post in enumerate(self.weibo_posts):
            for line in self._wrap_article(f"{index+1:02d}  {post.title}",
                                           self.weibo_title_font, self.width-42):
                rows.append((offset, "post_head", line))
                offset += 22
            offset += 3
            collapsed = len(post.text) > 150 and index not in self.weibo_expanded
            body = post.text[:150].rstrip()+"…" if collapsed else post.text
            for paragraph in body.splitlines():
                for line in self._wrap_article(paragraph, self.weibo_body_font,
                                               self.width-42):
                    rows.append((offset, "body", line))
                    offset += 20
                offset += 5
            if len(post.text) > 150:
                rows.append((offset, "expand", index))
                offset += 25
            if not post.complete:
                rows.append((offset, "hint", "全文暂不可用，请在微博查看。"))
                offset += 24
            for start in range(0, len(post.images), 3):
                rows.append((offset, "images", (index, start,
                                                  post.images[start:start+3])))
                offset += thumb_h+7
            if self.weibo_preview and self.weibo_preview[0] == index:
                rows.append((offset, "preview", self.weibo_preview))
                offset += preview_h+30
            if post.video_url:
                rows.append((offset, "video", (index, post.video_poster)))
                offset += video_h+10
            rows.append((offset, "post_link", index))
            offset += 39
        self.weibo_layout_cache = (key, rows, offset)
        return rows, offset

    def _weibo_thumb_height(self) -> int:
        width = (self.width-52)//3
        return min(150, max(84, round(width*.72)))

    def _weibo_video_height(self) -> int:
        return min(300, max(160, round((self.width-40)*9/16)))

    def _weibo_preview_height(self) -> int:
        return min(max(150, self.height-190), 440,
                   max(210, round((self.width-40)*1.05)))

    def _scroll_to_weibo_preview(self) -> None:
        if self.weibo_preview is None:
            return
        rows, total = self._weibo_detail_layout()
        offset = next((offset for offset, kind, value in rows
                       if kind == "preview" and value == self.weibo_preview), None)
        if offset is not None:
            limit = max(0, total-(self.height-160))
            self.weibo_scroll = min(limit, offset+6)

    @staticmethod
    def _trim_dark_sidebars(image: object) -> object:
        """Remove baked-in black sidebars from wide image thumbnails."""
        from PIL import ImageOps
        if image.width <= image.height:
            return image
        sample = ImageOps.grayscale(image.copy())
        sample.thumbnail((96, 96))
        pixels = sample.load()
        top, bottom = sample.height//6, sample.height*5//6
        columns = [sum(pixels[x, y] for y in range(top, bottom)) /
                   max(1, bottom-top) for x in range(sample.width)]
        left, right = 0, sample.width-1
        while left < sample.width//3 and columns[left] < 26:
            left += 1
        while right > sample.width*2//3 and columns[right] < 26:
            right -= 1
        center = columns[sample.width//3:sample.width*2//3]
        if (left >= sample.width*.12 and sample.width-1-right >= sample.width*.12
                and center and sum(center)/len(center) > 48):
            scale = image.width/sample.width
            return image.crop((round(left*scale), 0,
                               round((right+1)*scale), image.height))
        return image

    def _weibo_photo(self, url: str, width: int, height: int,
                     mode: str = "contain") -> object | None:
        data = self.weibo_image_bytes.get(url)
        if not data:
            return None
        key = (url, width, height, mode)
        if key not in self.weibo_images:
            try:
                from PIL import Image, ImageOps, ImageTk
                with Image.open(io.BytesIO(data)) as original:
                    image = original.convert("RGB")
                if mode == "cover":
                    image = self._trim_dark_sidebars(image)
                    image = ImageOps.fit(image, (width, height),
                                         method=Image.Resampling.LANCZOS)
                else:
                    image.thumbnail((width, height), Image.Resampling.LANCZOS)
                self.weibo_images[key] = ImageTk.PhotoImage(image, master=self.root)
            except Exception:
                return None
        return self.weibo_images[key]

    def _draw_weibo_detail(self) -> None:
        if not self.weibo_detail_item:
            return
        c, colors, w, h = self.canvas, self.colors, self.width, self.height
        self._icon("back", 30, 75)
        self._text(54, 75, "微博热搜", 12, colors["text"], "bold")
        self._icon("open", w-30, 75, tag="open_weibo")
        c.create_line(18, 91, w-18, 91, fill=colors["line"])
        rows, _ = self._weibo_detail_layout()
        video_position: tuple[int, int, int] | None = None
        for offset, kind, value in rows:
            y = 108+offset-self.weibo_scroll
            if not 101 <= y <= h-59:
                continue
            if kind == "heading":
                self._text(20, y, str(value), 11, colors["text"], "bold", anchor="nw")
            elif kind == "hint":
                self._text(20, y, str(value), 10, colors["muted"], anchor="nw")
            elif kind == "post_head":
                self._text(20, y, str(value), 10, colors["accent"],
                           "bold", anchor="nw")
            elif kind == "body":
                self._text(20, y, str(value), 10, colors["text"], anchor="nw")
            elif kind == "expand":
                self._text(20, y, "收起全文  ⌃" if value in self.weibo_expanded
                           else "展开全文  ⌄", 9, colors["accent"],
                           anchor="nw", tag=f"expand_{value}")
            elif kind == "images" and y+self._weibo_thumb_height() < h-38:
                index, start, urls = value
                size = (w-52)//3
                thumb_h = self._weibo_thumb_height()
                for image_index, url in enumerate(urls):
                    x = 20+image_index*(size+6)
                    photo = self._weibo_photo(url, size, thumb_h, "cover")
                    tag = f"image_{index}_{start+image_index}"
                    if photo:
                        c.create_image(x, y, image=photo, anchor="nw", tags=tag)
                    else:
                        self._round(x, y, x+size, y+thumb_h, 5,
                                    colors["segment_bg"], tag=tag)
                        self._text(x+size/2, y+thumb_h/2, "图片", 9, colors["muted"],
                                   anchor="center", tag=tag)
                    if self.weibo_preview == (index, start+image_index):
                        c.create_rectangle(x, y, x+size, y+thumb_h,
                                           outline=colors["accent"], width=2, tags=tag)
            elif kind == "preview" and y+self._weibo_preview_height() < h-38:
                index, image_index = value
                url = self.weibo_posts[index].images[image_index]
                preview_h = self._weibo_preview_height()
                self._round(20, y, w-20, y+preview_h, 7, colors["segment_bg"])
                photo = self._weibo_photo(url, w-40, preview_h)
                if photo:
                    c.create_image(w/2, y+preview_h/2, image=photo,
                                   anchor="center", tags="preview_close")
                else:
                    self._text(w/2, y+preview_h/2, "图片加载中…", 9,
                               colors["muted"], anchor="center")
                self._text(20, y+preview_h+15, "收起预览  ⌃", 9,
                           colors["accent"], tag="preview_close")
                count = len(self.weibo_posts[index].images)
                if count > 1:
                    self._text(w-118, y+preview_h+15, "‹", 14,
                               colors["accent"], tag="preview_prev")
                    self._text(w-78, y+preview_h+15,
                               f"{image_index+1}/{count}", 9,
                               colors["muted"], anchor="center")
                    self._text(w-38, y+preview_h+15, "›", 14,
                               colors["accent"], tag="preview_next")
            elif kind == "video":
                index, poster = value
                video_h = self._weibo_video_height()
                if y+video_h < h-38:
                    if self.weibo_playing_video == index:
                        video_position = (20, y, video_h)
                    else:
                        tag = f"video_{index}"
                        photo = self._weibo_photo(poster, w-40, video_h,
                                                  "cover") if poster else None
                        if photo:
                            c.create_image(20, y, image=photo,
                                           anchor="nw", tags=tag)
                        else:
                            self._round(20, y, w-20, y+video_h, 7,
                                        colors["segment_bg"], tag=tag)
                        label = ("…" if self.weibo_video_downloading == index
                                 else "重试" if self.weibo_video_error and
                                 self.weibo_video_error_index == index else "▶")
                        self._round(w/2-29, y+video_h/2-29, w/2+29,
                                    y+video_h/2+29, 29, colors["header"],
                                    tag=tag, alpha=215)
                        self._text(w/2, y+video_h/2, label, 13,
                                   colors["accent"], "bold", anchor="center", tag=tag)
            elif kind == "post_link":
                index = value
                self._text(20, y, "在微博打开  ↗", 9, colors["accent"],
                           anchor="nw", tag=f"post_{index}")
                c.create_line(20, y+26, w-20, y+26, fill=colors["line"])
        self._sync_weibo_video(video_position)

    def _stop_weibo_video(self) -> None:
        self.weibo_video_token += 1
        if self.weibo_video_process is not None:
            try:
                self.weibo_video_process.terminate()
                self.weibo_video_process.wait(timeout=1)
            except OSError:
                pass
            except subprocess.TimeoutExpired:
                self.weibo_video_process.kill()
            self.weibo_video_process = None
        if self.weibo_video_frame is not None:
            self.weibo_video_frame.destroy()
            self.weibo_video_frame = None
        self.weibo_playing_video = None
        self.weibo_video_downloading = None
        self.weibo_video_file = ""

    def _play_weibo_video(self, index: int) -> None:
        if not 0 <= index < len(self.weibo_posts):
            return
        self._stop_weibo_video()
        self.weibo_video_error = ""
        self.weibo_video_error_index = None
        url = self.weibo_posts[index].video_url
        cached = self.weibo_video_cache.get(url)
        if cached and os.path.isfile(cached):
            self.weibo_playing_video = index
            self.weibo_video_file = cached
            self._draw()
            return
        self.weibo_video_downloading = index
        token = self.weibo_video_token
        self._draw()
        threading.Thread(target=self._download_weibo_video,
                         args=(index, url, token), daemon=True).start()

    def _download_weibo_video(self, index: int, url: str, token: int) -> None:
        from urllib.request import Request, urlopen
        try:
            request = Request(url, headers={"User-Agent": "Mozilla/5.0",
                                            "Referer": "https://m.weibo.cn/"})
            with urlopen(request, timeout=15) as response:
                if not (response.headers.get("Content-Type") or "").startswith("video/"):
                    raise ValueError("微博未返回可播放视频")
                data = response.read(80_000_001)
            if len(data) > 80_000_000:
                raise ValueError("视频文件过大")
            if token != self.weibo_video_token:
                return
            parent = (os.path.join(config.APP_DIR, "video_cache")
                      if getattr(sys, "frozen", False)
                      else os.path.join(os.path.dirname(__file__), ".build", "video-cache"))
            os.makedirs(parent, exist_ok=True)
            path = os.path.join(parent, f"newsx-{os.getpid()}-{token}.mp4")
            with open(path, "wb") as output:
                output.write(data)
            self.weibo_video_temp_files.add(path)
            self.events.put(("weibo_video", (token, index, path, "")))
        except Exception as exc:
            self.events.put(("weibo_video", (token, index, "", str(exc))))

    def _sync_weibo_video(self, position: tuple[int, int, int] | None) -> None:
        if self.weibo_playing_video is None:
            return
        if position is None:
            self._stop_weibo_video()
            self.root.after_idle(self._draw)
            return
        x, y, height = position
        if self.weibo_video_frame is not None:
            self.weibo_video_frame.place_configure(
                x=x, y=y, width=self.width-40, height=height)
            return
        base = getattr(sys, "_MEIPASS", os.path.dirname(__file__))
        executable = os.path.join(base, "mpv", "mpv.exe")
        if not os.path.isfile(executable):
            executable = os.path.join(os.path.dirname(__file__), "vendor", "mpv", "mpv.exe")
        try:
            frame = tk.Frame(self.root, bg="black", highlightthickness=0)
            frame.place(x=x, y=y, width=self.width-40, height=height)
            frame.update_idletasks()
            self.weibo_video_frame = frame
            self.weibo_video_process = subprocess.Popen([
                executable, "--no-config", "--no-border", "--force-window=yes",
                "--osc=yes", "--panscan=1.0", "--show-in-taskbar=no",
                "--keep-open=yes", "--no-terminal", "--wid="+str(frame.winfo_id()),
                self.weibo_video_file,
            ], creationflags=subprocess.CREATE_NO_WINDOW,
                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL)
            self.root.after(1200, self._check_weibo_video)
        except (OSError, tk.TclError):
            index = self.weibo_playing_video
            self._stop_weibo_video()
            self.weibo_video_error = "视频播放失败"
            self.weibo_video_error_index = index
            self.root.after_idle(self._draw)

    def _check_weibo_video(self) -> None:
        if (self.weibo_playing_video is not None
                and self.weibo_video_process is not None
                and self.weibo_video_process.poll() is not None):
            index = self.weibo_playing_video
            self._stop_weibo_video()
            self.weibo_video_error = "视频播放失败"
            self.weibo_video_error_index = index
            self._draw()

    def _draw_tabs(self) -> None:
        c, colors = self.canvas, self.colors
        for key, label, left, right in (
            ("stocks", "自选", 17, 83), ("weibo", "微博", 95, 158),
        ):
            tag = "tab_" + key
            self._hit(left, 47, right, 79, tag)
            active = self.tab == key
            self._text(left+3, 63, label, 10,
                       colors["text"] if active else colors["muted"],
                       "bold" if active else "normal", tag=tag)
            if active:
                c.create_line(left+3, 79, right-8, 79, fill=colors["accent"],
                              width=2, tags=tag)
        c.create_line(17, 81, self.width-17, 81, fill=colors["line"])

    def _stock_visible_rows(self) -> int:
        return max(1, (self.height-34-110)//49)

    def _hot_visible_rows(self) -> int:
        return max(1, (self.height-145)//49)

    def _draw_stocks(self) -> None:
        colors, c, w = self.colors, self.canvas, self.width
        price_x, amount_x = w-116, w-18
        self._text(19, 99, "关注", 9, colors["subtle"])
        self._text(51, 99, "名称 / 代码", 9, colors["subtle"])
        self._text(price_x, 99, "最新价 / 涨跌幅", 9, colors["subtle"], anchor="e")
        self._text(amount_x, 99, "成交额", 9, colors["subtle"], anchor="e")
        if not self.quotes:
            if not self.settings["tora_front"]:
                self._empty("连接你的行情", "移入面板，在底部点击站点按钮设置 TORA 前置地址。")
            else:
                self._empty("正在等待行情", self.stock_error or "连接后会显示自选股票。")
            return
        followed = config.followed_codes(self.settings)
        for index, quote in enumerate(self.quotes[self.scroll:
                                                  self.scroll+self._stock_visible_rows()]):
            y = 110 + index*49
            tag = f"stock_{self.scroll+index}"
            self._hit(15, y, w-15, y+47, tag,
                      colors["hover"] if self.hover_tag == tag else colors["surface"])
            star_tag = f"follow_{self.scroll+index}"
            self._hit(17, y+8, 45, y+40, star_tag,
                      colors["hover"] if self.hover_tag == star_tag else None)
            self._text(30, y+24,
                       "★" if quote.symbol in followed else "☆",
                       18, "#E89122" if self.settings["theme"] == "light" else "#FFB45E",
                       anchor="center", tag=star_tag)
            tone = colors["down"] if quote.change_percent.startswith("-") else (
                colors["up"] if quote.change_percent not in ("", "--") else colors["text"])
            self._text(51, y+15, self._clip(quote.name, price_x-95, 10, "bold"),
                       10, colors["text"], "bold", tag=tag)
            self._text(51, y+34, quote.symbol, 8, colors["muted"],
                       tag=tag, font=NUMBER_FONT)
            self._text(price_x, y+15, _display_price(quote.price), 12, tone, "bold",
                       "e", tag, NUMBER_FONT)
            self._text(price_x, y+34, _display_change(quote.change_percent),
                       9, tone, "normal",
                       "e", tag, NUMBER_FONT)
            self._text(amount_x, y+24, _compact_number(quote.turnover), 10,
                       tone if quote.turnover is not None else colors["muted"],
                       "bold", "e", tag, NUMBER_FONT)
            c.create_line(19, y+48, w-19, y+48, fill=colors["line"])

    def _draw_hot(self) -> None:
        colors, c, w = self.colors, self.canvas, self.width
        board = self.hot.get("weibo")
        if not board or not board.items:
            detail = board.error if board and board.error else (
                self.hot_error or "稍等片刻，最新话题会出现在这里。")
            self._empty("暂时取不到热榜", detail)
            return
        for index, item in enumerate(board.items[self.scroll:
                                                  self.scroll+self._hot_visible_rows()]):
            y = 96 + index*49
            tag = f"item_{self.scroll+index}"
            self._draw_weibo_row(item, 20, w-20, y, 49, tag,
                                 featured=self.scroll+index < 3)
            c.create_line(19, y+48, w-19, y+48, fill=colors["line"])

    def _empty(self, heading: str, detail: str) -> None:
        colors = self.colors
        center = 120 + (self.height-390)*.35
        self.canvas.create_line(28, center+70, 65, center+70,
                                77, center+51, 90, center+91,
                                105, center+62, 132, center+62,
                                fill=colors["accent"], width=2,
                                capstyle="round", joinstyle="round")
        self._text(20, center+130, heading, 13, colors["text"], "bold")
        self._text(20, center+156, self._clip(detail, self.width-40, 9),
                   9, colors["muted"])

    def _draw_detail(self) -> None:
        c, colors, w = self.canvas, self.colors, self.width
        quote = next((q for q in self.quotes if q.symbol == self.detail_symbol), None)
        self._icon("back", 30, 65, colors["surface"])
        if not quote:
            self._text(53, 65, "行情暂不可用", 11, colors["text"], "bold")
            return
        self._text(53, 65, self._clip(quote.name, w-150, 12, "bold"),
                   12, colors["text"], "bold")
        self._text(w-19, 65, quote.symbol, 9, colors["muted"],
                   anchor="e", font=NUMBER_FONT)
        tone = colors["down"] if quote.change_percent.startswith("-") else (
            colors["up"] if quote.change_percent not in ("", "--") else colors["text"])
        self._text(20, 98, _display_price(quote.price), 21, tone, "bold",
                   font=NUMBER_FONT)
        self._text(22, 121, _display_change(quote.change_percent),
                   10, tone, "bold", font=NUMBER_FONT)
        if self.inside:
            self._icon("remove", w-33, 98)
        c.create_line(20, 134, w-20, 134, fill=colors["line"])
        metric_pairs = (
            (("开盘", _price(quote.open_price)),
             ("收盘", _price(quote.close_price))),
            (("最高", _price(quote.high_price)),
             ("最低", _price(quote.low_price))),
            (("涨停", _price(quote.upper_limit)),
             ("跌停", _price(quote.lower_limit))),
            (("成交量", _compact_number(quote.volume)),
             ("成交额", _compact_number(quote.turnover))),
        )
        mid = w/2
        for group_index, pair in enumerate(metric_pairs):
            col = group_index % 2
            x = 20 if col == 0 else mid+6
            for pair_index, (label, value) in enumerate(pair):
                row = group_index//2*2 + pair_index
                y = 148 + row*19
                self._text(x, y, label, 9, colors["muted"])
                self._text(mid-12 if col == 0 else w-20, y, value, 10,
                           colors["text"], "bold", "e", font=NUMBER_FONT)
        c.create_line(20, 215, w-20, 215, fill=colors["line"])
        all_volumes = [v for side in (quote.ask_levels, quote.bid_levels)
                       for _, v in side if v is not None]
        max_volume = max(all_volumes, default=1)
        row_h = max(13, min(23, (self.height-260)//10))
        for i in range(10):
            y = 226 + i*row_h
            if i < 5:
                level = quote.ask_levels[4-i] if len(quote.ask_levels) == 5 else (None, None)
                label, ink, bar = f"卖{5-i}", colors["up"], colors["sell_bar"]
            else:
                level = quote.bid_levels[i-5] if len(quote.bid_levels) == 5 else (None, None)
                label, ink, bar = f"买{i-4}", colors["down"], colors["buy_bar"]
            self._book_row(20, w-20, y, label, level, max_volume,
                           ink, bar, row_h, f"book_{i}")
        c.create_line(20, 226+5*row_h-row_h/2, w-20,
                      226+5*row_h-row_h/2, fill=colors["line"])

    def _book_row(self, x1: float, x2: float, y: float, label: str,
                  level: tuple[float | None, float | None], max_volume: float,
                  ink: str, bar: str, row_h: int, tag: str) -> None:
        price, volume = level
        hovered = self.hover_tag == tag
        if hovered:
            self._round(x1, y-row_h/2, x2, y+row_h/2, 6,
                        self.colors["hover"], tag, alpha=105)
            self.canvas.create_line(x1+2, y-row_h/2+4, x1+2, y+row_h/2-4,
                                    fill=ink, width=2, capstyle="round")
        self._hit(x1, y-row_h/2, x2, y+row_h/2, tag)
        bar_x1, bar_x2 = x1+132, x2-61
        bar_y1, bar_y2 = y-4, y+4
        self._round(bar_x1, bar_y1, bar_x2, bar_y2, 3,
                    self.colors["book_track"])
        if volume:
            bar_width = max(3, (bar_x2-bar_x1)*volume/max_volume)
            self._round(bar_x1, bar_y1, min(bar_x2, bar_x1+bar_width), bar_y2, 3, bar)
        self._text(x1+5, y, label, 8 if row_h < 15 else 9, ink, "bold")
        self._text(x1+42, y, _price(price), 8 if row_h < 15 else 9,
                   self.colors["text"],
                   font=NUMBER_FONT)
        self._text(x2-4, y, _compact_number(volume), 8 if row_h < 15 else 9, ink,
                   anchor="e", font=NUMBER_FONT)

    def _draw_status(self) -> None:
        c, colors, w, h = self.canvas, self.colors, self.width, self.height
        c.create_line(17, h-37, w-17, h-37, fill=colors["line"])
        if self.inside and not self.detail_symbol \
                and self.headline_detail_item is None \
                and self.weibo_detail_item is None:
            if self.tab == "stocks":
                self._icon("add", 30, h-21, colors["surface"])
                self._icon("site", 64, h-21, colors["surface"])
            else:
                self._icon("refresh", 30, h-21, colors["surface"])
        if self.tab == "stocks":
            stamp = ("缓存 " + self._format_cache_time(self.stock_cache_at)
                     if self.feed_version <= 0 and self.stock_cache_at else
                     self.last_stock_update or "等待行情")
            status = f"{self.site_state} · {stamp}"
        elif self.tab == "cls":
            stamp = self._board_time(self.headlines) if self.headlines else ""
            status = self.refresh_status.get("cls") or self.headline_error or (
                "等待获取" if not stamp else "")
            status = " · ".join(part for part in (status, stamp) if part)
        else:
            board = self.hot.get(self.tab)
            stamp = self._board_time(board) if board else ""
            status = self.refresh_status.get(self.tab) or self.hot_error or (
                "等待获取" if not stamp else "")
            source = ("源 " + self._format_cache_time(board.source_at, "%H:%M")
                      if board and board.source_at else "")
            status = " · ".join(part for part in (
                board.origin if board else "", status, stamp, source) if part)
        self._text(w-27, h-21, self._clip(status, w-120, 9), 9,
                   colors["muted"], anchor="e", font=NUMBER_FONT)

    @staticmethod
    def _board_time(board: HotBoard | HeadlineBoard) -> str:
        return Widget._format_cache_time(board.fetched_at)

    def _draw_tooltip(self) -> None:
        tip = ICON_TIPS.get(self.tooltip_tag)
        if not tip:
            return
        x, icon_y = self.icon_locations.get(self.tooltip_tag, (30, 65))
        y = icon_y+29 if icon_y < self.height/2 else icon_y-32
        if self.tooltip_tag == "back":
            x, y = x+68, icon_y
        font = tkfont.Font(root=self.root, family=FONT, size=9)
        half = font.measure(tip)/2+9
        if self.focus_card or self.detail_symbol:
            left, right, top, bottom = 18, self.width-18, 61, self.height-18
        else:
            card = next(((cx, cy, cw, ch) for _, cx, cy, cw, ch
                         in self._dashboard_layout() if cy <= icon_y <= cy+ch), None)
            left, right, top, bottom = (card[0]+8, card[0]+card[2]-8,
                                        card[1]+8, card[1]+card[3]-8) if card else (
                                            10, self.width-10, 10, self.height-10)
        x = max(left+half, min(right-half, x))
        y = max(top+13, min(bottom-13, y))
        self._round(x-half, y-11, x+half, y+11, 6, self.colors["text"])
        self._text(x, y, tip, 9, self.colors["bg"], anchor="center")

    def _draw_popup(self) -> None:
        colors, w, h = self.colors, self.width, self.height
        if self.popup == "opacity":
            x1, x2 = w-85, w-15
            self._card(x1, 47, x2-x1, 183)
            cx = (x1+x2)/2
            self._text(cx, 66, f'{round(self.settings["opacity"]*100)}%',
                       9, colors["text"], anchor="center", font=NUMBER_FONT)
            self._hit(cx-21, 79, cx+21, 214, "opacity_slider",
                      colors["surface"])
            self.canvas.create_line(cx, 83, cx, 205, fill=colors["line"], width=5)
            knob_y = 205-(self.settings["opacity"]-.45)/.55*122
            self.canvas.create_line(cx, knob_y, cx, 205,
                                    fill=colors["accent"], width=5)
            self.canvas.create_oval(cx-7, knob_y-7, cx+7, knob_y+7,
                                    fill=colors["accent"], outline=colors["bg"],
                                    width=2, tags="opacity_slider")
        else:
            self._round(0, HEADER_HEIGHT, w, h, 0, colors["bg"], alpha=105)
            x1, y1, x2, y2 = self._popup_bounds()
            self._round(x1, y1, x2, y2, 18, colors["surface"],
                        alpha=255, shadow=True)
            self._hit(x1, y1, x2, y2, "popup_panel")
            label = "行情站点" if self.popup == "site" else "添加自选"
            self._text(26, y1+22, label, 11, colors["text"], "bold")
            self._icon("close", w-31, y1+22, tag="popup_cancel")
            input_y = y1+47
            if self.popup == "add":
                self._round(26, input_y, 134, input_y+34, 10,
                            colors["segment_bg"])
                for index, exchange in enumerate(("沪", "深", "北")):
                    left = 29+index*34
                    tag = f"exchange_{exchange}"
                    selected = exchange == self.exchange_choice.get()
                    if selected:
                        self._round(left, input_y+3, left+32, input_y+31, 8,
                                    colors["segment_active"], tag)
                    elif self.hover_tag == tag:
                        self._round(left, input_y+3, left+32, input_y+31, 8,
                                    colors["hover"], tag, alpha=100)
                    self._hit(left, input_y+3, left+32, input_y+31, tag)
                    self._text(left+16, input_y+17, exchange, 10,
                               colors["text"] if selected else colors["muted"],
                               "bold" if selected else "normal",
                               anchor="center", tag=tag)
                self._round(144, input_y, w-26, input_y+34, 10, colors["input"])
                hint = "选择交易所，输入 6 位代码"
                action = "添加"
            else:
                self._round(26, input_y, w-26, input_y+34, 10, colors["input"])
                hint = "例如 tcp://主机:端口"
                action = "保存"
            self._text(26, y2-21, self._clip(self.popup_error or hint,
                       w-135, 9), 9,
                       colors["up"] if self.popup_error else colors["muted"])
            self._round(w-92, y2-35, w-26, y2-9, 9,
                        colors["button_hover"] if self.hover_tag == "popup_save"
                        else colors["accent"], "popup_save")
            self._hit(w-92, y2-35, w-26, y2-9, "popup_save")
            self._text(w-59, y2-22, action, 9,
                       "#FFFFFF" if self.settings["theme"] == "light"
                       else colors["bg"], "bold", "center", "popup_save")
            if self.popup_entry:
                self.popup_entry.lift()

    def _popup_bounds(self) -> tuple[int, int, int, int]:
        return 14, self.height-180, self.width-14, self.height-47

    def _position_popup_entry(self) -> None:
        if not self.popup_entry:
            return
        input_y = self.height-180+47
        x = 155 if self.popup == "add" else 37
        self.popup_entry.place(x=x, y=input_y+7,
                               width=self.width-x-37, height=20)

    def _open_popup(self, kind: str) -> None:
        if self.popup == kind:
            self._dismiss_popup()
            return
        self._dismiss_popup()
        self.popup, self.popup_error = kind, ""
        if kind in ("site", "add"):
            colors = self.colors
            self.popup_entry = tk.Entry(self.root, font=(FONT, 10),
                                        bg=colors["input"], fg=colors["text"],
                                        insertbackground=colors["text"], relief="flat",
                                        borderwidth=0, highlightthickness=0,
                                        selectbackground=colors["accent"],
                                        selectforeground=colors["bg"])
            if kind == "add":
                self.exchange_choice.set("沪")
                self.popup_entry.configure(justify="center")
            self._position_popup_entry()
            if kind == "site":
                self.popup_entry.insert(0, self.settings["tora_front"])
            self.popup_entry.bind("<Return>", self._save_popup)
            self.popup_entry.bind("<Escape>", self._escape)
            self.popup_entry.focus_set()
        self._draw()

    def _choose_exchange(self, exchange: str) -> None:
        self.exchange_choice.set(exchange)
        self._draw()
        if self.popup_entry:
            self.popup_entry.focus_set()

    def _dismiss_popup(self) -> None:
        if self.popup_entry:
            self.popup_entry.destroy()
            self.popup_entry = None
        self.popup = ""
        self.popup_error = ""
        self.opacity_drag = False
        self._draw()

    def _save_popup(self, _event: tk.Event | None = None) -> None:
        if not self.popup_entry:
            return
        value = self.popup_entry.get().strip()
        try:
            if self.popup == "site":
                if value:
                    validate_front(value)
                if value != self.settings["tora_front"]:
                    self.settings["tora_front"] = value
                    config.save(self.settings)
                    self._stop_feed()
                    self._start_feed()
            elif self.popup == "add":
                if not re.fullmatch(r"\d{6}", value):
                    raise ValueError("请输入 6 位股票代码")
                exchange = {"沪": "SH", "深": "SZ", "北": "BJ"}[self.exchange_choice.get()]
                code = value
                symbol = exchange+code
                if symbol not in config.symbol_codes(self.settings):
                    if self.feed:
                        self.feed.add_symbol(symbol)
                    self.settings["symbols"].append({"symbol": symbol, "followed": True})
                    config.save(self.settings)
                    self.quotes.append(Quote(symbol, symbol, "", "",
                                             trading_day=self.market_day))
                    self.stock_error = "正在等待新股票行情"
                    self._draw()
            self._dismiss_popup()
        except (ValueError, OSError, RuntimeError) as exc:
            self.popup_error = str(exc)
            self._draw()

    def _set_opacity(self, y: int) -> None:
        value = .45+(205-min(205, max(83, y)))/122*.55
        self.settings["opacity"] = round(value, 2)
        self.root.attributes("-alpha", self.settings["opacity"])
        self._draw()

    def _tag_at(self, x: int, y: int) -> str:
        if self.popup in ("site", "add") and y >= HEADER_HEIGHT:
            x1, y1, x2, y2 = self._popup_bounds()
            if not (x1 <= x <= x2 and y1 <= y <= y2):
                return ""
        allowed = set(ICON_TIPS) | {"remove", "opacity_slider", "tab_stocks",
                                    "tab_weibo", "card_stocks", "card_weibo",
                                    "card_cls", "home", "popup_panel",
                                    "popup_save", "popup_cancel",
                                    "preview_close", "preview_prev", "preview_next"}
        for item in reversed(self.canvas.find_overlapping(x, y, x, y)):
            for tag in self.canvas.gettags(item):
                if y < HEADER_HEIGHT and (tag.startswith(("card_", "item_", "stock_"))
                               or tag in {"add", "site", "refresh",
                                          "refresh_weibo"}):
                    continue
                if tag in allowed or tag.startswith(
                        ("item_", "stock_", "follow_", "book_", "exchange_",
                         "post_", "video_", "image_", "expand_")):
                    return tag
        return ""

    def _resize_edge(self, x: int, y: int) -> str:
        """Use an invisible six-pixel resize zone on all four sides."""
        if x < 0 or y < 0 or x >= self.width or y >= self.height:
            return ""
        horizontal = "left" if x < 7 else "right" if x >= self.width-7 else ""
        vertical = "top" if y < 7 else "bottom" if y >= self.height-7 else ""
        if horizontal and vertical:
            return vertical + "_" + horizontal
        return horizontal or vertical

    def _press(self, event: tk.Event) -> None:
        tag = self._tag_at(event.x, event.y)
        edge = self._resize_edge(event.x, event.y)
        if edge:
            self.resize_origin = (event.x_root, event.y_root,
                                  self.width, self.height,
                                  self.root.winfo_x(), self.root.winfo_y(), edge)
        elif self.popup == "opacity" and self.width-71 <= event.x <= self.width-29 \
                and 79 <= event.y <= 214:
            self.opacity_drag = True
            self._set_opacity(event.y)
        elif tag == "close":
            self._close()
        elif tag == "popup_save":
            self._save_popup()
        elif tag == "popup_cancel":
            self._dismiss_popup()
        elif tag.startswith("exchange_"):
            self._choose_exchange(tag.removeprefix("exchange_"))
        elif tag == "popup_panel":
            pass
        elif tag == "theme":
            self.settings["theme"] = "dark" if self.settings["theme"] == "light" else "light"
            self.root.configure(bg=self.colors["bg"])
            self.canvas.configure(bg=self.colors["bg"])
            config.save(self.settings)
            self._dismiss_popup()
        elif tag in ("opacity", "site", "add"):
            self._open_popup(tag)
        elif tag == "refresh" or tag.startswith("refresh_"):
            self.refresh(self.tab)
        elif tag == "back":
            if self.detail_symbol:
                self.detail_symbol = None
            elif self.headline_detail_item is not None:
                self.headline_detail_index = None
                self.headline_detail_item = None
                self.headline_request_token += 1
            elif self.weibo_detail_item is not None:
                self._stop_weibo_video()
                self.weibo_detail_item = None
                self.weibo_request_token += 1
            else:
                self.focus_card = None
            self._draw()
        elif tag == "open_article" and self.headline_detail_item:
            webbrowser.open(self.headline_detail_item.url)
        elif tag == "open_weibo" and self.weibo_detail_item:
            webbrowser.open(self.weibo_detail_item.url)
        elif tag.startswith("expand_") and self.weibo_detail_item:
            index = int(tag[7:])
            if index in self.weibo_expanded:
                self.weibo_expanded.remove(index)
            else:
                self.weibo_expanded.add(index)
            _, total = self._weibo_detail_layout()
            self.weibo_scroll = min(self.weibo_scroll,
                                    max(0, total-(self.height-160)))
            self._draw()
        elif tag.startswith("image_") and self.weibo_detail_item:
            _, post_index, image_index = tag.split("_")
            selected = (int(post_index), int(image_index))
            self.weibo_preview = None if self.weibo_preview == selected else selected
            self._scroll_to_weibo_preview()
            self._draw()
        elif tag == "preview_close" and self.weibo_preview:
            self.weibo_preview = None
            self._draw()
        elif tag in ("preview_prev", "preview_next") and self.weibo_preview:
            post_index, image_index = self.weibo_preview
            count = len(self.weibo_posts[post_index].images)
            delta = -1 if tag == "preview_prev" else 1
            self.weibo_preview = (post_index, (image_index+delta) % count)
            self._draw()
        elif tag.startswith("post_") and self.weibo_detail_item:
            index = int(tag[5:])
            if 0 <= index < len(self.weibo_posts):
                webbrowser.open(self.weibo_posts[index].url)
        elif tag.startswith("video_") and self.weibo_detail_item:
            index = int(tag[6:])
            self._play_weibo_video(index)
        elif tag == "remove" and self.detail_symbol:
            self._remove_stock(self.detail_symbol)
        elif tag.startswith("follow_") and self.focus_card == "stocks" \
                and not self.detail_symbol:
            index = int(tag[7:])
            if index < len(self.quotes):
                self._toggle_follow(self.quotes[index].symbol)
        elif tag.startswith("card_"):
            self._select_tab(tag[5:])
        elif tag.startswith("tab_"):
            self._select_tab(tag[4:])
        elif tag.startswith("stock_") and (not self.focus_card or self.tab == "stocks"):
            index = int(tag[6:])
            if index < len(self.quotes):
                self.tab = "stocks"
                self.detail_symbol = self.quotes[index].symbol
                self._dismiss_popup()
        elif tag.startswith("item_"):
            parts = tag.split("_")
            platform = self.tab if len(parts) == 2 else "_".join(parts[1:-1])
            index = int(parts[-1])
            if platform == "cls":
                self._open_headline(index)
            elif platform == "weibo":
                self._open_weibo(index)
            else:
                board = self.hot.get(platform)
                if board and index < len(board.items) and board.items[index].url:
                    webbrowser.open(board.items[index].url)
        elif self.popup:
            self._dismiss_popup()
        elif event.y < 48:
            self.drag_origin = (event.x_root-self.root.winfo_x(),
                                event.y_root-self.root.winfo_y())

    def _select_tab(self, tab: str) -> None:
        self._stop_weibo_video()
        self.tab, self.scroll, self.hover_tag = tab, 0, ""
        self.detail_symbol = None
        self.headline_detail_index = None
        self.headline_detail_item = None
        self.headline_request_token += 1
        self.weibo_detail_item = None
        self.weibo_preview = None
        self.weibo_request_token += 1
        self.focus_card = tab
        self._dismiss_popup()

    def _remove_stock(self, symbol: str) -> None:
        previous_symbols = self.settings["symbols"]
        try:
            if self.feed:
                self.feed.remove_symbol(symbol)
            self.settings["symbols"] = [row for row in self.settings["symbols"]
                                        if row["symbol"] != symbol]
            config.save(self.settings)
        except (ValueError, OSError, RuntimeError) as exc:
            self.settings["symbols"] = previous_symbols
            self.stock_error = str(exc)
            self._draw()
            return
        self.quotes = [quote for quote in self.quotes if quote.symbol != symbol]
        save_market_cache(self.quotes, self.stock_cache_at, self.market_day)
        self.detail_symbol = None
        self.scroll = min(self.scroll, max(0, len(self.quotes)-self._stock_visible_rows()))
        self._draw()

    def _toggle_follow(self, symbol: str) -> None:
        row = next((row for row in self.settings["symbols"]
                    if row["symbol"] == symbol), None)
        if row is None:
            return
        row["followed"] = not row["followed"]
        try:
            config.save(self.settings)
        except OSError as exc:
            row["followed"] = not row["followed"]
            self.stock_error = str(exc)
        self._draw()

    def _motion(self, event: tk.Event) -> None:
        if not self.inside:
            self.inside = True
            self._draw()
            self._animate_brand()
        tag = self._tag_at(event.x, event.y)
        edge = self._resize_edge(event.x, event.y)
        if edge in ("left", "right"):
            cursor = "sb_h_double_arrow"
        elif edge in ("top", "bottom"):
            cursor = "sb_v_double_arrow"
        elif edge in ("top_left", "bottom_right"):
            cursor = "size_nw_se"
        elif edge:
            cursor = "size_ne_sw"
        else:
            cursor = "hand2" if tag and not tag.startswith("book_") \
                and tag != "popup_panel" else "fleur" if event.y < 48 else "arrow"
        self.canvas.configure(cursor=cursor)
        if tag != self.hover_tag:
            previous = self.hover_tag
            self.hover_tag = tag
            self.tooltip_tag = ""
            if self.tooltip_after:
                self.root.after_cancel(self.tooltip_after)
                self.tooltip_after = None
            if tag in ICON_TIPS:
                self.tooltip_after = self.root.after(
                    450, lambda target=tag: self._show_tooltip(target))
            if (self.tab == "cls" and self.focus_card == "cls"
                    and self.headline_detail_item is None and not self.popup
                    and not self.tooltip_tag
                    and (not previous or previous.startswith("item_"))
                    and (not tag or tag.startswith("item_"))):
                if previous:
                    self.canvas.itemconfigure(f"headline_hover_{previous}",
                                              state="hidden")
                if tag:
                    self.canvas.itemconfigure(f"headline_hover_{tag}",
                                              state="normal")
            else:
                self._draw()

    def _show_tooltip(self, tag: str) -> None:
        self.tooltip_after = None
        if self.inside and self.hover_tag == tag and not self.popup:
            self.tooltip_tag = tag
            self._draw()

    def _leave(self, _event: tk.Event) -> None:
        self.root.after(40, self._check_leave)

    def _check_leave(self) -> None:
        x, y = self.root.winfo_pointerxy()
        if self.inside and not (
                self.root.winfo_rootx() <= x < self.root.winfo_rootx()+self.width
                and self.root.winfo_rooty() <= y < self.root.winfo_rooty()+self.height):
            self.inside = False
            self.hover_tag = self.tooltip_tag = ""
            if not self.popup_entry:
                self._dismiss_popup()
            else:
                self._draw()
            self._animate_brand()

    def _brand_target_x(self) -> float:
        return 16 if self.inside else (self.width-self._brand_width)/2

    def _animate_brand(self) -> None:
        if self._brand_after:
            self.root.after_cancel(self._brand_after)
            self._brand_after = None
        self._brand_from = self._brand_x
        self._brand_to = self._brand_target_x()
        self._brand_started = time.perf_counter()
        if abs(self._brand_to-self._brand_from) > .5:
            self._step_brand()

    def _step_brand(self) -> None:
        elapsed = time.perf_counter()-self._brand_started
        progress = min(1.0, elapsed/.26)
        eased = progress*progress*(3-2*progress)
        next_x = self._brand_from+(self._brand_to-self._brand_from)*eased
        self.canvas.move("brand", next_x-self._brand_x, 0)
        self._brand_x = next_x
        if progress < 1:
            self._brand_after = self.root.after(16, self._step_brand)
        else:
            self._brand_after = None

    def _escape(self, _event: tk.Event | None = None) -> None:
        if self.popup:
            self._dismiss_popup()
        elif self.detail_symbol:
            self.detail_symbol = None
            self._draw()
        elif self.headline_detail_item is not None:
            self.headline_detail_index = None
            self.headline_detail_item = None
            self.headline_request_token += 1
            self._draw()
        elif self.weibo_detail_item is not None:
            if self.weibo_preview is not None:
                self.weibo_preview = None
                self._draw()
                return
            self._stop_weibo_video()
            self.weibo_detail_item = None
            self.weibo_request_token += 1
            self._draw()
        elif self.focus_card:
            self.focus_card = None
            self._draw()
        # Esc never closes the widget.

    def _drag(self, event: tk.Event) -> None:
        if self.opacity_drag:
            self._set_opacity(event.y)
        elif self.resize_origin:
            mouse_x, mouse_y, w0, h0, window_x, window_y, edge = self.resize_origin
            dx, dy = event.x_root-mouse_x, event.y_root-mouse_y
            x, y = window_x, window_y
            if "left" in edge:
                self.width = max(MIN_WIDTH, min(MAX_WIDTH, w0-dx))
                x += w0-self.width
            elif "right" in edge:
                self.width = max(MIN_WIDTH, min(MAX_WIDTH, w0+dx))
            if "top" in edge:
                self.height = max(MIN_HEIGHT, min(MAX_HEIGHT, h0-dy))
                y += h0-self.height
            elif "bottom" in edge:
                self.height = max(MIN_HEIGHT, min(MAX_HEIGHT, h0+dy))
            if self._brand_after:
                self.root.after_cancel(self._brand_after)
                self._brand_after = None
            self._brand_x = self._brand_target_x()
            self.root.geometry(f"{self.width}x{self.height}+{x}+{y}")
            self.canvas.configure(width=self.width, height=self.height)
            # SetWindowRgn forces a native window update. Let Tk apply the
            # requested position first, or Windows resets left/top drags.
            self.root.update_idletasks()
            self._round_window()
            self.page_scroll = min(self.page_scroll, self._dashboard_scroll_limit())
            self._position_popup_entry()
            self._draw()
        elif self.drag_origin:
            self.root.geometry(f"+{event.x_root-self.drag_origin[0]}+"
                               f"{event.y_root-self.drag_origin[1]}")

    def _release(self, _event: tk.Event) -> None:
        if self.opacity_drag:
            self.opacity_drag = False
            config.save(self.settings)
        if self.resize_origin:
            self.resize_origin = None
            self.settings["width"], self.settings["height"] = self.width, self.height
            self.settings["x"], self.settings["y"] = self.root.winfo_x(), self.root.winfo_y()
            config.save(self.settings)
            self._glass_key = None
            self._draw()
        if self.drag_origin:
            self.drag_origin = None
            self.settings["x"], self.settings["y"] = self.root.winfo_x(), self.root.winfo_y()
            config.save(self.settings)
            self._glass_key = None
            self._draw()

    def _wheel(self, event: tk.Event) -> None:
        self._scroll_by(-2 if event.delta > 0 else 2)

    def _scroll_by(self, amount: int) -> None:
        if self.detail_symbol:
            return
        if self.headline_detail_item is not None:
            _, total = self._headline_article_layout()
            limit = max(0, total-(self.height-160))
            self.article_scroll = max(0, min(limit,
                                             self.article_scroll+amount*30))
            self._draw()
            return
        if self.weibo_detail_item is not None:
            _, total = self._weibo_detail_layout()
            limit = max(0, total-(self.height-160))
            self.weibo_scroll = max(0, min(limit,
                                           self.weibo_scroll+amount*30))
            self._draw()
            return
        if not self.focus_card:
            self.page_scroll = max(0, min(self._dashboard_scroll_limit(),
                                          self.page_scroll+amount*34))
            self._draw()
            return
        if self.tab == "stocks":
            limit = max(0, len(self.quotes)-self._stock_visible_rows())
        elif self.tab == "cls":
            limit = max(0, len(self.headlines.items)-self._headline_visible_rows()) \
                if self.headlines else 0
        else:
            board = self.hot.get(self.tab)
            limit = max(0, len(board.items)-self._hot_visible_rows()) if board else 0
        new_scroll = max(0, min(limit, self.scroll+amount))
        if new_scroll != self.scroll:
            self.scroll = new_scroll
            self._draw()

    def refresh(self, source: str | None = None) -> None:
        if source in (None, "weibo") and not self.hot_busy:
            self.hot_busy = True
            self.refresh_status["weibo"] = "正在刷新"
            threading.Thread(target=self._fetch_hot, daemon=True).start()
        if source in (None, "cls") and not self.headline_busy:
            self.headline_busy = True
            self.refresh_status["cls"] = "正在刷新"
            threading.Thread(target=self._fetch_cls, daemon=True).start()
        if source in (None, "stocks") and self.feed is None and self.settings["tora_front"]:
            self._start_feed()
        self._draw()

    def _fetch_hot(self) -> None:
        try:
            self.events.put(("hot", fetch_hot(self.settings["hot_url"])))
        except Exception as exc:
            self.events.put(("hot_error", str(exc)))

    def _fetch_cls(self) -> None:
        try:
            self.events.put(("cls", fetch_cls_headlines()))
        except Exception as exc:
            self.events.put(("cls_error", str(exc)))

    def _start_feed(self) -> None:
        if self.feed is not None or not self.settings["tora_front"]:
            return
        self.site_state = "站点连接中"
        self.feed_token += 1
        token = self.feed_token
        try:
            feed = ToraFeed(self.settings["tora_front"], config.symbol_codes(self.settings),
                            lambda status: self.events.put(("feed_status", (token, status))))
            self.feed = feed
            threading.Thread(target=self._run_feed, args=(feed, token), daemon=True).start()
        except Exception as exc:
            self.stock_error = str(exc)
            self.site_state = "站点连接失败"
            self._draw()

    def _run_feed(self, feed: ToraFeed, token: int) -> None:
        try:
            feed.start()
        except Exception as exc:
            self.events.put(("feed_failure", (token, str(exc))))

    def _stop_feed(self) -> None:
        feed = self.feed
        self.feed = None
        self.feed_token += 1
        self.feed_version = -1
        self.site_state = "站点未连接" if self.settings["tora_front"] else "站点未配置"
        if feed:
            threading.Thread(target=feed.close, daemon=True).start()

    def _close(self) -> None:
        self._stop_weibo_video()
        for path in self.weibo_video_temp_files:
            try:
                os.remove(path)
            except OSError:
                pass
        if self._brand_after:
            self.root.after_cancel(self._brand_after)
        if self.quotes:
            save_market_cache(self.quotes, self.stock_cache_at, self.market_day)
        self._stop_feed()
        self.root.destroy()

    def _process_events(self) -> None:
        dirty = False
        try:
            while True:
                kind, value = self.events.get_nowait()
                dirty = True
                if kind == "hot":
                    self.hot = value  # type: ignore[assignment]
                    self.hot_error = ""
                    self.hot_busy = False
                    board = self.hot.get("weibo")
                    self.refresh_status["weibo"] = (
                        "刷新失败 · 已显示缓存" if board and board.items and board.error
                        else "刷新完成" if board and board.items
                        else "刷新失败")
                elif kind == "hot_error":
                    self.hot_error = str(value)
                    self.hot_busy = False
                    self.refresh_status["weibo"] = (
                        "刷新失败 · 已显示缓存" if self.hot.get("weibo") else "刷新失败")
                elif kind == "cls":
                    self.headlines = value  # type: ignore[assignment]
                    self.headline_error = ""
                    self.headline_busy = False
                    self.refresh_status["cls"] = (
                        "刷新完成" if not self.headlines.error else
                        "刷新失败 · 已显示缓存")
                elif kind == "cls_error":
                    self.headline_error = str(value)
                    self.headline_busy = False
                    self.refresh_status["cls"] = (
                        "刷新失败 · 已显示缓存" if self.headlines else "刷新失败")
                elif kind == "cls_article":
                    token, body, error = value  # type: ignore[misc]
                    if token == self.headline_request_token:
                        self.headline_loading = False
                        self.headline_article_error = error
                        if body:
                            self.headline_body = body
                        self._article_layout_cache = None
                        self.article_scroll = 0
                elif kind == "weibo_posts":
                    token, keyword, posts, error, final = value  # type: ignore[misc]
                    if token == self.weibo_request_token:
                        if not self.weibo_posts:
                            self.weibo_scroll = 0
                        self.weibo_posts = posts
                        self.weibo_posts_error = error
                        self.weibo_posts_loading = False
                        if final and posts:
                            self.weibo_post_cache[keyword] = (time.monotonic(), posts)
                elif kind == "weibo_image":
                    token, url, data = value  # type: ignore[misc]
                    if token == self.weibo_request_token:
                        self.weibo_image_bytes[url] = data
                elif kind == "weibo_video":
                    token, index, path, error = value  # type: ignore[misc]
                    if token == self.weibo_video_token and self.weibo_detail_item:
                        self.weibo_video_downloading = None
                        self.weibo_video_error = error
                        self.weibo_video_error_index = index if error else None
                        if path:
                            self.weibo_playing_video = index
                            self.weibo_video_file = path
                            self.weibo_video_cache[self.weibo_posts[index].video_url] = path
                elif kind == "feed_status":
                    token, status = value  # type: ignore[misc]
                    if token == self.feed_token:
                        self.stock_error = status
                        self.site_state = _site_state_from_feed_status(status)
                elif kind == "feed_failure":
                    token, error = value  # type: ignore[misc]
                    if token == self.feed_token:
                        self.stock_error = "行情接口错误：" + error
                        self._stop_feed()
                        self.site_state = "站点连接失败"
        except queue.Empty:
            pass
        if dirty:
            self._draw()
        if self.feed:
            version, quotes, updated, feed_day = self.feed.snapshot()
            if version != self.feed_version:
                self.feed_version = version
                if quotes or feed_day:
                    rolled, blanked, merged, day = reconcile_quotes(
                        config.symbol_codes(self.settings), self.quotes, self.market_day,
                        quotes, feed_day)
                    if rolled:
                        self.quotes = blanked
                        self.market_day = day
                        self.last_stock_update = ""
                        self.stock_cache_at = datetime.now(timezone.utc).isoformat()
                        save_market_cache(self.quotes, self.stock_cache_at, day)
                    if merged != self.quotes:
                        self.quotes = merged
                        self.market_day = day
                        self.last_stock_update = updated
                        self.stock_cache_at = datetime.now(timezone.utc).isoformat()
                        now = time.monotonic()
                        if rolled or now-self._last_market_save >= 10:
                            save_market_cache(self.quotes, self.stock_cache_at, day)
                            self._last_market_save = now
                if not self.focus_card or self.tab == "stocks":
                    self._draw()
        self.root.after(100, self._process_events)

    def _tick(self) -> None:
        try:
            self.refresh()
        finally:
            self.root.after(self.settings["refresh_seconds"]*1000, self._tick)

    def run(self) -> None:
        self.root.mainloop()


if __name__ == "__main__":
    Widget().run()
