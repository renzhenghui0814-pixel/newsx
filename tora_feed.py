"""Adapter for the user-supplied TORA XMdApi Python SDK (v1.0.9)."""

from __future__ import annotations

import math
import re
import sys
import threading
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Callable


@dataclass(frozen=True)
class Quote:
    symbol: str
    name: str
    price: str
    change_percent: str
    turnover: float | None = None
    volume: float | None = None
    open_price: float | None = None
    high_price: float | None = None
    low_price: float | None = None
    close_price: float | None = None
    upper_limit: float | None = None
    lower_limit: float | None = None
    bid_levels: tuple[tuple[float | None, float | None], ...] = ()
    ask_levels: tuple[tuple[float | None, float | None], ...] = ()
    trading_day: str = ""

SDK = Path(__file__).resolve().parent / "vendor" / "PythonAPI_20260915"
sys.path.insert(0, str(SDK))
sys.path.insert(0, str(SDK / "x64" if sys.maxsize > 2**32 else SDK / "x86"))

import xmdapi  # noqa: E402

EXCHANGES = {
    "SH": xmdapi.TORA_TSTP_EXD_SSE,
    "SZ": xmdapi.TORA_TSTP_EXD_SZSE,
    "BJ": xmdapi.TORA_TSTP_EXD_BSE,
}


def normalize_symbol(value: str) -> tuple[str, str]:
    """Accept SH600519, 600519.SH, or a six-digit A-share code."""
    value = value.strip().upper()
    match = re.fullmatch(r"(SH|SZ|BJ)(\d{6})", value) or re.fullmatch(r"(\d{6})\.(SH|SZ|BJ)", value)
    if match:
        a, b = match.groups()
        return (a, b) if a in EXCHANGES else (b, a)
    if re.fullmatch(r"\d{6}", value):
        if value.startswith("6"):
            return "SH", value
        if value.startswith(("0", "3")):
            return "SZ", value
        if value.startswith(("4", "8", "9")):
            return "BJ", value
    raise ValueError(f"无法识别股票代码：{value}，请用 SH600519、SZ000001 或 BJxxxxxx")


def validate_front(value: str) -> str:
    value = value.strip()
    for part in value.split(","):
        if not re.fullmatch(r"tcp://[A-Za-z0-9_.-]+:\d{1,5}", part.strip()):
            raise ValueError("前置地址格式应为 tcp://主机:端口，可用逗号分隔多个地址")
        if not 1 <= int(part.rsplit(":", 1)[1]) <= 65535:
            raise ValueError("前置端口超出范围")
    return value


def _text(value: object) -> str:
    if isinstance(value, bytes):
        for encoding in ("utf-8", "gbk"):
            try:
                return value.decode(encoding).strip("\x00 ")
            except UnicodeDecodeError:
                pass
        return value.decode("utf-8", "replace").strip("\x00 ")
    return str(value).strip("\x00 ")


def _positive(data: object, field: str) -> float | None:
    try:
        number = float(getattr(data, field))
        return number if math.isfinite(number) and 0 < number < 1e15 else None
    except (AttributeError, TypeError, ValueError):
        return None


def _trading_day(value: object) -> str:
    day = _text(value)
    return day if re.fullmatch(r"\d{8}", day) else ""


def quote_from_market_data(data: object, symbol: str) -> Quote | None:
    price = float(getattr(data, "LastPrice"))
    previous = float(getattr(data, "PreClosePrice"))
    if not math.isfinite(price) or price <= 0 or price > 1e10:
        return None
    change = f"{(price / previous - 1) * 100:+.2f}" if math.isfinite(previous) and 0 < previous < 1e10 else "--"
    name = _text(getattr(data, "SecurityName", "")) or symbol
    trading_day = _trading_day(getattr(data, "TradingDay", ""))
    levels = lambda side: tuple((_positive(data, f"{side}Price{i}"),
                                  _positive(data, f"{side}Volume{i}"))
                                 for i in range(1, 6))
    return Quote(symbol, name, f"{price:.2f}", change,
                 turnover=_positive(data, "Turnover"),
                 volume=_positive(data, "Volume"),
                 open_price=_positive(data, "OpenPrice"),
                 high_price=_positive(data, "HighestPrice"),
                 low_price=_positive(data, "LowestPrice"),
                 close_price=_positive(data, "ClosePrice"),
                 upper_limit=_positive(data, "UpperLimitPrice"),
                 lower_limit=_positive(data, "LowerLimitPrice"),
                 bid_levels=levels("Bid"), ask_levels=levels("Ask"),
                 trading_day=trading_day)


