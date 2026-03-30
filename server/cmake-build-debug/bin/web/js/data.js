// Metis Genie Platform v5.3.1
/**
 * Metis Genie Platform v5.3.1 - Data Layer
 * Fetches live data from server REST API with demo fallback
 * @author Bennie Shearer  @copyright 2026
 */
const Data = {
    ticker:[],holdings:[],allocation:[],activities:[],riskMetrics:[],sectors:[],portfolios:[],
    orders:[],transactions:[],alerts:[],stressScenarios:[],varMetrics:[],perfStats:[],
    benchStats:[],watchlist:[],corrMatrix:[],corrLabels:[],factors:[],

    async init() {
        if (typeof Connection !== 'undefined' && Connection.connected && !Connection.demoMode) {
            await Promise.allSettled([
                this.fetchMarket(), this.fetchPositions(), this.fetchPortfolios(),
                this.fetchOrders(), this.fetchRisk(), this.fetchAlerts(),
                this.fetchTransactions(), this.fetchBenchmark()
            ]);
        }
        this.ensureDefaults();
    },

    async fetchMarket() {
        try {
            const r = await Connection.get('/market');
            if (r.ok) { const d = r.data; this.ticker = d.map(m => ({ sym:m.symbol, price:m.price, chg:m.change })); }
        } catch (_) { console.debug("Data fetch error (market):", _); }
    },
    async fetchPositions() {
        try {
            const r = await Connection.get('/positions');
            if (r.ok) {
                const d = r.data;
                this.holdings = d.map(p => ({
                    symbol:p.symbol, name:p.name, shares:p.shares,
                    cost: (p.value - p.pnl) / p.shares, price:p.price,
                    value:p.value, weight:p.weight, change: p.pnl / (p.value - p.pnl) * 100
                }));
                this._deriveSectors();
                this.watchlist = d.map(p => ({
                    symbol:p.symbol, name:p.name, price:p.price,
                    change: p.pnl/p.shares, changePct: p.pnl/(p.value-p.pnl)*100,
                    volume: Math.floor(Math.random()*50e6)+10e6, high52:p.price*1.15, low52:p.price*0.70
                }));
            }
        } catch (_) { console.debug("Data fetch error (positions):", _); }
    },
    async fetchPortfolios() {
        try {
            const r = await Connection.get('/portfolios');
            if (r.ok) { const d = r.data; this.portfolios = d.map(p => ({ id:p.id, name:p.name, aum:p.aum, ytd:p.ytd, positions:0, sharpe:p.sharpe, status:p.status })); }
        } catch (_) { console.debug("Data fetch error (portfolios):", _); }
    },
    async fetchOrders() {
        try {
            const r = await Connection.get('/orders');
            if (r.ok) { const d = r.data; this.orders = d.map(o => ({ id:o.id, time:new Date(), symbol:o.symbol, side:o.side, qty:o.qty, price:o.price, type:o.type, status:o.status, fill:o.fill })); }
        } catch (_) { console.debug("Data fetch error (orders):", _); }
    },
    async fetchRisk() {
        try {
            const r = await Connection.get('/risk');
            if (r.ok) {
                const d = r.data;
                this.riskMetrics = [
                    {label:'Sharpe',value:d.sharpe},{label:'Sortino',value:d.sortino},{label:'Max DD',value:d.max_drawdown,neg:true},
                    {label:'Beta',value:d.beta},{label:'Alpha',value:d.alpha,pos:true},{label:'Vol',value:d.tracking_error}
                ];
                this.varMetrics = [{label:'VaR 95% 1D',value:d.var_95},{label:'VaR 99% 1D',value:d.var_99},{label:'CVaR 95%',value:d.cvar_95}];
            }
        } catch (_) { console.debug("Data fetch error (risk):", _); }
    },
    async fetchAlerts() {
        try {
            const r = await Connection.get('/alerts');
            if (r.ok) { const d = r.data; if (Array.isArray(d)) this.alerts = d.map((a,i) => ({ id:i+1, type:(a.severity||'info').toLowerCase(), title:a.title||a.name||a.type||'Alert', desc:a.description||a.message||'', time:new Date(a.timestamp||Date.now()), read:!!a.read })); }
        } catch (_) { console.debug("Data fetch error (alerts):", _); }
    },
    async fetchTransactions() {
        try {
            const r = await Connection.get('/transactions');
            if (r.ok) { const d = r.data; if (Array.isArray(d)) this.transactions = d.map(t => ({ date:new Date(t.date||t.timestamp), symbol:t.symbol, type:t.type||'Trade', side:t.side||'-', qty:t.qty||t.quantity||0, price:t.price||0, amount:t.amount||t.total||0, fees:t.fees||t.commission||0, net:t.net||(t.amount-(t.fees||0)) })); }
        } catch (_) { console.debug("Data fetch error (transactions):", _); }
    },
    async fetchBenchmark() {
        try {
            const r = await Connection.get('/benchmark');
            if (r.ok) {
                const d = r.data;
                if (d.alpha !== undefined) this.benchStats = [{label:'Alpha',value:d.alpha,pos:d.alpha>0},{label:'Beta',value:d.beta},{label:'R-Squared',value:d.r_squared},{label:'Tracking Err',value:d.tracking_error}];
                if (d.total_return !== undefined) this.perfStats = [{label:'Total Return',value:d.total_return,pos:true},{label:'Benchmark',value:d.benchmark_return,pos:true},{label:'Active Return',value:d.active_return,pos:d.active_return>0},{label:'Info Ratio',value:d.information_ratio}];
            }
        } catch (_) { console.debug("Data fetch error (benchmark):", _); }
    },

    ensureDefaults() {
        if (!this.ticker.length) this.ticker = [{sym:'SPX',price:5234.18,chg:0.45},{sym:'NDX',price:16428.82,chg:0.72},{sym:'DJI',price:39127.14,chg:0.28},{sym:'VIX',price:14.32,chg:-2.1},{sym:'TNX',price:4.25,chg:0.02},{sym:'DXY',price:104.52,chg:-0.15},{sym:'GC',price:2045.30,chg:0.85},{sym:'CL',price:78.45,chg:-1.23}];
        if (!this.holdings.length) this.holdings = [
            {symbol:'AAPL',name:'Apple Inc.',shares:150,cost:165.20,price:187.45,value:28117.50,weight:9.87,change:2.34},
            {symbol:'MSFT',name:'Microsoft',shares:100,cost:320.50,price:378.91,value:37891.00,weight:13.31,change:1.56},
            {symbol:'NVDA',name:'NVIDIA',shares:80,cost:420.00,price:495.22,value:39617.60,weight:13.92,change:3.45},
            {symbol:'GOOGL',name:'Alphabet',shares:50,cost:125.80,price:142.65,value:7132.50,weight:2.50,change:-0.82},
            {symbol:'AMZN',name:'Amazon',shares:75,cost:155.30,price:178.25,value:13368.75,weight:4.70,change:1.23},
            {symbol:'META',name:'Meta',shares:60,cost:480.00,price:505.95,value:30357.00,weight:10.66,change:-1.12},
            {symbol:'TSLA',name:'Tesla',shares:90,cost:225.00,price:248.50,value:22365.00,weight:7.85,change:-2.45},
            {symbol:'BRK.B',name:'Berkshire',shares:40,cost:352.00,price:410.85,value:16434.00,weight:5.77,change:0.68},
            {symbol:'JPM',name:'JPMorgan',shares:70,cost:168.50,price:195.30,value:13671.00,weight:4.80,change:0.92},
            {symbol:'UNH',name:'UnitedHealth',shares:25,cost:480.00,price:528.40,value:13210.00,weight:4.64,change:-0.35}
        ];
        if (!this.allocation.length) this.allocation = [{name:'US Equities',value:55,color:'#3b82f6'},{name:"Int'l Equities",value:20,color:'#10b981'},{name:'Fixed Income',value:15,color:'#f59e0b'},{name:'Alternatives',value:7,color:'#8b5cf6'},{name:'Cash',value:3,color:'#64748b'}];
        if (!this.activities.length) this.activities = [{type:'buy',title:'Bought NVDA',desc:'20 @ $492.50',time:new Date(Date.now()-7200000)},{type:'sell',title:'Sold GOOGL',desc:'10 @ $143.20',time:new Date(Date.now()-14400000)},{type:'div',title:'Dividend MSFT',desc:'$75.00',time:new Date(Date.now()-86400000)},{type:'buy',title:'Bought AAPL',desc:'25 @ $185.90',time:new Date(Date.now()-172800000)},{type:'sell',title:'Sold META',desc:'15 @ $510.25',time:new Date(Date.now()-259200000)},{type:'buy',title:'Bought JPM',desc:'30 @ $192.40',time:new Date(Date.now()-345600000)}];
        if (!this.riskMetrics.length) this.riskMetrics = [{label:'Sharpe',value:1.85},{label:'Sortino',value:2.42},{label:'Max DD',value:-8.34,neg:true},{label:'Beta',value:0.92},{label:'Alpha',value:3.21,pos:true},{label:'Vol',value:14.8}];
        if (!this.sectors.length) this.sectors = [{name:'Technology',value:45.2,color:'#3b82f6'},{name:'Healthcare',value:18.5,color:'#10b981'},{name:'Financials',value:15.3,color:'#f59e0b'},{name:'Consumer',value:12.8,color:'#ef4444'},{name:'Energy',value:8.2,color:'#8b5cf6'}];
        if (!this.portfolios.length) this.portfolios = [{id:'growth',name:'Growth Fund',aum:125400000,ytd:18.2,positions:45,sharpe:1.85,status:'Active'},{id:'value',name:'Value Fund',aum:78200000,ytd:12.5,positions:32,sharpe:1.42,status:'Active'},{id:'balanced',name:'Balanced Fund',aum:44900000,ytd:9.8,positions:58,sharpe:1.65,status:'Active'}];
        if (!this.orders.length) this.orders = [{id:'ORD-8472',time:new Date(Date.now()-1800000),symbol:'AAPL',side:'Buy',qty:50,price:186.50,type:'Limit',status:'Pending',fill:0},{id:'ORD-8471',time:new Date(Date.now()-3600000),symbol:'MSFT',side:'Sell',qty:25,price:379.00,type:'Limit',status:'Filled',fill:100},{id:'ORD-8470',time:new Date(Date.now()-7200000),symbol:'NVDA',side:'Buy',qty:20,price:492.50,type:'Market',status:'Filled',fill:100}];
        if (!this.transactions.length) this.transactions = [{date:new Date(Date.now()-86400000),symbol:'NVDA',type:'Trade',side:'Buy',qty:20,price:492.50,amount:9850.00,fees:4.95,net:-9854.95},{date:new Date(Date.now()-172800000),symbol:'GOOGL',type:'Trade',side:'Sell',qty:10,price:143.20,amount:1432.00,fees:4.95,net:1427.05},{date:new Date(Date.now()-259200000),symbol:'MSFT',type:'Dividend',side:'-',qty:100,price:0.75,amount:75.00,fees:0,net:75.00}];
        if (!this.alerts.length) this.alerts = [{id:1,type:'critical',title:'VaR Limit Breach',desc:'Portfolio VaR exceeded 2% threshold at 2.15%',time:new Date(Date.now()-300000),read:false},{id:2,type:'warning',title:'Price Alert: AAPL',desc:'AAPL crossed above $185 threshold',time:new Date(Date.now()-720000),read:false},{id:3,type:'info',title:'Rebalance Recommended',desc:'Growth Fund drift exceeds 5% threshold',time:new Date(Date.now()-3600000),read:false}];
        if (!this.stressScenarios.length) this.stressScenarios = [{name:'2008 Financial Crisis',impact:-42300000,pct:-14.8},{name:'COVID-19 Mar 2020',impact:-31700000,pct:-11.1},{name:'Rate +200bps',impact:-8400000,pct:-2.9},{name:'USD Rally 10%',impact:-5200000,pct:-1.8},{name:'Equity Crash -20%',impact:-56800000,pct:-19.9}];
        if (!this.varMetrics.length) this.varMetrics = [{label:'VaR 95% 1D',value:42711},{label:'VaR 99% 1D',value:58420},{label:'CVaR 95%',value:54830}];
        if (!this.perfStats.length) this.perfStats = [{label:'Total Return',value:18.42,pos:true},{label:'Benchmark',value:15.28,pos:true},{label:'Active Return',value:3.14,pos:true},{label:'Info Ratio',value:0.85}];
        if (!this.benchStats.length) this.benchStats = [{label:'Alpha',value:3.21,pos:true},{label:'Beta',value:0.92},{label:'R-Squared',value:0.94},{label:'Tracking Err',value:3.68}];
        if (!this.watchlist.length) this.watchlist = this.holdings.slice(0,5).map(h => ({symbol:h.symbol,name:h.name,price:h.price,change:h.price*h.change/100,changePct:h.change,volume:Math.floor(Math.random()*50e6)+10e6,high52:h.price*1.15,low52:h.price*0.70}));
        if (!this.corrMatrix.length) { this.corrMatrix=[[1,.82,.65,.78,.45],[.82,1,.71,.85,.38],[.65,.71,1,.62,.52],[.78,.85,.62,1,.41],[.45,.38,.52,.41,1]]; this.corrLabels=['AAPL','MSFT','NVDA','AMZN','TSLA']; }
        if (!this.factors.length) this.factors = [{name:'Market',value:85.2},{name:'Size',value:12.4},{name:'Value',value:-8.3},{name:'Momentum',value:22.1},{name:'Quality',value:15.8},{name:'Vol',value:-5.6}];
    },

    _deriveSectors() {
        const map = {'AAPL':'Technology','MSFT':'Technology','NVDA':'Technology','GOOGL':'Technology','META':'Technology','AMZN':'Consumer','TSLA':'Automotive','JPM':'Financials','BRK.B':'Financials','UNH':'Healthcare'};
        const colors = {Technology:'#3b82f6',Healthcare:'#10b981',Financials:'#f59e0b',Consumer:'#ef4444',Automotive:'#8b5cf6',Energy:'#06b6d4'};
        const totals = {}, tv = this.holdings.reduce((s,h) => s+h.value, 0);
        this.holdings.forEach(h => { const sec = map[h.symbol]||'Other'; totals[sec] = (totals[sec]||0)+h.value; });
        this.sectors = Object.entries(totals).map(([name,val]) => ({name, value:Math.round(val/tv*1000)/10, color:colors[name]||'#94a3b8'})).sort((a,b) => b.value-a.value);
    },

    getPositions() { return this.holdings.map(h => ({...h, pnl:(h.price-h.cost)*h.shares, pnlPct:((h.price-h.cost)/h.cost)*100})); }
};
window.Data = Data;
