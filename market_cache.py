"""Persist stock quotes in the shared cache file."""

from __future__ import annotations

from dataclasses import asdict

from cache_store import load_record, parse_cache_time, save_record
from tora_feed import Quote


def blank_quote(quote: Quote, trading_day: str) -> Quote:
    """Keep identity while removing every value from a previous trading day."""
    return Quote(quote.symbol, quote.name, "", "", trading_day=trading_day)


def reconcile_quotes(symbols: list[str], cached: list[Quote], cached_day: str,
                     incoming: list[Quote], feed_day: str = ""
                     ) -> tuple[bool, list[Quote], list[Quote], str]:
    """Select one trading day and prevent old feed entries from filling blank rows."""
    incoming_day = max([feed_day] + [quote.trading_day for quote in incoming
                                    if quote.trading_day])
    if incoming_day and cached_day and incoming_day < cached_day:
        return False, cached, cached, cached_day
    day = incoming_day or cached_day
    rollover = bool(incoming_day and cached and incoming_day != cached_day)
    base = [blank_quote(quote, day) for quote in cached] if rollover else cached
    accepted = {quote.symbol: quote for quote in incoming
                if not day or quote.trading_day == day}
    by_symbol = {quote.symbol: quote for quote in base}
    by_symbol.update(accepted)
    merged = [by_symbol[symbol] for symbol in symbols if symbol in by_symbol]
    return rollover, base, merged, day


def load_market_cache(symbols: list[str]) -> tuple[list[Quote], str, str]:
    record = load_record("stocks")
    if not record:
        return [], "", ""
    saved_at = parse_cache_time(record.get("time", ""))
    trading_day = str(record.get("trading_day", ""))
    if not saved_at:
        return [], "", ""
    wanted = set(symbols)
    quotes = []
    for row in record["items"]:
        if not isinstance(row, dict) or row.get("symbol") not in wanted:
            continue
        try:
            quote = Quote(**{
                **row,
                "trading_day": row.get("trading_day") or trading_day,
                "bid_levels": tuple(tuple(level) for level in row.get("bid_levels", ())),
                "ask_levels": tuple(tuple(level) for level in row.get("ask_levels", ())),
            })
        except (TypeError, ValueError):
            continue
        quotes.append(quote)
    by_symbol = {quote.symbol: quote for quote in quotes}
    return [by_symbol[symbol] for symbol in symbols if symbol in by_symbol], saved_at, trading_day


def save_market_cache(quotes: list[Quote], saved_at: str, trading_day: str) -> None:
    save_record("stocks", saved_at, [asdict(quote) for quote in quotes], trading_day)
