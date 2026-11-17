#include "market/quote_store.h"
#include "market/symbol.h"

#include <cassert>

using namespace newsx::market;

int main() {
    assert(parse_symbol("600519")->full() == "SH600519");
    assert(parse_symbol("000001.SZ")->full() == "SZ000001");
    assert(parse_symbol("BJ430047")->tora_exchange() == '4');
    assert(!parse_symbol("123"));
    assert(valid_fronts("tcp://host:6402,tcp://backup:6403"));
    assert(!valid_fronts("tcp://host:70000"));

    QuoteStore store;
    assert(store.set_trading_day("20261112"));
    Quote first;
    first.symbol = "SH600519";
    first.name = "Moutai";
    first.trading_day = "20261112";
    first.price = 1500.0;
    assert(store.update(first));
    assert(store.set_trading_day("20261113"));
    auto snapshot = store.ordered({"SH600519"});
    assert(snapshot.size() == 1 && !snapshot[0].price && snapshot[0].name == "Moutai");
    assert(!store.update(first));
    first.trading_day = "20261113";
    first.price = 1510.0;
    assert(store.update(first));
    Quote empty = first;
    empty.price.reset();
    assert(!store.update(empty));
    store.erase("SH600519");
    assert(store.ordered({"SH600519"}).empty());
}