class ToraFeed:
    def __init__(self, front: str, symbols: list[str], on_status: Callable[[str], None]) -> None:
        self.front = validate_front(front)
        self.watchlist = [normalize_symbol(s) for s in symbols]
        self.on_status = on_status
        self.api = None
        self.spi = None
        self.active = False
        self.logged_in = False
        self._lock = threading.Lock()
        self._quotes: dict[str, Quote] = {}
        self._version = 0
        self.updated_at = ""
        self.trading_day = ""

    def start(self) -> None:
        self.active = True
        api = xmdapi.CTORATstpXMdApi.CreateTstpXMdApi()
        self.api = api
        owner = self

        class Spi(xmdapi.CTORATstpXMdSpi):
            def __init__(self):
                xmdapi.CTORATstpXMdSpi.__init__(self)

            def OnFrontConnected(self):
                if not owner.active:
                    return
                owner.on_status("已连接，正在登录")
                ret = api.ReqUserLogin(xmdapi.CTORATstpReqUserLoginField(), 1)
                if ret != 0:
                    owner.on_status(f"登录请求失败：{ret}")

            def OnFrontDisconnected(self, reason):
                with owner._lock:
                    owner.logged_in = False
                if owner.active:
                    owner.on_status(f"连接断开：{reason}，等待重连")

            def OnRspUserLogin(self, _login, info, _request_id):
                if not owner.active:
                    return
                if info.ErrorID != 0:
                    owner.on_status(f"登录失败：{info.ErrorID} {_text(info.ErrorMsg)}")
                    return
                with owner._lock:
                    owner.logged_in = True
                    day = _trading_day(getattr(_login, "TradingDay", ""))
                    if day > owner.trading_day:
                        owner.trading_day = day
                        owner._version += 1
                    watchlist = list(owner.watchlist)
                owner.on_status("已登录，正在订阅")
                grouped: dict[str, list[bytes]] = {key: [] for key in EXCHANGES}
                for exchange, code in watchlist:
                    grouped[exchange].append(code.encode("ascii"))
                for exchange, codes in grouped.items():
                    if codes:
                        ret = api.SubscribeMarketData(codes, EXCHANGES[exchange])
                        if ret != 0:
                            owner.on_status(f"{exchange} 订阅失败：{ret}")
                owner.on_status("实时行情已订阅")

            def OnRspSubMarketData(self, _security, info):
                if owner.active and info.ErrorID != 0:
                    owner.on_status(f"订阅失败：{info.ErrorID} {_text(info.ErrorMsg)}")

            def OnRtnMarketData(self, data):
                if not owner.active:
                    return
                code = _text(data.SecurityID)
                with owner._lock:
                    matches = [exchange for exchange, candidate in owner.watchlist if candidate == code]
                if not matches:
                    return
                # The same numeric code can exist on several exchanges; use the
                # exchange field where the SDK exposes a matching enum value.
                exchange = next((name for name in matches if data.ExchangeID == EXCHANGES[name]), matches[0])
                symbol = exchange + code
                try:
                    quote = quote_from_market_data(data, symbol)
                except (AttributeError, TypeError, ValueError):
                    quote = None
                if quote is None:
                    day = _trading_day(getattr(data, "TradingDay", ""))
                    if not day:
                        return
                    quote = Quote(symbol, _text(getattr(data, "SecurityName", "")) or symbol,
                                  "", "", trading_day=day)
                with owner._lock:
                    if (exchange, code) not in owner.watchlist:
                        return
                    old = owner._quotes.get(symbol)
                    if old and old.trading_day == quote.trading_day and old.price and not quote.price:
                        return
                    owner._quotes[symbol] = quote
                    if quote.trading_day > owner.trading_day:
                        owner.trading_day = quote.trading_day
                    owner._version += 1
                    owner.updated_at = datetime.now().strftime("%H:%M:%S")

        self.spi = Spi()
        api.RegisterSpi(self.spi)
        api.RegisterFront(self.front)
        self.on_status("正在连接行情前置")
        api.Init()

    def add_symbol(self, symbol: str) -> bool:
        """Subscribe on the existing API session; never recreate the feed."""
        exchange, code = normalize_symbol(symbol)
        with self._lock:
            if (exchange, code) in self.watchlist:
                return False
            self.watchlist.append((exchange, code))
            api = self.api if self.logged_in else None
        if api is not None:
            ret = api.SubscribeMarketData([code.encode("ascii")], EXCHANGES[exchange])
            if ret != 0:
                with self._lock:
                    self.watchlist.remove((exchange, code))
                raise RuntimeError(f"订阅失败：{ret}")
        return True

    def remove_symbol(self, symbol: str) -> bool:
        """Unsubscribe and discard cached quotes without disrupting other symbols."""
        exchange, code = normalize_symbol(symbol)
        with self._lock:
            if (exchange, code) not in self.watchlist:
                return False
            api = self.api if self.logged_in else None
        if api is not None:
            ret = api.UnSubscribeMarketData([code.encode("ascii")], EXCHANGES[exchange])
            if ret != 0:
                raise RuntimeError(f"退订失败：{ret}")
        with self._lock:
            self.watchlist.remove((exchange, code))
            self._quotes.pop(exchange+code, None)
            self._version += 1
        return True

    def snapshot(self) -> tuple[int, list[Quote], str, str]:
        with self._lock:
            ordered = [self._quotes[exchange+code] for exchange, code in self.watchlist if exchange+code in self._quotes]
            return self._version, ordered, self.updated_at, self.trading_day

    def close(self) -> None:
        self.active = False
        self.logged_in = False
        if self.api is not None:
            self.api.Release()
            self.api = None
        self.spi = None
