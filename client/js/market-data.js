// Metis Genie Platform v5.5.11
/**
 * Market Data Page - Metis Genie Platform v5.5.11
 * Handles: feeds, corporate actions, order book, market overview
 */
'use strict';

document.addEventListener('DOMContentLoaded', async () => {
    await loadMarketOverview();
    await loadFeeds();
    await loadCorporateActions();
    await loadOrderBook();
});

async function loadMarketOverview() {
    try {
        const data = await apiGet('/api/v1/market/overview');
        const grid = document.getElementById('indices-grid');
        if (grid && data.indices) {
            grid.innerHTML = data.indices.map(idx => `
                <div class="metric-card">
                    <div class="metric-label">${idx.name}</div>
                    <div class="metric-value">${idx.value.toLocaleString()}</div>
                    <div class="metric-change ${idx.change_pct >= 0 ? 'positive' : 'negative'}">
                        ${idx.change_pct >= 0 ? '+' : ''}${idx.change_pct.toFixed(2)}%
                    </div>
                </div>
            `).join('');
        }
        const indicators = document.getElementById('market-indicators');
        if (indicators) {
            indicators.innerHTML = `
                <div class="metric"><span class="label">VIX</span><span class="value">${data.vix}</span></div>
                <div class="metric"><span class="label">10Y Treasury</span><span class="value">${data.treasury_10y}%</span></div>
                <div class="metric"><span class="label">USD Index</span><span class="value">${data.dxy}</span></div>
                <div class="metric"><span class="label">Status</span><span class="value">${data.market_status}</span></div>
            `;
        }
        const badge = document.getElementById('market-status');
        if (badge) { badge.textContent = data.market_status; badge.className = 'badge badge-green'; }

        if (data.sectors) {
            const bars = document.getElementById('sector-bars');
            if (bars) {
                bars.innerHTML = data.sectors.map(s => `
                    <div class="bar-row">
                        <span class="bar-label">${s.name}</span>
                        <div class="bar-track"><div class="bar-fill ${s.change_pct >= 0 ? 'positive' : 'negative'}"
                            style="width:${Math.min(Math.abs(s.change_pct) * 20, 100)}%"></div></div>
                        <span class="bar-value ${s.change_pct >= 0 ? 'positive' : 'negative'}">${s.change_pct >= 0 ? '+' : ''}${s.change_pct}%</span>
                    </div>
                `).join('');
            }
        }
    } catch (e) { console.error('Market overview error:', e); }
}

async function loadFeeds() {
    try {
        const data = await apiGet('/api/v1/feeds');
        const tbody = document.querySelector('#feeds-table tbody');
        if (tbody && data.feeds) {
            tbody.innerHTML = data.feeds.map(f => `<tr>
                <td><strong>${f.provider}</strong></td><td>${f.protocol}</td>
                <td><span class="badge badge-${f.status === 'Configured' ? 'green' : 'yellow'}">${f.status}</span></td>
                <td>${f.symbols_subscribed}</td><td>${f.latency_ms}ms</td><td>${f.messages_per_sec}</td>
                <td>${f.note}</td></tr>`).join('');
        }
        const summary = document.getElementById('feed-summary');
        if (summary) {
            summary.innerHTML = `
                <div class="metric"><span class="label">Total Symbols</span><span class="value">${data.total_symbols}</span></div>
                <div class="metric"><span class="label">Aggregate Msg/sec</span><span class="value">${data.aggregate_messages_per_sec}</span></div>
            `;
        }
    } catch (e) { console.error('Feeds error:', e); }
}

async function loadCorporateActions() {
    try {
        const data = await apiGet('/api/v1/market/corporate-actions');
        const pendingTbody = document.querySelector('#pending-actions-table tbody');
        if (pendingTbody && data.pending_actions) {
            pendingTbody.innerHTML = data.pending_actions.map(a => `<tr>
                <td>${a.action_id}</td><td>${a.type}</td><td><strong>${a.symbol}</strong></td>
                <td>${a.ex_date}</td>
                <td>${a.cash_amount ? '$' + a.cash_amount + '/share' : a.split_ratio ? a.split_ratio + ':1 split' : ''}</td>
                <td><span class="badge badge-yellow">${a.status}</span></td></tr>`).join('');
        }
        const procTbody = document.querySelector('#processed-actions-table tbody');
        if (procTbody && data.recent_processed) {
            procTbody.innerHTML = data.recent_processed.map(a => `<tr>
                <td>${a.action_id}</td><td>${a.type}</td><td><strong>${a.symbol}</strong></td>
                <td>${a.ex_date}</td><td>${a.impact}</td>
                <td><span class="badge badge-green">${a.status}</span></td></tr>`).join('');
        }
    } catch (e) { console.error('Corporate actions error:', e); }
}

async function loadOrderBook() {
    try {
        const data = await apiGet('/api/v1/trading/orderbook');
        const bidsTbody = document.querySelector('#bids-table tbody');
        const asksTbody = document.querySelector('#asks-table tbody');
        if (bidsTbody && data.bids) {
            bidsTbody.innerHTML = data.bids.map(b => `<tr class="bid-row">
                <td>${b.price.toFixed(2)}</td><td>${b.quantity.toLocaleString()}</td><td>${b.orders}</td></tr>`).join('');
        }
        if (asksTbody && data.asks) {
            asksTbody.innerHTML = data.asks.map(a => `<tr class="ask-row">
                <td>${a.price.toFixed(2)}</td><td>${a.quantity.toLocaleString()}</td><td>${a.orders}</td></tr>`).join('');
        }
        const metrics = document.getElementById('orderbook-metrics');
        if (metrics && data.orderbook) {
            const ob = data.orderbook;
            metrics.innerHTML = `
                <div class="metric"><span class="label">Symbol</span><span class="value">${ob.symbol}</span></div>
                <div class="metric"><span class="label">Last</span><span class="value">$${ob.last_trade_price}</span></div>
                <div class="metric"><span class="label">Spread</span><span class="value">${ob.spread_bps.toFixed(1)} bps</span></div>
                <div class="metric"><span class="label">Imbalance</span><span class="value">${(ob.bid_ask_imbalance * 100).toFixed(1)}%</span></div>
            `;
        }
        const tradesTbody = document.querySelector('#trades-table tbody');
        if (tradesTbody && data.recent_trades) {
            tradesTbody.innerHTML = data.recent_trades.map(t => `<tr>
                <td>${t.time}</td><td>$${t.price.toFixed(2)}</td><td>${t.quantity}</td>
                <td class="${t.side === 'buy' ? 'positive' : 'negative'}">${t.side.toUpperCase()}</td></tr>`).join('');
        }
    } catch (e) { console.error('Order book error:', e); }
}

async function apiGet(path) {
    const cfg = window.GenieConfig || { baseUrl: 'http://localhost:8080' };
    const token = localStorage.getItem('genie_token') || 'demo-token';
    const resp = await fetch(cfg.baseUrl + path, { headers: { 'Authorization': 'Bearer ' + token } });
    return resp.json();
}
