geni_add_sample(sandbox main.cpp)

# Smoke test: the sample must start, simulate and render without a visible window.
add_test(NAME sample_sandbox COMMAND sandbox --hidden --frames 120)
set_tests_properties(sample_sandbox PROPERTIES LABELS "gpu" TIMEOUT 120)
