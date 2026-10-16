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
