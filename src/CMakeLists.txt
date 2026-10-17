# Header-only SIMD lane layer (NEON / AVX2 / scalar, plus the test-only
# Emulated<W> backend).
add_library(opt_simd INTERFACE)
add_library(opt::simd ALIAS opt_simd)
target_include_directories(opt_simd INTERFACE ${PROJECT_SOURCE_DIR}/include)
target_compile_features(opt_simd INTERFACE cxx_std_20)

# Shared building blocks: version info (compiled) plus header-only RNG,
# normal distribution, fast math, compensated sum and latency histogram.
add_library(opt_core core/version.cpp)
add_library(opt::core ALIAS opt_core)
target_include_directories(opt_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(opt_core PRIVATE OPTENG_VERSION_STRING="${PROJECT_VERSION}")
target_compile_features(opt_core PUBLIC cxx_std_20)
target_link_libraries(opt_core PUBLIC opt::simd)
opteng_configure_target(opt_core)

# Pricing core: BSM + Greeks, implied vol, CRR tree, Monte Carlo.
# No third-party dependency.
add_library(opt_pricing pricing/black_scholes.cpp pricing/implied_vol.cpp pricing/binomial.cpp
                        pricing/monte_carlo.cpp)
add_library(opt::pricing ALIAS opt_pricing)
target_link_libraries(opt_pricing PUBLIC opt::core)
opteng_configure_target(opt_pricing)

# Batch engines (reference, fast scalar, SIMD) and the threaded pricer.
find_package(Threads REQUIRED)
add_library(opt_batch batch/batch.cpp batch/thread_pool.cpp)
add_library(opt::batch ALIAS opt_batch)
target_link_libraries(opt_batch PUBLIC opt::pricing opt::simd Threads::Threads)
opteng_configure_target(opt_batch)
# Volatility layer: SVI, robust SVI fit, static-arbitrage checks, surface.
# No third-party dependency.
add_library(opt_vol vol/svi_fit.cpp vol/arbitrage.cpp vol/surface.cpp)
add_library(opt::vol ALIAS opt_vol)
target_link_libraries(opt_vol PUBLIC opt::pricing opt::core)
opteng_configure_target(opt_vol)
# Deribit snapshot loader plus the market implied vols of a snapshot
# (vol/market_iv.cpp reads a deribit::Snapshot, so it is built here).
# nlohmann/json is a private dependency: no public header includes it.
add_library(opt_deribit deribit/snapshot.cpp vol/market_iv.cpp)
add_library(opt::deribit ALIAS opt_deribit)
target_link_libraries(opt_deribit PUBLIC opt::vol PRIVATE nlohmann_json::nlohmann_json)
opteng_configure_target(opt_deribit)
