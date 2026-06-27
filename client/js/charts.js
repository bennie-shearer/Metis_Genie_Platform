// Metis Genie Platform v5.5.11
/**
 * Metis Genie Platform v5.5.11 - Canvas Charts
 * Pure Vanilla JS
 * @author Bennie Shearer  @copyright 2026
 */
const Charts = {
    css(n) { return getComputedStyle(document.documentElement).getPropertyValue(n).trim(); },
    setup(canvas, w, h) {
        const dpr = window.devicePixelRatio || 1;
        canvas.width = w * dpr; canvas.height = h * dpr;
        canvas.style.width = w + 'px'; canvas.style.height = h + 'px';
        const ctx = canvas.getContext('2d'); ctx.scale(dpr, dpr);
        return { ctx, w, h };
    },
    line(canvas, data, opts = {}) {
        const rect = canvas.parentElement.getBoundingClientRect();
        const { ctx, w, h } = this.setup(canvas, rect.width, opts.height || 260);
        const p = { t: 20, r: 20, b: 30, l: 50 };
        const cw = w - p.l - p.r, ch = h - p.t - p.b;
        ctx.clearRect(0, 0, w, h);
        const all = data.datasets.flatMap(d => d.data);
        const mn = Math.min(...all) * 0.95, mx = Math.max(...all) * 1.05, rng = mx - mn || 1;
        const sx = i => p.l + (i / (data.labels.length - 1)) * cw;
        const sy = v => p.t + ch - ((v - mn) / rng) * ch;
        ctx.strokeStyle = this.css('--border'); ctx.lineWidth = 1; ctx.setLineDash([3, 3]);
        for (let i = 0; i <= 4; i++) { const y = p.t + ch * i / 4; ctx.beginPath(); ctx.moveTo(p.l, y); ctx.lineTo(w - p.r, y); ctx.stroke(); }
        ctx.setLineDash([]);
        ctx.fillStyle = this.css('--fg-3'); ctx.font = '11px ' + this.css('--font'); ctx.textAlign = 'right'; ctx.textBaseline = 'middle';
        for (let i = 0; i <= 4; i++) ctx.fillText((mx - rng * i / 4).toFixed(1), p.l - 6, p.t + ch * i / 4);
        data.datasets.forEach((ds, di) => {
            const c = ds.color || U.getColor(di);
            if (ds.fill) {
                ctx.beginPath(); ctx.moveTo(sx(0), sy(ds.data[0]));
                ds.data.forEach((v, i) => ctx.lineTo(sx(i), sy(v)));
                ctx.lineTo(sx(ds.data.length - 1), h - p.b); ctx.lineTo(sx(0), h - p.b); ctx.closePath();
                const grad = ctx.createLinearGradient(0, p.t, 0, h - p.b);
                grad.addColorStop(0, c + '30'); grad.addColorStop(1, c + '05'); ctx.fillStyle = grad; ctx.fill();
            }
            ctx.strokeStyle = c; ctx.lineWidth = 2; ctx.beginPath();
            ds.data.forEach((v, i) => { if (i === 0) ctx.moveTo(sx(i), sy(v)); else ctx.lineTo(sx(i), sy(v)); });
            ctx.stroke();
        });
        ctx.fillStyle = this.css('--fg-3'); ctx.textAlign = 'center'; ctx.textBaseline = 'top';
        const step = Math.ceil(data.labels.length / 6);
        data.labels.forEach((l, i) => { if (i % step === 0 || i === data.labels.length - 1) ctx.fillText(l, sx(i), h - p.b + 6); });
        canvas._chartData = { data, sx, sy, p, mn, mx, rng, w, h };
    },
    donut(canvas, data, opts = {}) {
        const sz = opts.size || 180;
        const { ctx, w, h } = this.setup(canvas, sz, sz);
        ctx.clearRect(0, 0, w, h);
        const cx = w/2, cy = h/2, r = (Math.min(w,h)/2)-8, ir = r * 0.62;
        const total = data.reduce((a, d) => a + d.value, 0);
        let angle = -Math.PI / 2;
        data.forEach((d, i) => {
            const slice = (d.value / total) * Math.PI * 2;
            ctx.beginPath(); ctx.moveTo(cx, cy); ctx.arc(cx, cy, r, angle, angle + slice); ctx.closePath();
            ctx.fillStyle = d.color || U.getColor(i); ctx.fill(); angle += slice;
        });
        ctx.beginPath(); ctx.arc(cx, cy, ir, 0, Math.PI * 2); ctx.fillStyle = this.css('--bg-1'); ctx.fill();
    },
    bar(canvas, data, opts = {}) {
        const rect = canvas.parentElement.getBoundingClientRect();
        const { ctx, w, h } = this.setup(canvas, rect.width, opts.height || 200);
        const p = { t: 20, r: 20, b: 40, l: 40 };
        ctx.clearRect(0, 0, w, h);
        const vals = data.map(d => d.value);
        const mx = Math.max(...vals.map(Math.abs)) * 1.15;
        const cw = w - p.l - p.r, ch = h - p.t - p.b;
        const barW = (cw / data.length) - 8;
        const zero = p.t + ch / 2;
        ctx.strokeStyle = this.css('--border'); ctx.lineWidth = 1;
        ctx.beginPath(); ctx.moveTo(p.l, zero); ctx.lineTo(w - p.r, zero); ctx.stroke();
        data.forEach((d, i) => {
            const x = p.l + i * (cw / data.length) + 4;
            const barH = (Math.abs(d.value) / mx) * (ch / 2);
            const y = d.value >= 0 ? zero - barH : zero;
            ctx.fillStyle = d.color || (d.value >= 0 ? this.css('--green') : this.css('--red'));
            ctx.beginPath(); ctx.roundRect(x, y, barW, barH, 3); ctx.fill();
            ctx.fillStyle = this.css('--fg-3'); ctx.font = '10px ' + this.css('--font');
            ctx.textAlign = 'center'; ctx.fillText(d.label, x + barW / 2, h - 8);
        });
    },
    spark(canvas, data, opts = {}) {
        const { ctx, w, h } = this.setup(canvas, opts.width || 80, opts.height || 30);
        ctx.clearRect(0, 0, w, h);
        const mn = Math.min(...data), mx = Math.max(...data), rng = mx - mn || 1;
        const up = data[data.length - 1] >= data[0];
        ctx.strokeStyle = up ? this.css('--green') : this.css('--red');
        ctx.lineWidth = 1.5; ctx.beginPath();
        data.forEach((v, i) => {
            const x = (i / (data.length - 1)) * w;
            const y = h - ((v - mn) / rng) * (h - 4) - 2;
            if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
        }); ctx.stroke();
    },
    distribution(canvas, data, opts = {}) {
        const rect = canvas.parentElement.getBoundingClientRect();
        const { ctx, w, h } = this.setup(canvas, rect.width, opts.height || 150);
        const p = { t: 10, r: 20, b: 20, l: 20 };
        ctx.clearRect(0, 0, w, h);
        const mx = Math.max(...data), cw = w - p.l - p.r, ch = h - p.t - p.b;
        const bw = cw / data.length;
        data.forEach((v, i) => { const x = p.l + i * bw, bh = (v / mx) * ch; ctx.fillStyle = this.css('--accent') + '50'; ctx.fillRect(x, p.t + ch - bh, bw - 1, bh); });
        if (opts.varLine !== undefined) {
            const vx = p.l + (opts.varLine / data.length) * cw;
            ctx.strokeStyle = this.css('--red'); ctx.lineWidth = 2; ctx.setLineDash([4, 4]);
            ctx.beginPath(); ctx.moveTo(vx, p.t); ctx.lineTo(vx, h - p.b); ctx.stroke(); ctx.setLineDash([]);
            ctx.fillStyle = this.css('--red'); ctx.font = '10px ' + this.css('--font'); ctx.textAlign = 'center'; ctx.fillText('VaR 95%', vx, h - 4);
        }
    },
    heatmap(canvas, matrix, labels, opts = {}) {
        const sz = labels.length, cellSz = opts.cellSize || 50, lbl = opts.labelWidth || 60;
        const total = lbl + sz * cellSz + 10;
        const { ctx, w, h } = this.setup(canvas, total, total);
        ctx.clearRect(0, 0, w, h);
        ctx.fillStyle = this.css('--fg-2'); ctx.font = '11px ' + this.css('--font');
        ctx.textAlign = 'right'; ctx.textBaseline = 'middle';
        labels.forEach((l, i) => ctx.fillText(l, lbl - 8, lbl + i * cellSz + cellSz / 2));
        ctx.textAlign = 'center'; ctx.textBaseline = 'bottom';
        labels.forEach((l, i) => { ctx.save(); ctx.translate(lbl + i * cellSz + cellSz / 2, lbl - 6); ctx.rotate(-0.5); ctx.fillText(l, 0, 0); ctx.restore(); });
        for (let r = 0; r < sz; r++) for (let c = 0; c < sz; c++) {
            const v = matrix[r][c], x = lbl + c * cellSz, y = lbl + r * cellSz;
            if (v >= 0) ctx.fillStyle = `rgba(16,185,129,${Math.round(v * 200) / 255})`; else ctx.fillStyle = `rgba(239,68,68,${Math.round(Math.abs(v) * 200) / 255})`;
            ctx.fillRect(x + 1, y + 1, cellSz - 2, cellSz - 2);
            ctx.fillStyle = Math.abs(v) > 0.5 ? '#fff' : this.css('--fg-0'); ctx.font = '10px ' + this.css('--mono'); ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
            ctx.fillText(v.toFixed(2), x + cellSz / 2, y + cellSz / 2);
        }
    },
    randomWalk(pts, start = 100, vol = 0.015) { const d = [start]; for (let i = 1; i < pts; i++) d.push(d[i-1] * (1 + (Math.random() - 0.48) * vol)); return d; },
    dateLabels(days) { const l = []; const now = new Date(); for (let i = days - 1; i >= 0; i--) { const d = new Date(now); d.setDate(d.getDate() - i); l.push((d.getMonth()+1)+'/'+d.getDate()); } return l; }
};
if (!CanvasRenderingContext2D.prototype.roundRect) { CanvasRenderingContext2D.prototype.roundRect = function(x,y,w,h,r) { if(w<2*r)r=w/2;if(h<2*r)r=h/2; this.moveTo(x+r,y);this.arcTo(x+w,y,x+w,y+h,r);this.arcTo(x+w,y+h,x,y+h,r);this.arcTo(x,y+h,x,y,r);this.arcTo(x,y,x+w,y,r);this.closePath(); }; }
window.Charts = Charts;
