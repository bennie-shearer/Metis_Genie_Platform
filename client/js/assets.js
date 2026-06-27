// Metis Genie Platform v5.5.11
/**
 * Assets Page - Metis Genie Platform v5.5.11
 * Handles: private assets, equity, fixed income, derivatives, FX, tenants
 */
'use strict';

document.addEventListener('DOMContentLoaded', async () => {
    await loadPrivateAssets();
    await loadEquityHoldings();
    await loadFixedIncome();
    await loadDerivatives();
    await loadFxCommodity();
    await loadTenants();
});

async function loadPrivateAssets() {
    try {
        const data = await apiGet('/api/v1/assets/private');
        const tbody = document.querySelector('#private-assets-table tbody');
        if (tbody && data.private_assets) {
            tbody.innerHTML = data.private_assets.map(a => `<tr>
                <td><strong>${a.name}</strong></td><td>${a.type}</td><td>${a.vintage}</td>
                <td>$${(a.committed/1e6).toFixed(1)}M</td><td>$${(a.called/1e6).toFixed(1)}M</td>
                <td>$${(a.nav/1e6).toFixed(1)}M</td>
                <td class="${a.irr >= 0 ? 'positive' : 'negative'}">${a.irr.toFixed(1)}%</td>
                <td>${a.tvpi.toFixed(2)}x</td>
                <td><span class="badge badge-${a.status === 'Active' ? 'green' : 'yellow'}">${a.status}</span></td>
            </tr>`).join('');
        }
    } catch (e) { console.error('Private assets error:', e); }
}

async function loadEquityHoldings() {
    try {
        const data = await apiGet('/api/v1/positions');
        const tbody = document.querySelector('#equity-table tbody');
        if (tbody && data.positions) {
            tbody.innerHTML = data.positions.map(p => `<tr>
                <td><strong>${p.symbol}</strong></td><td>${p.name || p.symbol}</td>
                <td>${p.quantity}</td><td>$${p.price.toFixed(2)}</td>
                <td>$${p.market_value.toLocaleString()}</td>
                <td>${p.weight ? p.weight.toFixed(1) + '%' : '-'}</td>
                <td class="${p.pnl >= 0 ? 'positive' : 'negative'}">$${p.pnl.toLocaleString()}</td>
                <td>${p.sector || '-'}</td></tr>`).join('');
        }
    } catch (e) { console.error('Equity error:', e); }
}

async function loadFixedIncome() {
    const tbody = document.querySelector('#fixed-income-table tbody');
    if (tbody) {
        tbody.innerHTML = `
            <tr><td>912828ZT</td><td>US Treasury</td><td>2.875%</td><td>2028-05-15</td><td>$500,000</td><td>$487,500</td><td>3.42%</td><td>4.2</td><td>AAA</td></tr>
            <tr><td>594918BQ</td><td>Microsoft Corp</td><td>3.125%</td><td>2029-12-15</td><td>$250,000</td><td>$242,800</td><td>3.85%</td><td>5.8</td><td>AAA</td></tr>
            <tr><td>037833AK</td><td>Apple Inc</td><td>2.650%</td><td>2030-05-11</td><td>$200,000</td><td>$188,400</td><td>4.02%</td><td>6.1</td><td>AA+</td></tr>`;
    }
}

async function loadDerivatives() {
    const tbody = document.querySelector('#derivatives-table tbody');
    if (tbody) {
        tbody.innerHTML = `
            <tr><td>AAPL 180C Mar26</td><td>Call</td><td>AAPL</td><td>$180</td><td>2026-03-21</td><td>10</td><td>$8,500</td><td>0.45</td><td>0.03</td></tr>
            <tr><td>SPY 600P Mar26</td><td>Put</td><td>SPY</td><td>$600</td><td>2026-03-21</td><td>5</td><td>$12,250</td><td>-0.35</td><td>0.02</td></tr>
            <tr><td>ES Mar26</td><td>Future</td><td>S&P 500</td><td>-</td><td>2026-03-20</td><td>2</td><td>$604,250</td><td>1.00</td><td>-</td></tr>`;
    }
}

async function loadFxCommodity() {
    const fxTbody = document.querySelector('#fx-table tbody');
    if (fxTbody) {
        fxTbody.innerHTML = `
            <tr><td>EUR/USD</td><td>EUR 500,000</td><td>1.0842</td><td class="positive">+$1,250</td></tr>
            <tr><td>GBP/USD</td><td>GBP 250,000</td><td>1.2615</td><td class="negative">-$480</td></tr>
            <tr><td>USD/JPY</td><td>JPY 50,000,000</td><td>148.52</td><td class="positive">+$820</td></tr>`;
    }
    const comTbody = document.querySelector('#commodity-table tbody');
    if (comTbody) {
        comTbody.innerHTML = `
            <tr><td>Gold Apr26</td><td>10 oz</td><td>$2,045</td><td>$20,450</td><td class="positive">+$650</td></tr>
            <tr><td>WTI Crude Mar26</td><td>100 bbl</td><td>$72.80</td><td>$7,280</td><td class="negative">-$120</td></tr>`;
    }
}

async function loadTenants() {
    try {
        const data = await apiGet('/api/v1/tenants');
        const tbody = document.querySelector('#tenants-table tbody');
        if (tbody && data.tenants) {
            tbody.innerHTML = data.tenants.map(t => `<tr>
                <td><strong>${t.name}</strong></td>
                <td><span class="badge badge-green">${t.status}</span></td>
                <td>${t.users}</td><td>${t.portfolios}</td><td>${t.aum}</td></tr>`).join('');
        }
    } catch (e) { console.error('Tenants error:', e); }
}

async function apiGet(path) {
    const cfg = window.GenieConfig || { baseUrl: 'http://localhost:8080' };
    const token = localStorage.getItem('genie_token') || 'demo-token';
    const resp = await fetch(cfg.baseUrl + path, { headers: { 'Authorization': 'Bearer ' + token } });
    return resp.json();
}
