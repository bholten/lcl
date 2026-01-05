# lcl-curl

HTTP client bindings for LCL using libcurl.

## Build

```bash
# From the lcl root directory
cmake -DLCL_BUILD_CURL=ON -B build
cmake --build build

# Or standalone
cd packages/lcl-curl
cmake -B build
cmake --build build
```

## Usage

```tcl
curl::init

let c [curl::new]
curl::set_url $c "https://api.example.com/data"
curl::set_verb $c GET
curl::set_header $c "Authorization: Bearer token123"

curl::set_write_callback $c [lambda {chunk} {
    puts "Received: $chunk"
}]

curl::perform $c

let status [curl::get_response_code $c]
puts "Status: $status"
```

## API Reference

### Lifecycle

| Function | Description |
|----------|-------------|
| `curl::init` | Initialize libcurl globally (call once) |
| `curl::new` | Create a new curl handle |
| `curl::reset $handle` | Reset handle to initial state |
| `curl::perform $handle` | Execute the request |

### Request Configuration

| Function | Description |
|----------|-------------|
| `curl::set_url $handle $url` | Set request URL |
| `curl::set_verb $handle $method` | Set HTTP method (GET, POST, etc.) |
| `curl::set_header $handle $header...` | Add HTTP headers |
| `curl::set_body $handle $data` | Set request body |
| `curl::set_user_agent $handle $ua` | Set User-Agent header |

### Timeouts & Connection

| Function | Description |
|----------|-------------|
| `curl::set_timeout_ms $handle $ms` | Total request timeout |
| `curl::set_connection_timeout_ms $handle $ms` | Connection timeout |
| `curl::set_accept_timeout_ms $handle $ms` | Accept timeout |
| `curl::set_low_speed_limit $handle $bytes` | Minimum transfer speed |
| `curl::set_low_speed_time $handle $secs` | Time for low speed check |

### TLS/SSL

| Function | Description |
|----------|-------------|
| `curl::set_ssl_verify_peer $handle $bool` | Verify server certificate |
| `curl::set_ssl_verify_host $handle $bool` | Verify hostname |
| `curl::set_ca_info $handle $path` | CA certificate file |
| `curl::set_ssl_cert $handle $path` | Client certificate |
| `curl::set_ssl_key $handle $path` | Client private key |

### Callbacks

| Function | Description |
|----------|-------------|
| `curl::set_write_callback $handle $proc` | Called with response body chunks |
| `curl::set_header_callback $handle $proc` | Called for each response header |
| `curl::set_sse_callback $handle $proc` | Called for each SSE event (parsed dict) |

**Note:** `set_write_callback` and `set_sse_callback` are mutually exclusive - they both use the same underlying curl write function.

### SSE (Server-Sent Events)

The SSE callback receives a parsed dict with keys:
- `event` - Event type (default: "message")
- `data` - Event data (multiple data lines joined with newline)
- `id` - Event ID (if present)
- `retry` - Retry time in ms (if present)

```tcl
curl::set_sse_callback $c [lambda {event} {
    let type [get $event event]
    let data [get $event data]
    puts "Event: $type, Data: $data"
}]
```

### Response Info

| Function | Description |
|----------|-------------|
| `curl::get_response_code $handle` | HTTP status code |
| `curl::get_content_type $handle` | Content-Type header |
| `curl::get_effective_url $handle` | Final URL after redirects |
| `curl::get_total_time $handle` | Total request time |
| `curl::get_header_size $handle` | Response header size |
| `curl::get_primary_ip $handle` | Server IP address |

### Error Handling

| Function | Description |
|----------|-------------|
| `curl::get_last_error $handle` | CURL error code (0 = OK, 28 = timeout) |
| `curl::is_timeout $handle` | Returns 1 if last error was timeout |
| `curl::error_string $handle` | Human-readable error message |

```tcl
catch {curl::perform $c}

if {[curl::is_timeout $c]} {
    puts "Request timed out (expected for SSE)"
} else {
    puts "Error: [curl::error_string $c]"
}
```

### Proxy Configuration

| Function | Description |
|----------|-------------|
| `curl::set_proxy $handle $url` | Proxy URL |
| `curl::set_proxy_port $handle $port` | Proxy port |
| `curl::set_no_proxy $handle $hosts` | Hosts to bypass proxy |

### Authentication

| Function | Description |
|----------|-------------|
| `curl::set_username $handle $user` | HTTP auth username |
| `curl::set_password $handle $pass` | HTTP auth password |
| `curl::set_xoauth2_bearer $handle $token` | OAuth2 bearer token |

### Redirects

| Function | Description |
|----------|-------------|
| `curl::set_follow_location $handle $bool` | Follow redirects |
| `curl::set_max_redirects $handle $n` | Maximum redirects |
