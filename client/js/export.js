// Metis Genie Platform v5.5.11
/**
 * Metis Genie Platform v5.5.11 - Data Export/Import
 * @author Bennie Shearer  @copyright 2026
 */
const Export = {
    toCSV(data, filename = 'export.csv') {
        if (!data || !data.length) { U.log.warn('Export', 'toCSV: no data'); return; }
        U.log.info('Export', 'CSV export: ' + data.length + ' rows -> ' + filename);
        const headers = Object.keys(data[0]);
        const rows = data.map(row => headers.map(h => {
            let val = row[h]; if (val instanceof Date) val = val.toISOString();
            if (typeof val === 'string' && (val.includes(',') || val.includes('"'))) val = '"' + val.replace(/"/g, '""') + '"';
            return val;
        }).join(','));
        this.download([headers.join(','), ...rows].join('\n'), filename, 'text/csv');
    },
    toJSON(data, filename = 'export.json') { U.log.info('Export', 'JSON export -> ' + filename); this.download(JSON.stringify(data, null, 2), filename, 'application/json'); },
    download(content, filename, type) {
        const blob = new Blob([content], { type: type + ';charset=utf-8;' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a'); a.href = url; a.download = filename;
        document.body.appendChild(a); a.click(); document.body.removeChild(a); URL.revokeObjectURL(url);
    },
    holdings() { return Data.holdings.map(h => ({ Symbol:h.symbol, Name:h.name, Shares:h.shares, 'Avg Cost':h.cost, Price:h.price, Value:h.value, 'Wt%':h.weight, 'Chg%':h.change })); },
    positions() { return Data.getPositions().map(p => ({ Symbol:p.symbol, Name:p.name, Qty:p.shares, 'Avg Cost':p.cost, Price:p.price, Value:p.value, 'P&L':p.pnl.toFixed(2), 'P&L%':p.pnlPct.toFixed(2), 'Wt%':p.weight })); },
    transactions() { return Data.transactions.map(t => ({ Date:U.dateFull(t.date), Symbol:t.symbol, Type:t.type, Side:t.side, Qty:t.qty, Price:t.price, Amount:t.amount, Fees:t.fees, Net:t.net })); },
    // Import CSV
    importCSV(file) {
        return new Promise((resolve, reject) => {
            const reader = new FileReader();
            reader.onload = e => {
                try {
                    const lines = e.target.result.split('\n').filter(l => l.trim());
                    const headers = lines[0].split(',').map(h => h.trim().replace(/"/g, ''));
                    const data = lines.slice(1).map(line => {
                        const vals = line.split(',').map(v => v.trim().replace(/"/g, ''));
                        const obj = {};
                        headers.forEach((h, i) => obj[h] = isNaN(vals[i]) ? vals[i] : parseFloat(vals[i]));
                        return obj;
                    });
                    resolve(data);
                } catch (err) { reject(err); }
            };
            reader.onerror = () => reject(new Error('File read failed'));
            reader.readAsText(file);
        });
    }
};
window.Export = Export;
