# lcl-json

JSON encoding and decoding for LCL using cJSON.

## Build

```bash
# From the lcl root directory
cmake -DLCL_BUILD_JSON=ON -B build
cmake --build build

# Or standalone
cd packages/lcl-json
cmake -B build
cmake --build build
```

## Usage

```tcl
# Encode LCL values to JSON
let data #{
    name "Alice"
    age 30
    active [json::true]
}
let json_str [json::encode $data]
puts $json_str  ;# {"name":"Alice","age":30,"active":true}

# Decode JSON to LCL values
let parsed [json::decode {{"items": [1, 2, 3], "valid": true}}]
let items [get $parsed items]
```

## API Reference

### Encoding/Decoding

| Function | Description |
|----------|-------------|
| `json::encode $value` | Convert LCL value to JSON string |
| `json::decode $string` | Parse JSON string to LCL value |

### Type Mapping

| JSON Type | LCL Type |
|-----------|----------|
| string | string |
| number (integer) | int |
| number (decimal) | float |
| array | list |
| object | dict |
| true/false | json boolean (opaque) |
| null | json null (opaque) |

### Special Values

| Function | Description |
|----------|-------------|
| `json::true` | Create JSON true value |
| `json::false` | Create JSON false value |
| `json::null` | Create JSON null value |

### Type Checking

| Function | Description |
|----------|-------------|
| `json::bool? $value` | Check if value is JSON boolean (returns 0/1) |
| `json::null? $value` | Check if value is JSON null (returns 0/1) |

## Notes

- JSON booleans are distinct from LCL integers. Use `json::true` and `json::false` when you need actual JSON boolean values in output.
- JSON null is represented as an opaque value. Use `json::null?` to check for null values.
- Numbers that fit in a long integer are decoded as LCL integers; others become floats.
