// Metis Genie Platform v5.5.11
/**
 * Performance Page - Metis Genie Platform v5.5.11
 * Handles: attribution, benchmarks, regime detection
 */
'use strict';

document.addEventListener('DOMContentLoaded', async () => {
    await loadAttribution();
    await loadBenchmarks();
    await loadRegimeDetection();
});

async function loadAttribution() {
    try {
        const data = await apiGet('/api/v1/performance/attribution');
        const metrics = document.getElementById('perf-metrics');
        if (metrics) {
            metrics.innerHTML = `
                <div class="metric"><span class="label">Portfolio Return</span><span class="value positive">${data.portfolio_return}%</span></div>
                <div class="metric"><span class="label">Benchmark Return</span><span class="value">${data.benchmark_return}%</span></div>
                <div class="metric"><span class="label">Excess Return</span><span class="value ${data.excess_return >= 0 ? 'positive' : 'negative'}">${data.excess_return > 0 ? '+' : ''}${data.excess_return}%</span></div>
                <div class="metric"><span class="label">Period</span><span class="value">${data.period}</span></div>
            `;
        }
        const tbody = document.querySelector('#attribution-table tbody');
        if (tbody && data.attribution) {
            tbody.innerHTML = data.attribution.map(a => `<tr>
                <td><strong>${a.segment}</strong></td>
                <td>${a.portfolio_weight.toFixed(1)}%</td><td>${a.benchmark_weight.toFixed(1)}%</td>
                <td>${a.portfolio_return.toFixed(1)}%</td><td>${a.benchmark_return.toFixed(1)}%</td>
                <td class="${a.allocation >= 0 ? 'positive' : 'negative'}">${a.allocation >= 0 ? '+' : ''}${a.allocation.toFixed(2)}%</td>
                <td class="${a.selection >= 0 ? 'positive' : 'negative'}">${a.selection >= 0 ? '+' : ''}${a.selection.toFixed(2)}%</td>
                <td class="${a.interaction >= 0 ? 'positive' : 'negative'}">${a.interaction >= 0 ? '+' : ''}${a.interaction.toFixed(2)}%</td>
                <td class="${a.total >= 0 ? 'positive' : 'negative'}"><strong>${a.total >= 0 ? '+' : ''}${a.total.toFixed(2)}%</strong></td>
            </tr>`).join('');
        }
        const raMetrics = document.getElementById('risk-adjusted-metrics');
        if (raMetrics && data.risk_adjusted) {
            const ra = data.risk_adjusted;
            raMetrics.innerHTML = `
                <div class="metric"><span class="label">Sharpe Ratio</span><span class="value">${ra.sharpe}</span></div>
                <div class="metric"><span class="label">Sortino Ratio</span><span class="value">${ra.sortino}</span></div>
                <div class="metric"><span class="label">Information Ratio</span><span class="value">${ra.information_ratio}</span></div>
                <div class="metric"><span class="label">Tracking Error</span><span class="value">${ra.tracking_error}%</span></div>
            `;
        }
    } catch (e) { console.error('Attribution error:', e); }
}

async function loadBenchmarks() {
    try {
        const data = await apiGet('/api/v1/benchmark/custom');
        const tbody = document.querySelector('#benchmarks-table tbody');
        if (tbody && data.custom_benchmarks) {
            tbody.innerHTML = data.custom_benchmarks.map(b => `<tr>
                <td><strong>${b.name}</strong></td>
                <td>${b.components.map(c => c.index + ' ' + (c.weight * 100) + '%').join(', ')}</td>
                <td class="positive">${b.ytd_return}%</td>
                <td>${b.rebalance}</td></tr>`).join('');
        }
    } catch (e) { console.error('Benchmarks error:', e); }
}

async function loadRegimeDetection() {
    try {
        const data = await apiGet('/api/v1/analytics/regimes');
        const current = document.getElementById('regime-current');
        if (current && data.current_regime) {
            const r = data.current_regime;
            current.innerHTML = `
                <div class="metric"><span class="label">Volatility</span><span class="value badge badge-${r.volatility === 'low' ? 'green' : r.volatility === 'normal' ? 'blue' : 'red'}">${r.volatility.toUpperCase()}</span></div>
                <div class="metric"><span class="label">Trend</span><span class="value badge badge-${r.trend.includes('bull') ? 'green' : r.trend === 'sideways' ? 'yellow' : 'red'}">${r.trend.toUpperCase()}</span></div>
                <div class="metric"><span class="label">Correlation</span><span class="value badge badge-${r.correlation === 'normal' ? 'blue' : 'yellow'}">${r.correlation.toUpperCase()}</span></div>
                <div class="metric"><span class="label">Realized Vol</span><span class="value">${(r.realized_vol * 100).toFixed(1)}%</span></div>
                <div class="metric"><span class="label">Vol Percentile</span><span class="value">${r.vol_percentile.toFixed(0)}th</span></div>
            `;
        }
        const tbody = document.querySelector('#regime-history-table tbody');
        if (tbody && data.regime_history) {
            tbody.innerHTML = data.regime_history.map(h => `<tr>
                <td>${h.date}</td>
                <td><span class="badge badge-${h.volatility === 'low' ? 'green' : h.volatility === 'normal' ? 'blue' : 'red'}">${h.volatility}</span></td>
                <td><span class="badge badge-${h.trend.includes('bull') ? 'green' : h.trend === 'sideways' ? 'yellow' : 'red'}">${h.trend}</span></td>
                <td>${h.momentum.toFixed(2)}</td></tr>`).join('');
        }
    } catch (e) { console.error('Regime detection error:', e); }
}

async function apiGet(path) {
    const cfg = window.GenieConfig || { baseUrl: 'http://localhost:8080' };
    const token = localStorage.getItem('genie_token') || 'demo-token';
    const resp = await fetch(cfg.baseUrl + path, { headers: { 'Authorization': 'Bearer ' + token } });
    return resp.json();
}
