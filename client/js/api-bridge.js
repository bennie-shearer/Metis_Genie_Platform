// Metis Genie Platform v5.5.11
/**
 * api-bridge.js - Full Server Endpoint Coverage
 * Metis Genie Platform v5.5.11
 *
 * Provides client-side functions for ALL server REST API endpoints,
 * ensuring complete feature parity between server and client.
 *
 * Endpoints covered by this bridge:
 *   /api/v1/esg
 *   /api/v1/compliance/regulatory
 *   /api/v1/risk/factors
 *   /api/v1/risk/liquidity
 *   /api/v1/ml/alpha
 *   /api/v1/fix/status
 *   /api/v1/reporting/schedules
 *   /api/v1/export/formats
 *   /api/v1/export/csv/templates
 *   /api/v1/export
 *   /api/v1/ops/circuit-breakers
 *   /api/v1/ops/rate-limits
 *   /api/v1/ops/shutdown
 *   /api/v1/ops/telemetry
 *   /api/v1/config/validate
 *   /api/v1/whatif/scenarios
 */
'use strict';

const ApiBridge = {

    baseUrl() {
        return (window.GenieConfig && window.GenieConfig.baseUrl)
            ? window.GenieConfig.baseUrl : 'http://localhost:8080';
    },

    headers() {
        const token = localStorage.getItem('genie_token') || 'demo-token';
        return { 'Authorization': 'Bearer ' + token, 'Content-Type': 'application/json' };
    },

    async get(path) {
        const resp = await fetch(this.baseUrl() + path, { headers: this.headers() });
        return resp.json();
    },

    async post(path, body) {
        const resp = await fetch(this.baseUrl() + path, {
            method: 'POST', headers: this.headers(), body: JSON.stringify(body)
        });
        return resp.json();
    },

    // ========== ESG Scoring (/api/v1/esg) ==========
    async loadEsg(containerId) {
        const data = await this.get('/api/v1/esg');
        const el = document.getElementById(containerId);
        if (!el || !data.esg_scores) return data;
        el.innerHTML = `
            <div class="metrics-row" style="margin-bottom:1rem;">
                <div class="metric"><span class="label">Portfolio ESG</span><span class="value">${data.portfolio_esg.weighted_score}</span></div>
                <div class="metric"><span class="label">Rating</span><span class="value">${data.portfolio_esg.rating}</span></div>
                <div class="metric"><span class="label">Carbon Intensity</span><span class="value">${data.portfolio_esg.carbon_intensity}</span></div>
                <div class="metric"><span class="label">Methodology</span><span class="value">${data.methodology}</span></div>
            </div>
            <table class="data-table"><thead><tr><th>Symbol</th><th>Environment</th><th>Social</th><th>Governance</th><th>Total</th><th>Rating</th><th>Trend</th></tr></thead>
            <tbody>${data.esg_scores.map(s => `<tr><td><strong>${s.symbol}</strong></td><td>${s.environment}</td><td>${s.social}</td><td>${s.governance}</td><td>${s.total}</td><td>${s.rating}</td><td>${s.trend}</td></tr>`).join('')}</tbody></table>`;
        return data;
    },

    // ========== Regulatory Reporting (/api/v1/compliance/regulatory) ==========
    async loadRegulatory(containerId) {
        const data = await this.get('/api/v1/compliance/regulatory');
        const el = document.getElementById(containerId);
        if (!el || !data.deadlines) return data;
        el.innerHTML = `
            <h3>Regulatory Calendar</h3>
            <table class="data-table"><thead><tr><th>Report</th><th>Period</th><th>Due Date</th><th>Status</th><th>Days Remaining</th></tr></thead>
            <tbody>${data.deadlines.map(d => `<tr><td>${d.report}</td><td>${d.period}</td><td>${d.due_date}</td>
                <td><span class="badge badge-${d.status === 'submitted' || d.status === 'active' ? 'green' : d.status === 'draft' ? 'yellow' : 'blue'}">${d.status}</span></td>
                <td>${d.days_remaining}</td></tr>`).join('')}</tbody></table>
            <h3>Filing History</h3>
            <table class="data-table"><thead><tr><th>Report ID</th><th>Type</th><th>Period</th><th>Status</th><th>Records</th></tr></thead>
            <tbody>${(data.report_history || []).map(r => `<tr><td>${r.report_id}</td><td>${r.type}</td><td>${r.period}</td><td><span class="badge badge-green">${r.status}</span></td><td>${r.records}</td></tr>`).join('')}</tbody></table>
            <div class="metrics-row" style="margin-top:1rem;"><div class="metric"><span class="label">Firm</span><span class="value">${data.firm.name}</span></div><div class="metric"><span class="label">CIK</span><span class="value">${data.firm.cik}</span></div><div class="metric"><span class="label">LEI</span><span class="value">${data.firm.lei}</span></div></div>`;
        return data;
    },

    // ========== Risk Factors (/api/v1/risk/factors) ==========
    async loadRiskFactors(containerId) {
        const data = await this.get('/api/v1/risk/factors');
        const el = document.getElementById(containerId);
        if (!el || !data.factor_exposures) return data;
        const fe = data.factor_exposures;
        const fr = data.factor_returns;
        el.innerHTML = `
            <div class="metrics-row" style="margin-bottom:1rem;">
                <div class="metric"><span class="label">R-Squared</span><span class="value">${data.r_squared}</span></div>
                <div class="metric"><span class="label">Tracking Error</span><span class="value">${data.tracking_error}%</span></div>
                <div class="metric"><span class="label">Style</span><span class="value">${data.style_drift.current_quarter}</span></div>
            </div>
            <table class="data-table"><thead><tr><th>Factor</th><th>Exposure</th><th>Return (%)</th></tr></thead>
            <tbody>
                <tr><td>Market Beta</td><td>${fe.market_beta}</td><td>${fr.market}%</td></tr>
                <tr><td>Size (SMB)</td><td>${fe.size_smb}</td><td>${fr.size}%</td></tr>
                <tr><td>Value (HML)</td><td>${fe.value_hml}</td><td>${fr.value}%</td></tr>
                <tr><td>Momentum (UMD)</td><td>${fe.momentum_umd}</td><td>${fr.momentum}%</td></tr>
                <tr><td>Quality</td><td>${fe.quality}</td><td>${fr.quality}%</td></tr>
                <tr><td>Volatility</td><td>${fe.volatility}</td><td>${fr.volatility}%</td></tr>
            </tbody></table>`;
        return data;
    },

    // ========== Liquidity Risk (/api/v1/risk/liquidity) ==========
    async loadLiquidityRisk(containerId) {
        const data = await this.get('/api/v1/risk/liquidity');
        const el = document.getElementById(containerId);
        if (!el || !data.portfolio_liquidity) return data;
        const pl = data.portfolio_liquidity;
        el.innerHTML = `
            <div class="metrics-row" style="margin-bottom:1rem;">
                <div class="metric"><span class="label">Liquidity Score</span><span class="value">${pl.weighted_score}/10</span></div>
                <div class="metric"><span class="label">LVaR 95%</span><span class="value">$${pl.lvar_95.toLocaleString()}</span></div>
                <div class="metric"><span class="label">LVaR 99%</span><span class="value">$${pl.lvar_99.toLocaleString()}</span></div>
                <div class="metric"><span class="label">Cost %</span><span class="value">${pl.liquidation_cost_pct}%</span></div>
                <div class="metric"><span class="label">Max Days</span><span class="value">${pl.max_days_to_liquidate}</span></div>
                <div class="metric"><span class="label">HHI</span><span class="value">${pl.concentration_hhi}</span></div>
            </div>
            <h3>Position Liquidity</h3>
            <table class="data-table"><thead><tr><th>Symbol</th><th>Score</th><th>Tier</th><th>Days</th><th>Spread (bps)</th><th>Cost %</th></tr></thead>
            <tbody>${(data.position_liquidity || []).map(p => `<tr><td><strong>${p.symbol}</strong></td><td>${p.liquidity_score}</td><td><span class="badge badge-${p.tier === 'highly_liquid' ? 'green' : p.tier === 'liquid' ? 'blue' : 'yellow'}">${p.tier}</span></td><td>${p.days_to_liquidate}</td><td>${p.spread_bps}</td><td>${p.cost_pct}%</td></tr>`).join('')}</tbody></table>
            <h3>Stress Scenarios</h3>
            <table class="data-table"><thead><tr><th>Scenario</th><th>Normal Cost</th><th>Stressed Cost</th><th>Multiplier</th></tr></thead>
            <tbody>${(data.stress_scenarios || []).map(s => `<tr><td>${s.scenario}</td><td>$${s.normal_cost.toLocaleString()}</td><td>$${s.stressed_cost.toLocaleString()}</td><td>${s.multiplier}x</td></tr>`).join('')}</tbody></table>`;
        return data;
    },

    // ========== ML Alpha Signals (/api/v1/ml/alpha) ==========
    async loadMlAlpha(containerId) {
        const data = await this.get('/api/v1/ml/alpha');
        const el = document.getElementById(containerId);
        if (!el || !data.signals) return data;
        const mi = data.model_info;
        el.innerHTML = `
            <div class="metrics-row" style="margin-bottom:1rem;">
                <div class="metric"><span class="label">Model</span><span class="value">${mi.name}</span></div>
                <div class="metric"><span class="label">Type</span><span class="value">${mi.type}</span></div>
                <div class="metric"><span class="label">Features</span><span class="value">${mi.features}</span></div>
                <div class="metric"><span class="label">Backtest Sharpe</span><span class="value">${mi.backtest_sharpe}</span></div>
            </div>
            <table class="data-table"><thead><tr><th>Symbol</th><th>Signal</th><th>Confidence</th><th>Direction</th><th>Features</th></tr></thead>
            <tbody>${data.signals.map(s => `<tr><td><strong>${s.symbol}</strong></td><td class="${s.signal >= 0 ? 'positive' : 'negative'}">${s.signal.toFixed(2)}</td><td>${(s.confidence * 100).toFixed(0)}%</td><td><span class="badge badge-${s.direction === 'Long' ? 'green' : 'red'}">${s.direction}</span></td><td>${s.features.join(', ')}</td></tr>`).join('')}</tbody></table>`;
        return data;
    },

    // ========== FIX Protocol Status (/api/v1/fix/status) ==========
    async loadFixStatus(containerId) {
        const data = await this.get('/api/v1/fix/status');
        const el = document.getElementById(containerId);
        if (!el || !data.fix_sessions) return data;
        el.innerHTML = `
            <h3>FIX Sessions</h3>
            <table class="data-table"><thead><tr><th>Session ID</th><th>Protocol</th><th>Status</th><th>HB Interval</th><th>Sent</th><th>Received</th></tr></thead>
            <tbody>${data.fix_sessions.map(s => `<tr><td>${s.session_id}</td><td>${s.protocol}</td><td><span class="badge badge-blue">${s.status}</span></td><td>${s.heartbeat_interval}s</td><td>${s.messages_sent}</td><td>${s.messages_received}</td></tr>`).join('')}</tbody></table>
            <h3>Certification</h3>
            <div class="metrics-row">
                <div class="metric"><span class="label">FIX 4.2</span><span class="value badge badge-green">${data.certification.fix42_passed ? 'PASSED' : 'FAILED'}</span></div>
                <div class="metric"><span class="label">FIX 4.4</span><span class="value badge badge-green">${data.certification.fix44_passed ? 'PASSED' : 'FAILED'}</span></div>
                <div class="metric"><span class="label">Tests</span><span class="value">${data.certification.passed}/${data.certification.total_tests}</span></div>
            </div>
            <h3>Supported Messages</h3>
            <div style="display:flex;flex-wrap:wrap;gap:0.5rem;">${data.supported_messages.map(m => `<span class="badge badge-blue">${m}</span>`).join('')}</div>`;
        return data;
    },

    // ========== Report Schedules (/api/v1/reporting/schedules) ==========
    async loadReportSchedules(containerId) {
        const data = await this.get('/api/v1/reporting/schedules');
        const el = document.getElementById(containerId);
        if (!el || !data.schedules) return data;
        el.innerHTML = `<table class="data-table"><thead><tr><th>Report</th><th>Frequency</th><th>Time</th><th>Format</th><th>Recipients</th><th>Last Run</th><th>Status</th></tr></thead>
            <tbody>${data.schedules.map(s => `<tr><td>${s.report}</td><td>${s.frequency}</td><td>${s.time}</td><td>${s.format}</td><td>${s.recipients.join(', ')}</td><td>${new Date(s.last_run).toLocaleDateString()}</td><td><span class="badge badge-green">${s.status}</span></td></tr>`).join('')}</tbody></table>`;
        return data;
    },

    // ========== Export (/api/v1/export/formats, /api/v1/export/csv/templates, /api/v1/export) ==========
    async loadExportFormats(containerId) {
        const data = await this.get('/api/v1/export/formats');
        const el = document.getElementById(containerId);
        if (!el) return data;
        el.innerHTML = `
            <h3>Available Formats</h3>
            <div style="display:flex;flex-wrap:wrap;gap:1rem;margin-bottom:1rem;">${(data.formats || []).map(f => `<div class="metric-card"><div class="metric-label">${f.name}</div><div class="metric-value">${f.id.toUpperCase()}</div><div style="font-size:0.8em;color:#888;">${f.description}</div></div>`).join('')}</div>
            <h3>Exportable Datasets</h3>
            <div style="display:flex;flex-wrap:wrap;gap:0.5rem;">${(data.exportable_datasets || []).map(d => `<span class="badge badge-blue">${d}</span>`).join('')}</div>`;
        return data;
    },

    async loadCsvTemplates(containerId) {
        const data = await this.get('/api/v1/export/csv/templates');
        const el = document.getElementById(containerId);
        if (!el || !data.templates) return data;
        el.innerHTML = `<table class="data-table"><thead><tr><th>Template</th><th>Title</th><th>Columns</th></tr></thead>
            <tbody>${data.templates.map(t => `<tr><td><strong>${t.name}</strong></td><td>${t.title}</td><td>${t.columns}</td></tr>`).join('')}</tbody></table>`;
        return data;
    },

    async exportData(format, dataset) {
        return this.post('/api/v1/export', { format: format || 'csv', dataset: dataset || 'positions' });
    },

    // ========== Circuit Breakers (/api/v1/ops/circuit-breakers) ==========
    async loadCircuitBreakers(containerId) {
        const data = await this.get('/api/v1/ops/circuit-breakers');
        const el = document.getElementById(containerId);
        if (!el || !data.circuit_breakers) return data;
        el.innerHTML = `<table class="data-table"><thead><tr><th>Service</th><th>State</th><th>Failure Rate</th><th>Failures</th><th>Last Trip</th><th>Recovery</th></tr></thead>
            <tbody>${data.circuit_breakers.map(cb => `<tr><td>${cb.service}</td><td><span class="badge badge-${cb.state === 'CLOSED' ? 'green' : cb.state === 'OPEN' ? 'red' : 'yellow'}">${cb.state}</span></td><td>${cb.failure_rate_pct}%</td><td>${cb.consecutive_failures}</td><td>${cb.last_tripped || 'Never'}</td><td>${cb.recovery_timeout_sec}s</td></tr>`).join('')}</tbody></table>`;
        return data;
    },

    // ========== Rate Limits (/api/v1/ops/rate-limits) ==========
    async loadRateLimits(containerId) {
        const data = await this.get('/api/v1/ops/rate-limits');
        const el = document.getElementById(containerId);
        if (!el || !data.rate_limits) return data;
        el.innerHTML = `<table class="data-table"><thead><tr><th>Endpoint</th><th>Limit</th><th>Window</th><th>Current</th><th>Utilization</th></tr></thead>
            <tbody>${data.rate_limits.map(rl => `<tr><td>${rl.endpoint}</td><td>${rl.limit} req</td><td>${rl.window_sec}s</td><td>${rl.current_usage}</td><td><div class="bar-track" style="width:100px;display:inline-block;"><div class="bar-fill" style="width:${rl.utilization_pct}%;background:${rl.utilization_pct > 80 ? '#e74c3c' : rl.utilization_pct > 50 ? '#f39c12' : '#27ae60'};"></div></div> ${rl.utilization_pct}%</td></tr>`).join('')}</tbody></table>`;
        return data;
    },

    // ========== Shutdown Status (/api/v1/ops/shutdown) ==========
    async loadShutdownStatus(containerId) {
        const data = await this.get('/api/v1/ops/shutdown');
        const el = document.getElementById(containerId);
        if (!el) return data;
        el.innerHTML = `
            <div class="metrics-row">
                <div class="metric"><span class="label">State</span><span class="value badge badge-green">${data.state}</span></div>
                <div class="metric"><span class="label">Active Requests</span><span class="value">${data.active_requests}</span></div>
                <div class="metric"><span class="label">Hooks</span><span class="value">${data.hooks_registered}</span></div>
                <div class="metric"><span class="label">Drain Timeout</span><span class="value">${data.drain_timeout_seconds}s</span></div>
            </div>
            <h3>Registered Hooks</h3>
            <div style="display:flex;flex-wrap:wrap;gap:0.5rem;">${(data.hooks || []).map(h => `<span class="badge badge-blue">${h}</span>`).join('')}</div>`;
        return data;
    },

    // ========== Telemetry (/api/v1/ops/telemetry) ==========
    async loadTelemetry(containerId) {
        const data = await this.get('/api/v1/ops/telemetry');
        const el = document.getElementById(containerId);
        if (!el) return data;
        el.innerHTML = `
            <h3>Counters</h3>
            <table class="data-table"><thead><tr><th>Name</th><th>Value</th><th>Labels</th></tr></thead>
            <tbody>${(data.counters || []).map(c => `<tr><td>${c.name}</td><td>${c.value.toLocaleString()}</td><td>${JSON.stringify(c.labels || {})}</td></tr>`).join('')}</tbody></table>
            <h3>Gauges</h3>
            <div class="metrics-row">${(data.gauges || []).map(g => `<div class="metric"><span class="label">${g.name}</span><span class="value">${g.value}</span></div>`).join('')}</div>
            <h3>Histograms</h3>
            <table class="data-table"><thead><tr><th>Name</th><th>Count</th><th>Avg</th><th>P50</th><th>P95</th><th>P99</th></tr></thead>
            <tbody>${(data.histograms || []).map(h => `<tr><td>${h.name}</td><td>${h.count.toLocaleString()}</td><td>${h.avg}ms</td><td>${h.p50}ms</td><td>${h.p95}ms</td><td>${h.p99}ms</td></tr>`).join('')}</tbody></table>`;
        return data;
    },

    // ========== Config Validation (/api/v1/config/validate) ==========
    async loadConfigValidation(containerId) {
        const data = await this.get('/api/v1/config/validate');
        const el = document.getElementById(containerId);
        if (!el) return data;
        el.innerHTML = `
            <div class="metrics-row" style="margin-bottom:1rem;">
                <div class="metric"><span class="label">Status</span><span class="value badge badge-${data.valid ? 'green' : 'red'}">${data.valid ? 'VALID' : 'INVALID'}</span></div>
                <div class="metric"><span class="label">Errors</span><span class="value">${data.errors}</span></div>
                <div class="metric"><span class="label">Warnings</span><span class="value">${data.warnings}</span></div>
            </div>
            ${data.issues && data.issues.length > 0 ? `<table class="data-table"><thead><tr><th>Severity</th><th>Field</th><th>Message</th><th>Suggestion</th></tr></thead>
            <tbody>${data.issues.map(i => `<tr><td><span class="badge badge-${i.severity === 'error' ? 'red' : i.severity === 'warning' ? 'yellow' : 'blue'}">${i.severity}</span></td><td>${i.field}</td><td>${i.message}</td><td>${i.suggestion || '-'}</td></tr>`).join('')}</tbody></table>` : '<p>No issues found.</p>'}
            <p style="color:#888;margin-top:0.5rem;">${data.summary}</p>`;
        return data;
    },

    // ========== What-If Scenarios (/api/v1/whatif/scenarios) ==========
    async loadWhatIfScenarios(containerId) {
        const data = await this.get('/api/v1/whatif/scenarios');
        const el = document.getElementById(containerId);
        if (!el || !data.scenarios) return data;
        el.innerHTML = `<table class="data-table"><thead><tr><th>ID</th><th>Scenario</th><th>Type</th><th>P&L Impact</th><th>VaR Change</th><th>Worst Sector</th><th>Status</th></tr></thead>
            <tbody>${data.scenarios.map(s => `<tr><td>${s.id}</td><td>${s.name}</td><td>${s.type}</td><td class="${s.impact.portfolio_pnl >= 0 ? 'positive' : 'negative'}">$${s.impact.portfolio_pnl.toLocaleString()}</td><td>${s.impact.var_change > 0 ? '+' : ''}${s.impact.var_change}%</td><td>${s.impact.worst_sector}</td><td><span class="badge badge-green">${s.status}</span></td></tr>`).join('')}</tbody></table>`;
        return data;
    }
};

// Auto-attach to window for global access
window.ApiBridge = ApiBridge;
