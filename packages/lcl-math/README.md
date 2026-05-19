# lcl-math

Floating-point math functions for LCL (a thin wrapper around the C `<math.h>` library).

## Requirements

- LCL core engine
- C standard library `<math.h>` and libm
- **Portability:** Universal. Builds anywhere a hosted C implementation with libm is available — Linux, macOS, BSD, Windows, and embedded targets.

## Build

```bash
cmake -S . -B build -DLCL_BUILD_MATH=ON
cmake --build build
```

## Usage

```tcl
puts $math::pi                 ;# 3.141592653589793
puts [math::sqrt 2]            ;# 1.4142135623730951
puts [math::sin [/ $math::pi 2]]  ;# 1.0
puts [math::pow 2 10]          ;# 1024.0
puts [math::log10 1000]        ;# 3.0
```

## API Reference

All functions live in the `math::` namespace and operate on numeric (int or float) values, returning floats unless noted.

### Constants

| Function | Description |
|----------|-------------|
| `math::pi` | The constant pi |
| `math::e` | The constant e |

### Trigonometric

| Function | Description |
|----------|-------------|
| `math::sin $x` | Sine (x in radians) |
| `math::cos $x` | Cosine |
| `math::tan $x` | Tangent |
| `math::asin $x` | Arc sine |
| `math::acos $x` | Arc cosine |
| `math::atan $x` | Arc tangent |
| `math::atan2 $y $x` | Two-argument arc tangent |

### Hyperbolic

| Function | Description |
|----------|-------------|
| `math::sinh $x` | Hyperbolic sine |
| `math::cosh $x` | Hyperbolic cosine |
| `math::tanh $x` | Hyperbolic tangent |
| `math::asinh $x` | Inverse hyperbolic sine |
| `math::acosh $x` | Inverse hyperbolic cosine |
| `math::atanh $x` | Inverse hyperbolic tangent |

### Exponential and Logarithmic

| Function | Description |
|----------|-------------|
| `math::exp $x` | e raised to x |
| `math::exp2 $x` | 2 raised to x |
| `math::log $x` | Natural logarithm |
| `math::log2 $x` | Base-2 logarithm |
| `math::log10 $x` | Base-10 logarithm |

### Power and Root

| Function | Description |
|----------|-------------|
| `math::pow $x $y` | x raised to y |
| `math::sqrt $x` | Square root |
| `math::cbrt $x` | Cube root |
| `math::hypot $x $y` | sqrt(x*x + y*y) without overflow |

### Rounding and Remainder

| Function | Description |
|----------|-------------|
| `math::ceil $x` | Round up to integer |
| `math::floor $x` | Round down to integer |
| `math::trunc $x` | Truncate toward zero |
| `math::round $x` | Round to nearest (ties away from zero) |
| `math::fmod $x $y` | Floating-point remainder |

### Other

| Function | Description |
|----------|-------------|
| `math::abs $x` | Absolute value |
| `math::erf $x` | Error function |
| `math::erfc $x` | Complementary error function |
| `math::tgamma $x` | True gamma function |
| `math::lgamma $x` | Log gamma function |
| `math::isnan $x` | Returns 1 if x is NaN, else 0 |
| `math::isinf $x` | Returns 1 if x is infinite, else 0 |
| `math::min $x $y` | Minimum of two values |
| `math::max $x $y` | Maximum of two values |

## Tests

The package currently ships without a dedicated test suite. To smoke-test interactively:

```bash
cmake -S . -B build \
  -DLCL_BUILD_MATH=ON \
  -DLCL_BUILD_IO=ON \
  -DLCL_BUILD_CLI=ON
cmake --build build
./build/lcl -e 'puts [math::sqrt 2]'
```

When tests are added, they will follow the standard package pattern (file in `packages/lcl-math/test/`, registered via `lcl_add_package_test(NAME lcl-math ...)`, and run with `ctest --test-dir build -R lcl-math` after enabling `LCL_BUILD_TESTS`, `LCL_BUILD_IO`, and `LCL_BUILD_TEST_LIB`).
