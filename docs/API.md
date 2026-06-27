# Metis Genie Platform REST API Reference v5.5.11

**145+ REST Endpoints** | Version 5.5.11 | February 2026

## Authentication

All endpoints except `/api/v1/health`, `/api/v1/auth/login`, and
`/api/v1/auth/register` require authentication via Bearer token in the
Authorization header.

## Base URL

    http://localhost:8080/api/v1

## Endpoint Summary

### Authentication & Users (8)
| Method | Path | Description |
|--------|------|-------------|
| POST | /auth/login | Authenticate and receive token |
| POST | /auth/logout | Invalidate token |
| POST | /auth/register | Create new user account |
| GET | /users/me | Current user profile |
| PUT | /users/me | Update user profile |
| POST | /users/me/password | Change password |
| GET | /admin/users | List all users (admin) |
| GET | /admin/users/:username | Get user details (admin) |

### Portfolio & Positions (8)
| Method | Path | Description |
|--------|------|-------------|
| GET | /portfolios | List all portfolios |
| GET | /positions | List all positions |
| GET | /portfolio/constraints | Portfolio constraint check |
| GET | /portfolio/rebalancing | Rebalancing status and targets |
| GET | /reconciliation | Reconciliation status |
| GET | /reconciliation/status | Detailed reconciliation |
| GET | /transactions | Transaction history |
| GET | /ibor/positions | IBOR position book |

### Risk Analytics (8)
| Method | Path | Description |
|--------|------|-------------|
| GET | /risk | Risk dashboard |
| GET | /risk/factors | Factor model exposures |
| GET | /risk/gpu | GPU compute status |
| GET | /risk/hedging | Hedging positions |
| GET | /risk/hedging/strategies | Hedge type catalog |
| GET | /risk/incremental | Incremental VaR |
| GET | /risk/liquidity | Liquidity risk metrics |
| GET | /analytics/risk-attribution | 6-factor risk attribution |

### Analytics (7)
| Method | Path | Description |
|--------|------|-------------|
| GET | /analytics | Analytics dashboard |
| GET | /analytics/correlation | Correlation matrix |
| GET | /analytics/correlation/methods | Available correlation methods |
| GET | /analytics/regimes | Regime detection |
| GET | /analytics/stress-test | Scenario stress results |
| GET | /ml/alpha | ML alpha signals |
| POST | /nlq | Natural language query |

### Trading & Orders (14)
| Method | Path | Description |
|--------|------|-------------|
| GET | /orders | Order list |
| POST | /orders | Submit order |
| GET | /trading/batches | Batch order status |
| POST | /trading/batches | Submit batch order |
| GET | /trading/batches/history | Batch order history |
| GET | /trading/orderbook | Order book simulator |
| GET | /trading/routing | Order routing venues and strategies |
| GET | /trading/position-sizing | Position sizing signals |
| GET | /trading/settlement | Settlement status and items |
| GET | /trading/tca | Transaction cost analysis |
| GET | /trading/blotter | Trade blotter |
| GET | /trading/journal | Trade journal entries |
| GET | /whatif/scenarios | What-if scenario list |
| POST | /whatif/run | Run what-if scenario |

### Backtesting (2)
| Method | Path | Description |
|--------|------|-------------|
| GET | /backtesting/strategies | Available strategies and defaults |
| POST | /backtesting/run | Execute backtest |

### IBOR (2)
| Method | Path | Description |
|--------|------|-------------|
| GET | /ibor/positions | Investment book of record |
| GET | /ibor/reconciliation | IBOR vs ABOR reconciliation |

### Workflows (1)
| Method | Path | Description |
|--------|------|-------------|
| GET | /workflows | Workflow status and approvals |

### Batch Processing (3)
| Method | Path | Description |
|--------|------|-------------|
| GET | /batch-processor/jobs | Active batch jobs |
| POST | /batch-processor/submit | Submit new batch job |
| POST | /batch-processor/cancel | Cancel running job |

### Market Data (8)
| Method | Path | Description |
|--------|------|-------------|
| GET | /market | Market overview |
| GET | /market/overview | Detailed market data |
| GET | /market/quote | Real-time quote |
| GET | /market/calendar | Exchange calendars |
| GET | /market/corporate-actions | Corporate actions list |
| GET | /market/corporate-actions/pending | Pending actions |
| GET | /market/replay | Historical replay status |
| GET | /feeds | Market feed status |

### Compliance (4)
| Method | Path | Description |
|--------|------|-------------|
| GET | /compliance | Compliance dashboard |
| GET | /compliance/regulatory | Regulatory status |
| GET | /compliance/surveillance | Trade surveillance alerts |
| GET | /esg | ESG scores |

### Tax (3)
| Method | Path | Description |
|--------|------|-------------|
| GET | /tax | Tax overview |
| GET | /tax/lots | Tax lot positions |
| GET | /tax/lots/methods | 7 lot selection methods |

### Reporting (6)
| Method | Path | Description |
|--------|------|-------------|
| GET | /reporting | Report list |
| POST | /reporting/generate | Generate report |
| GET | /reporting/schedules | Report schedules |
| GET | /reporting/templates | Report templates |
| GET | /reports/scheduled | Scheduled report status |
| GET | /performance/attribution | Performance attribution |

### Compute & GPU (4)
| Method | Path | Description |
|--------|------|-------------|
| GET | /compute | Compute overview |
| GET | /compute/benchmarks | Performance benchmarks |
| GET | /compute/devices | Device enumeration |
| GET | /compute/gpu | GPU status and backends |

### Containers (3)
| Method | Path | Description |
|--------|------|-------------|
| GET | /containers | Container overview |
| GET | /containers/status | Orchestration status |
| GET | /containers/services | Registered services |

### Configuration (4)
| Method | Path | Description |
|--------|------|-------------|
| GET | /config | Server configuration |
| GET | /config/hot-reload | Hot reload status |
| GET | /config/validate | Configuration validation |
| GET | /feature-flags | Feature flag registry |

### Operations (9)
| Method | Path | Description |
|--------|------|-------------|
| GET | /operations/backups | Backup status |
| GET | /operations/health | Operational health |
| GET | /operations/jobs | Background job list |
| GET | /ops/circuit-breakers | Circuit breaker states |
| GET | /ops/rate-limits | Rate limiter status |
| GET | /ops/telemetry | Telemetry data |
| GET | /ops/shutdown | Shutdown status |
| POST | /ops/shutdown | Graceful shutdown |
| GET | /pipeline/status | Data pipeline status |

### Persistence (3)
| Method | Path | Description |
|--------|------|-------------|
| GET | /persistence/events | Event store |
| GET | /persistence/snapshots | Snapshot store |
| GET | /persistence/stores | Store catalog |

### Export (3)
| Method | Path | Description |
|--------|------|-------------|
| GET | /export/formats | Available export formats |
| GET | /export/csv/templates | CSV templates |
| POST | /export | Export data |

### Infrastructure (12)
| Method | Path | Description |
|--------|------|-------------|
| GET | /health | Health check |
| GET | /health/detailed | Detailed component health |
| GET | /health/live | Liveness probe |
| GET | /health/ready | Readiness probe |
| GET | /health/db | Database health |
| GET | /health/external | External service health |
| GET | /metrics | Prometheus-compatible metrics |
| GET | /status | Server status |
| GET | /version | Version information |
| GET | /api-versions | API version lifecycle |
| GET | /dashboards | Custom dashboard list |
| GET | /tenants | Multi-tenant list |

## Total: 145+ endpoints

*Metis Genie Platform v5.5.11 -- API Reference*
