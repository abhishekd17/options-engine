# Shared building blocks: version info (compiled) plus header-only RNG,
# normal distribution, compensated sum and latency histogram.
add_library(opt_core core/version.cpp)
add_library(opt::core ALIAS opt_core)
target_include_directories(opt_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(opt_core PRIVATE OPTENG_VERSION_STRING="${PROJECT_VERSION}")
target_compile_features(opt_core PUBLIC cxx_std_20)
opteng_configure_target(opt_core)
