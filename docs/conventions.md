# Conventions

## Units

These units hold everywhere in the library (`include/opt/pricing/types.hpp` states them too).

| Quantity | Meaning | Unit in the library | How tools print it |
|---|---|---|---|
| `time` (T) | time to expiry | years | years |
| `rate` (r) | risk-free rate | annualised, continuously compounded | same |
| `carry_yield` (q) | dividend or carry yield | annualised, continuously compounded | same |
| forward F | F = S e^{(r-q)T} | price units | same |
| discount D | D = e^{-rT} | none | same |
| delta | dV/dS | per 1 unit of spot | same |
| gamma | d2V/dS2 | per unit of spot, squared | same |
| vega | dV/dsigma | per 1.00 of vol (100 vol points) | per vol point: vega / 100 |
| theta | dV/dt in calendar time = -dV/dT | per year | per day: theta / 365 |
| rho | dV/dr | per 1.00 of rate | same |
| vanna | d2V/dS dsigma | per unit of spot per 1.00 of vol | same |
| volga | d2V/dsigma2 | per (1.00 of vol) squared | same |

Vol (sigma) is a decimal: 0.20 means 20%.

`black76()` returns Greeks with respect to the forward: delta = dV/dF, gamma = d2V/dF2,
vanna = d2V/dF dsigma. Its theta and rho hold F fixed, so rho = -T V and
theta = r V - D F phi(d1) sigma / (2 sqrt T) with r = -ln(D) / T.

## Error handling

- Programming errors (tree `steps` outside [1, 1,000,000], a workspace that is too small,
  `paths == 0`, an odd path count with antithetic sampling) throw `std::invalid_argument`.
- Numerical failures that depend on the input data are returned as a status enum and never
  thrown: `IvStatus` for implied vol, `TreeStatus` for the binomial tree. The value is NaN for
  every non-Ok status except `IvStatus::NotConverged`, which carries the best iterate.
  `TreeStatus::VolTooHigh` covers calls with sigma sqrt(T) above about 30, where node spots
  would leave the e^{+-700} range the tree clamps to. Default-constructed result structs
  (`IvResult`, `TreeResult`, `McResult`) hold NaN values and a failure status, never
  indeterminate values.
- `validate(const OptionSpec&)` is the boundary check for untrusted input. The pricers assert
  it in debug builds.
- The library never prints. Tools print results to stdout, errors to stderr, and exit 0 (ok),
  1 (runtime error) or 2 (usage error).
- Bad market data (Part 3 loader): a missing file, invalid JSON, a missing or empty `result`
  array or a non-positive index price throws `opt::deribit::DataError` (message has the path
  and JSON pointer). Bad rows are skipped and counted by reason, never silently.
## Deribit conventions
Deribit BTC options, as stored by `scripts/fetch_deribit.sh` (fields in
[tests/data/deribit/README.md](../tests/data/deribit/README.md)):
- **Inverse options.** `instrument_type` is `reversed`: premium and settlement are in BTC per
  1 BTC of notional. A call pays max(S_T - K, 0) / S_T BTC at expiry, which is worth
  max(S_T - K, 0) USD. `bid_price`, `ask_price`, `mark_price` are in BTC.
- **USD price.** price_usd = price_btc x F, where F is the expiry's `underlying_price` (the
  future for that expiry, or Deribit's synthetic forward `SYN.BTC-...` when no future is
  listed). Using the index price instead of F would be wrong by the basis (F - S) / S, about
  0.65% for the 27NOV26 expiry in the committed snapshot (F 81,967.84, index 81,437.86).
- **Model.** Deribit's mark IV is a Black-76 vol on the USD price with forward F and zero rate
  (`interest_rate` is 0.0 in every row). So the code solves
  `implied_vol_black(type, price_btc * F, F, K, tau, discount = 1)`.
- **Forward per expiry.** `underlying_price` moves by a few cents between rows of the same
  expiry (rows are stamped within ~10 ms of each other). The loader uses the median per
  expiry and counts rows that differ from it by more than 1e-9 relative as
  `forward_mismatch` (205 of 524 rows in the committed snapshot; the largest spread within an
  expiry is 0.24 USD, 3e-6 relative).
- **Time.** tau = (expiration_timestamp - valuation_ms) / (365 x 86,400,000): ACT/365 on
  milliseconds, 24/7 (crypto trades every day; there is no business-day calendar). Expiries are
  08:00 UTC.
- **Valuation time** = the maximum `creation_timestamp` over book rows. The loader also keeps
  the minimum; `vol_surface` warns when they differ by more than 1 s (10 ms in the committed
  snapshot).
- **Units.** `mark_iv` is in percent; the loader converts it to a decimal. Strikes and
  forwards are USD.
### Mark convention check
Re-measured by `vol_surface` on the committed snapshot (command and full output in
[part3-verification.md](part3-verification.md)). iv_mark is the Black-76 vol recomputed from
`mark_price x F`; iv_mid is from the bid/ask mid. Vol points (1 vol point = 0.01 vol):
| Comparison | Quotes | Median | p95 | Max |
|---|---|---|---|---|
| OTM \|iv_mark - mark_iv\| | 256 | 0.0027 | 0.0063 | 0.0927 |
| ITM \|iv_mark - mark_iv\| | 249 | 0.0134 | 0.4692 | 12.4881 |
| OTM iv_mid - mark_iv (signed) | 203 | +0.0186 | +0.6702 (p05 -0.3428) | 4.1208 |
| ITM iv_mid - mark_iv (signed) | 221 | +0.1906 | +10.2596 (p05 -3.3384) | 47.6985 |
OTM marks agree with Deribit's `mark_iv` to a median of 0.0027 vol points; `mark_iv` is
printed with 2 decimals in percent, so 0.005 vol points is its own rounding. That is the
evidence that the conventions above are the ones Deribit uses. The ITM outliers come from the
1e-8 BTC rounding of `mark_price`: an ITM option's price is mostly intrinsic value, so a tiny
price error is a large vol error (low vega relative to price). 19 marks are below intrinsic
value after that rounding (`BelowIntrinsic`) and have no IV. Mids differ from marks by
design: the mark is Deribit's smoothed fair value, the mid is the raw book, and ITM books are
wide. This is why the SVI fit uses OTM quotes only.
### SVI domain and wing slopes
The fitter uses the Zeliade domain 0 <= c <= 4s, |d| <= c, |d| <= 4s - c, 0 <= a <= max w
(c = b s, d = rho b s). The bound c <= 4s is the wing slope b (1 + |rho|) <= 4
(Rogers-Tehranchi). Lee's moment formula gives the stricter asymptotic bound 2 on b (1 + rho)
and b (1 - rho); the fitter does not enforce it, the arbitrage report prints both slopes and
flags values above 2.
