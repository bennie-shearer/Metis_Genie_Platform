# Metis Genie Platform Server v5.0.1 - Compilation Fixes (originally v2.21.0)

## Summary
This update addresses multiple compilation errors and API incompatibilities across the server codebase.

## Fixes Applied

### 1. logging.hpp
- Fixed LOG macros to support both 1 and 2 arguments using variadic macro technique
- Added LOG_GET_MACRO helper for argument count detection
- Created LOG_INFO_1/LOG_INFO_2 variants for each log level
- Macros now accept: `LOG_INFO(msg)` or `LOG_INFO(component, msg)`

### 2. http_client.hpp
- Added default constructor: `HttpClient()`
- Added string constructor: `HttpClient(const std::string& base_url)`
- Added `set_timeout(int)` method
- Added `set_header(key, value)` method
- Added `patch(path, body)` method
- Added static `url_encode()` method
- Added `JsonValue::empty()` method
- Fixed `Config` struct with explicit default constructor

### 3. date_utils.hpp
- Added `parse_date(const std::string&)` function for YYYY-MM-DD format parsing

### 4. market_data.hpp
- Unified PriceBar struct with all required fields:
  - Both TimePoint timestamp and string date fields
  - Both adj_close and adjusted_close (synced)
  - Added dividend_amount, split_coefficient
  - Added is_valid(), sync_adjusted() methods
- Added Quote struct (moved from alpha_vantage.hpp for shared use)
  - Includes bid, ask, bid_size, ask_size fields
  - Added mid() and spread() methods
- Added Dividend struct with symbol, date, amount, ex_date, payment_date, record_date
- Added Split struct with symbol, date, ratio, factor

### 5. alpha_vantage.hpp
- Updated to use unified PriceBar from market_data.hpp
- Removed duplicate Quote struct (now uses shared definition from market_data.hpp)
- Added include for market_data.hpp

### 6. price_cache.hpp
- Added flexible constructors to PriceCache:
  - `PriceCache(int staleness_seconds, int max_age_seconds)`
  - `PriceCache(int ttl, int stale_threshold, int grace_period)`
- Added constructor to PriceCacheConfig from integer seconds
- Added `get_stats()` methods (const and non-const)

### 7. price_validator.hpp
- Added constructors to ValidationIssue struct:
  - Default constructor
  - Constructor with severity, type, symbol, date, message
  - Constructor with all fields including value and expected

### 8. live_valuation.hpp
- Fixed `value_portfolio()` to use correct Portfolio interface:
  - Uses `port.cash_balance().amount` instead of `port.cash()`
  - Uses `port.positions()` map iteration with structured bindings
  - Accesses Position via methods: `pos.security_id()`, `pos.quantity()`, `pos.cost_basis().amount`
- Added `add_alert()` methods:
  - Overload with symbol, condition, threshold, message parameters
  - Overload accepting PriceAlert struct directly

### 9. portfolio.hpp
- Added `positions()` accessor method returning `const std::map<SecurityId, Position>&`

### 10. websocket_client.hpp
- Added `unsubscribe_trades()`, `unsubscribe_quotes()`, `unsubscribe_bars()` methods
- Added `close()` method (alias for disconnect)
- Added `on_open()` and `on_close()` callback registration methods
- Added `on_open_` and `on_close_` member variables

## Version Updates
All modified headers updated from version 2.19.0 to 2.21.0.

## Compilation Status
All 10 fixed headers compile successfully with g++ -std=c++20:
- logging.hpp: OK
- date_utils.hpp: OK
- http_client.hpp: OK
- websocket_client.hpp: OK
- market_data.hpp: OK
- price_cache.hpp: OK
- alpha_vantage.hpp: OK
- portfolio.hpp: OK
- position.hpp: OK
- live_valuation.hpp: OK

## Files Modified
1. include/genie/core/logging.hpp
2. include/genie/core/http_client.hpp
3. include/genie/core/date_utils.hpp
4. include/genie/core/websocket_client.hpp
5. include/genie/market/market_data.hpp
6. include/genie/market/alpha_vantage.hpp
7. include/genie/market/price_cache.hpp
8. include/genie/market/price_validator.hpp
9. include/genie/analytics/live_valuation.hpp
10. include/genie/portfolio/portfolio.hpp
