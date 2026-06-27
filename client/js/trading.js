// Metis Genie Platform v5.5.11
/* Metis Genie Platform Trading Page JavaScript v5.5.11 */
'use strict';

const Trading = {
    currentSymbol: 'AAPL',
    side: 'buy',
    symbols: [],
    positions: [],
    orders: [],
    fixStatus: null,
    transactions: [],

    // Hardcoded fallback symbols (used when server unavailable)
    defaultSymbols: [
        {sym:'AAPL',name:'Apple Inc',sector:'Technology',price:248.72,change:2.34,pct:0.95},
        {sym:'MSFT',name:'Microsoft Corp',sector:'Technology',price:452.18,change:5.67,pct:1.27},
        {sym:'GOOGL',name:'Alphabet Inc',sector:'Technology',price:178.93,change:-1.22,pct:-0.68},
        {sym:'AMZN',name:'Amazon.com Inc',sector:'Consumer',price:213.45,change:3.78,pct:1.80},
        {sym:'NVDA',name:'NVIDIA Corp',sector:'Technology',price:142.67,change:8.92,pct:6.67},
        {sym:'META',name:'Meta Platforms',sector:'Technology',price:612.34,change:-4.56,pct:-0.74},
        {sym:'TSLA',name:'Tesla Inc',sector:'Automotive',price:387.21,change:12.34,pct:3.29},
        {sym:'JPM',name:'JPMorgan Chase',sector:'Financial',price:258.90,change:1.45,pct:0.56},
        {sym:'V',name:'Visa Inc',sector:'Financial',price:312.45,change:2.10,pct:0.68},
        {sym:'JNJ',name:'Johnson & Johnson',sector:'Healthcare',price:158.90,change:-0.45,pct:-0.28},
        {sym:'WMT',name:'Walmart Inc',sector:'Consumer',price:187.23,change:1.89,pct:1.02},
        {sym:'PG',name:'Procter & Gamble',sector:'Consumer',price:172.56,change:0.78,pct:0.45},
        {sym:'UNH',name:'UnitedHealth Group',sector:'Healthcare',price:534.12,change:3.45,pct:0.65},
        {sym:'HD',name:'Home Depot',sector:'Consumer',price:398.67,change:-2.34,pct:-0.58},
        {sym:'BAC',name:'Bank of America',sector:'Financial',price:42.78,change:0.34,pct:0.80},
        {sym:'DIS',name:'Walt Disney',sector:'Entertainment',price:112.34,change:1.56,pct:1.41},
        {sym:'NFLX',name:'Netflix Inc',sector:'Entertainment',price:945.67,change:12.45,pct:1.33},
        {sym:'AMD',name:'AMD Inc',sector:'Technology',price:178.90,change:4.56,pct:2.61},
        {sym:'INTC',name:'Intel Corp',sector:'Technology',price:24.56,change:-0.89,pct:-3.50},
        {sym:'CRM',name:'Salesforce',sector:'Technology',price:312.45,change:5.67,pct:1.85},
        {sym:'SPY',name:'SPDR S&P 500 ETF',sector:'ETF',price:598.42,change:7.23,pct:1.22},
        {sym:'QQQ',name:'Invesco QQQ Trust',sector:'ETF',price:512.87,change:7.34,pct:1.45},
        {sym:'IWM',name:'iShares Russell 2000',sector:'ETF',price:223.45,change:1.93,pct:0.87},
        {sym:'GLD',name:'SPDR Gold Shares',sector:'Commodity',price:245.67,change:1.23,pct:0.50},
        {sym:'XLE',name:'Energy Select SPDR',sector:'Energy',price:89.34,change:-1.12,pct:-1.24},
    ],

    async init() {
        // Restore session for sub-page
        if (!Connection.connected) Connection.initSubPage();

        // Fetch live data from server
        await this.fetchServerData();

        this.initAutocomplete();
        this.initTradeTabs();
        this.initOrderType();
        this.initQuantity();
        this.initWatchlist();
        this.initTimeframe();
        this.initIndicators();
        this.initSubmitOrder();
        this.renderPositions();
        this.renderOrders();
        this.updateEstimate();
    },

    async fetchServerData() {
        if (!Connection.connected || Connection.demoMode) {
            this.symbols = this.defaultSymbols;
            return;
        }
        // Fetch positions and merge with default symbol list
        const [posR, ordR, mktR, fixR, txnR] = await Promise.allSettled([
            Connection.get('/positions'),
            Connection.get('/orders'),
            Connection.get('/market'),
            Connection.get('/fix/status'),
            Connection.get('/transactions')
        ]);
        // Positions -> update symbols with live prices
        if (posR.status === 'fulfilled' && posR.value.ok) {
            const sectorMap = {'AAPL':'Technology','MSFT':'Technology','NVDA':'Technology','GOOGL':'Technology',
                'META':'Technology','AMZN':'Consumer','TSLA':'Automotive','JPM':'Financial','V':'Financial',
                'UNH':'Healthcare','JNJ':'Healthcare','BAC':'Financial'};
            this.positions = posR.value.data;
            // Merge server positions into symbol list
            const posSyms = new Set(this.positions.map(p => p.symbol));
            this.symbols = this.defaultSymbols.map(s => {
                const pos = this.positions.find(p => p.symbol === s.sym);
                if (pos) return { sym: pos.symbol, name: pos.name, sector: sectorMap[pos.symbol] || s.sector,
                    price: pos.price, change: pos.pnl / pos.shares, pct: (pos.pnl / (pos.value - pos.pnl)) * 100 };
                return s;
            });
            // Add any server positions not in default list
            this.positions.filter(p => !this.defaultSymbols.find(s => s.sym === p.symbol)).forEach(p => {
                this.symbols.push({ sym: p.symbol, name: p.name, sector: sectorMap[p.symbol] || 'Other',
                    price: p.price, change: p.pnl / p.shares, pct: (p.pnl / (p.value - p.pnl)) * 100 });
            });
        } else {
            this.symbols = this.defaultSymbols;
        }
        // Orders
        if (ordR.status === 'fulfilled' && ordR.value.ok) {
            this.orders = ordR.value.data;
        }
        // Market data -> update watchlist prices
        if (mktR.status === 'fulfilled' && mktR.value.ok) {
            this.marketData = mktR.value.data;
        }
        // FIX Protocol Status
        if (fixR.status === 'fulfilled' && fixR.value.ok) {
            this.fixStatus = fixR.value.data;
            this.renderFixStatus();
        }
        // Transaction History
        if (txnR.status === 'fulfilled' && txnR.value.ok) {
            this.transactions = txnR.value.data;
            this.renderTransactions();
        }
    },

    renderPositions() {
        const tbody = document.getElementById('positionsBody');
        if (!tbody || !this.positions.length) return;
        tbody.innerHTML = this.positions.map(p => {
            const pnlCls = p.pnl >= 0 ? 'up' : 'down';
            const pnlPct = ((p.pnl / (p.value - p.pnl)) * 100).toFixed(2);
            return `<tr class="pos-row clickable" data-symbol="${p.symbol}">
                <td class="sym">${p.symbol}</td><td>${p.name}</td>
                <td class="r">${p.shares}</td><td class="r">$${p.price.toFixed(2)}</td>
                <td class="r">$${p.value.toLocaleString('en-US',{minimumFractionDigits:2})}</td>
                <td class="r ${pnlCls}">${p.pnl >= 0 ? '+' : ''}$${p.pnl.toLocaleString('en-US',{minimumFractionDigits:2})}</td>
                <td class="r ${pnlCls}">${pnlPct}%</td>
                <td class="r">${p.weight}%</td></tr>`;
        }).join('');
        // Click to select symbol
        tbody.querySelectorAll('.clickable').forEach(row => {
            row.addEventListener('click', () => this.selectSymbol(row.dataset.symbol));
        });
    },

    renderOrders() {
        const tbody = document.getElementById('ordersBody');
        if (!tbody || !this.orders.length) return;
        tbody.innerHTML = this.orders.map(o => {
            const cls = o.status === 'Filled' ? 'filled' : o.status === 'Pending' ? 'pending' : 'cancelled';
            return `<tr><td>${o.id}</td><td class="sym">${o.symbol}</td>
                <td class="${o.side === 'Buy' ? 'up' : 'down'}">${o.side}</td>
                <td class="r">${o.qty}</td><td class="r">$${o.price.toFixed(2)}</td>
                <td>${o.type}</td><td><span class="badge ${cls}">${o.status}</span></td>
                <td class="r">${o.fill}%</td></tr>`;
        }).join('');
    },

    // Symbol Autocomplete
    initAutocomplete() {
        const input = document.getElementById('symbolSearch');
        const dropdown = document.getElementById('autocompleteDropdown');
        if (!input || !dropdown) return;

        input.addEventListener('input', () => {
            const q = input.value.trim().toUpperCase();
            if (q.length < 1) { dropdown.classList.remove('show'); return; }
            const results = this.symbols.filter(s =>
                s.sym.startsWith(q) || s.name.toUpperCase().includes(q)
            ).slice(0, 8);

            if (results.length === 0) { dropdown.classList.remove('show'); return; }
            dropdown.innerHTML = results.map(s => `
                <div class="ac-item" data-symbol="${s.sym}">
                    <div><span class="ac-sym">${s.sym}</span> <span class="ac-name">${s.name}</span></div>
                    <span class="ac-sector">${s.sector}</span>
                </div>
            `).join('');
            dropdown.classList.add('show');

            dropdown.querySelectorAll('.ac-item').forEach(item => {
                item.addEventListener('click', () => {
                    this.selectSymbol(item.dataset.symbol);
                    input.value = '';
                    dropdown.classList.remove('show');
                });
            });
        });

        input.addEventListener('keydown', (e) => {
            if (e.key === 'Escape') dropdown.classList.remove('show');
        });

        document.addEventListener('click', (e) => {
            if (!input.contains(e.target) && !dropdown.contains(e.target))
                dropdown.classList.remove('show');
        });
    },

    selectSymbol(sym) {
        this.currentSymbol = sym;
        const data = this.symbols.find(s => s.sym === sym);
        if (!data) return;

        const el = (id) => document.getElementById(id);
        if (el('chartSymbol')) el('chartSymbol').textContent = sym;
        if (el('chartCompany')) el('chartCompany').textContent = data.name;
        if (el('chartPrice')) el('chartPrice').textContent = data.price.toFixed(2);
        if (el('chartChange')) {
            const ch = el('chartChange');
            ch.textContent = `${data.change >= 0 ? '+' : ''}${data.change.toFixed(2)} (${data.pct >= 0 ? '+' : ''}${data.pct.toFixed(2)}%)`;
            ch.className = 'chart-change ' + (data.change >= 0 ? 'up' : 'down');
        }
        if (el('ticketSymbol')) el('ticketSymbol').textContent = sym;
        if (el('submitOrder')) el('submitOrder').textContent = `${this.side === 'buy' ? 'Buy' : 'Sell'} ${sym}`;

        document.querySelectorAll('.wl-item').forEach(item => {
            item.classList.toggle('active', item.dataset.symbol === sym);
        });

        this.updateEstimate();
    },

    initTradeTabs() {
        document.querySelectorAll('.ticket-tab').forEach(tab => {
            tab.addEventListener('click', () => {
                document.querySelectorAll('.ticket-tab').forEach(t => t.classList.remove('active'));
                tab.classList.add('active');
                this.side = tab.dataset.side;
                const btn = document.getElementById('submitOrder');
                if (btn) {
                    btn.textContent = `${this.side === 'buy' ? 'Buy' : 'Sell'} ${this.currentSymbol}`;
                    btn.className = 'btn-trade ' + this.side;
                }
            });
        });
    },

    initOrderType() {
        const sel = document.getElementById('orderType');
        if (!sel) return;
        sel.addEventListener('change', () => {
            const t = sel.value;
            const lf = document.getElementById('limitPriceField');
            const sf = document.getElementById('stopPriceField');
            if (lf) lf.style.display = (t === 'limit' || t === 'stop_limit') ? '' : 'none';
            if (sf) sf.style.display = (t === 'stop' || t === 'stop_limit' || t === 'trailing_stop') ? '' : 'none';
        });
    },

    initQuantity() {
        const qty = document.getElementById('orderQty');
        const minus = document.getElementById('qtyMinus');
        const plus = document.getElementById('qtyPlus');
        if (minus) minus.addEventListener('click', () => { if (qty && qty.value > 1) { qty.value = parseInt(qty.value) - 1; this.updateEstimate(); } });
        if (plus) plus.addEventListener('click', () => { if (qty) { qty.value = parseInt(qty.value) + 1; this.updateEstimate(); } });
        if (qty) qty.addEventListener('change', () => this.updateEstimate());
    },

    updateEstimate() {
        const data = this.symbols.find(s => s.sym === this.currentSymbol);
        const qty = parseInt(document.getElementById('orderQty')?.value || 100);
        const price = data ? data.price : 0;
        const total = price * qty;
        const el = (id) => document.getElementById(id);
        if (el('estTotal')) el('estTotal').textContent = '$' + total.toLocaleString('en-US', {minimumFractionDigits:2});
        if (el('estComm')) el('estComm').textContent = '$0.00';
    },

    initWatchlist() {
        document.querySelectorAll('.wl-item').forEach(item => {
            item.addEventListener('click', () => this.selectSymbol(item.dataset.symbol));
        });
    },

    initTimeframe() {
        document.querySelectorAll('.tf-btn').forEach(btn => {
            btn.addEventListener('click', () => {
                document.querySelectorAll('.tf-btn').forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
            });
        });
        document.querySelectorAll('.ct-btn').forEach(btn => {
            btn.addEventListener('click', () => {
                document.querySelectorAll('.ct-btn').forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
            });
        });
    },

    initIndicators() {
        document.querySelectorAll('.ind-chip').forEach(chip => {
            chip.addEventListener('click', () => chip.classList.toggle('active'));
        });
    },

    initSubmitOrder() {
        const btn = document.getElementById('submitOrder');
        if (btn) {
            btn.addEventListener('click', () => {
                const qty = document.getElementById('orderQty')?.value || 100;
                const type = document.getElementById('orderType')?.value || 'market';
                const limitPrice = document.getElementById('limitPrice')?.value || '';
                const data = this.symbols.find(s => s.sym === this.currentSymbol);
                const price = data ? data.price : 0;
                const details = document.getElementById('confirmDetails');
                if (details) {
                    details.innerHTML = `
                        <div class="pos-row"><span>Action</span><span class="${this.side}">${this.side.toUpperCase()}</span></div>
                        <div class="pos-row"><span>Symbol</span><span><b>${this.currentSymbol}</b></span></div>
                        <div class="pos-row"><span>Quantity</span><span>${qty}</span></div>
                        <div class="pos-row"><span>Order Type</span><span>${type.replace('_', ' ').toUpperCase()}</span></div>
                        <div class="pos-row"><span>Est. Price</span><span>$${price.toFixed(2)}</span></div>
                        <div class="pos-row"><span>Est. Total</span><span><b>$${(price * qty).toLocaleString('en-US', {minimumFractionDigits:2})}</b></span></div>
                    `;
                }
                const modal = document.getElementById('orderConfirmBg');
                if (modal) modal.style.display = 'flex';
            });
        }

        // Modal close
        ['closeOrderConfirm', 'cancelOrder'].forEach(id => {
            const el = document.getElementById(id);
            if (el) el.addEventListener('click', () => {
                document.getElementById('orderConfirmBg').style.display = 'none';
            });
        });

        const confirm = document.getElementById('confirmOrder');
        if (confirm) {
            confirm.addEventListener('click', async () => {
                document.getElementById('orderConfirmBg').style.display = 'none';
                const qty = parseInt(document.getElementById('orderQty')?.value || 100);
                const type = document.getElementById('orderType')?.value || 'market';
                const limitPrice = parseFloat(document.getElementById('limitPrice')?.value || 0);

                // Submit to server
                if (Connection.connected && !Connection.demoMode) {
                    const body = {
                        symbol: this.currentSymbol, side: this.side === 'buy' ? 'Buy' : 'Sell',
                        qty: qty, type: type, price: limitPrice || undefined
                    };
                    const r = await Connection.post('/orders', body);
                    if (r.ok) {
                        this.showNotification('success', `Order ${r.data.id}: ${r.data.side} ${this.currentSymbol} - ${r.data.status}`);
                        // Refresh orders
                        const ordR = await Connection.get('/orders');
                        if (ordR.ok) { this.orders = ordR.data; this.renderOrders(); }
                        return;
                    }
                }
                // Fallback notification
                this.showNotification('info', `Order submitted: ${this.side.toUpperCase()} ${qty} ${this.currentSymbol}`);
            });
        }
    },

    renderFixStatus() {
        const el = document.getElementById('fixStatusData');
        if (!el || !this.fixStatus) return;
        const f = this.fixStatus;
        const sessions = f.sessions || [];
        el.innerHTML = sessions.map(s =>
            '<div style="display:flex;justify-content:space-between;padding:6px 0;font-size:13px;">' +
            '<span style="color:#94a3b8;">' + s.session_id + '</span>' +
            '<span style="color:' + (s.connected ? '#4ade80' : '#f87171') + ';">' +
            (s.connected ? 'Connected' : 'Disconnected') + '</span></div>'
        ).join('') || '<div style="color:#94a3b8;font-size:13px;">No active FIX sessions</div>';
    },

    renderTransactions() {
        const el = document.getElementById('txnBody');
        if (!el || !this.transactions) return;
        const txns = Array.isArray(this.transactions) ? this.transactions : this.transactions.transactions || [];
        el.innerHTML = txns.slice(0, 20).map(t =>
            '<tr><td>' + (t.id || '') + '</td><td style="font-weight:600">' + (t.symbol || '') +
            '</td><td>' + (t.side || '') + '</td><td class="r">' + (t.quantity || 0) +
            '</td><td class="r">$' + (t.price || 0).toFixed(2) +
            '</td><td style="font-size:.8rem;color:#64748b">' + (t.timestamp || '') + '</td></tr>'
        ).join('') || '<tr><td colspan="6" style="text-align:center;color:#94a3b8">No transactions</td></tr>';
    },

    showNotification(type, message) {
        const bar = document.getElementById('tradeNotification');
        if (bar) {
            bar.className = 'trade-notification show ' + type;
            bar.textContent = message;
            setTimeout(() => bar.className = 'trade-notification', 4000);
        } else {
            alert(message);
        }
    }
};

document.addEventListener('DOMContentLoaded', () => Trading.init());
