# Committed Deribit test data

Real BTC option-chain snapshots from Deribit's public REST API (no authentication). They make
the tests, `vol_surface` and the docs reproducible offline. This is public Deribit market data;
the repo owner should read Deribit's API terms of use before publishing the repo.

## What is here

| Directory | Fetched (UTC) | Content | Size (`du -sk`) |
|---|---|---|---|
| `BTC-20261017T123254Z/` | 2026-10-17 12:32:54 | full BTC chain, trimmed to the 8 nearest expiries: 524 options | 240 KB |
| `series/BTC-20261017T123312Z/` ... `series/BTC-20261017T125812Z/` | 12:33:12 to 12:58:12, every 300 s | 6 snapshots, expiries 19OCT26 and 20OCT26 only: 114 options each | 384 KB total |

Expiries in the full-chain copy: 18OCT26, 19OCT26, 20OCT26, 21OCT26, 23OCT26, 30OCT26, 6NOV26,
27NOV26. Dropped to stay inside the 400 KB budget: 25DEC26, 26MAR27, 25JUN27, 24SEP27 (the
trimmed full chain with all 12 expiries and 938 options was 420 KB).

The series expiries are the 2nd and 3rd nearest expiries with at least 10 strikes in the first
series snapshot (18OCT26 was the nearest).

## How it was made

```bash
# full chain (raw files go to data/deribit/, which is gitignored)
scripts/fetch_deribit.sh --count 1
scripts/trim_snapshot.sh data/deribit/BTC-20261017T123254Z tests/data/deribit \
  --expiries 18OCT26,19OCT26,20OCT26,21OCT26,23OCT26,30OCT26,6NOV26,27NOV26

# series: 6 snapshots 300 s apart (~25 minutes)
scripts/fetch_deribit.sh --count 6 --interval 300 --out data/deribit/series-raw
first=$(ls -d data/deribit/series-raw/BTC-* | head -1)
jq -r '[.result[] | {e: .expiration_timestamp, t: (.instrument_name|split("-")[1]), k: .strike}]
       | group_by(.e) | map({t: .[0].t, n: (map(.k)|unique|length)})
       | map(select(.n >= 10)) | .[1:3] | map(.t) | join(",")' "$first"/instruments.json
# -> 19OCT26,20OCT26
for d in data/deribit/series-raw/BTC-*; do
  scripts/trim_snapshot.sh "$d" tests/data/deribit/series --expiries 19OCT26,20OCT26
done
```

`trim_snapshot.sh` only drops fields and rows (with `jq`); it never changes a value and keeps
the directory name.

## Files and fields

Each snapshot directory holds:

- `instruments.json`: `public/get_instruments?currency=BTC&kind=option&expired=false`. Kept
  per row: `instrument_name`, `kind`, `option_type` (`call`/`put`), `strike` (USD),
  `expiration_timestamp` (ms since epoch, UTC; expiries are 08:00 UTC), `instrument_type`
  (`reversed` = inverse option), `settlement_currency` (`BTC`).
- `book_summary.json`: `public/get_book_summary_by_currency?currency=BTC&kind=option`. Kept
  per row: `instrument_name`, `bid_price` and `ask_price` (BTC per 1 BTC notional, `null` when
  there is no quote), `mark_price` (BTC), `mark_iv` (percent), `underlying_price` (USD forward
  of the expiry: the future, or a synthetic forward when no future is listed),
  `underlying_index` (name of that future or `SYN.` forward), `interest_rate` (0.0),
  `creation_timestamp` (ms, UTC), `open_interest` (contracts).
- `index_price.json`: `public/get_index_price?index_name=btc_usd`, unchanged.
- `meta.json`: fetch time, currency, URLs, script version, `curl --version` first line.

Both JSON files also keep the response envelope fields `usIn` and `usOut` (server receive and
send time, microseconds). The conventions used to turn these prices into vols are in
[docs/conventions.md](../../../docs/conventions.md#deribit-conventions).
