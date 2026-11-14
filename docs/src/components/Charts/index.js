// Charts for the benchmark pages: plain SVG, no chart library. Each chart sizes itself to its container, shows a
// tooltip on hover, focus and tap, and keeps every value in the page's tables (the tooltip never holds the only copy).
// Colours are CSS custom properties (src/css/charts.css), with their own dark-mode steps.
import React, {useEffect, useRef, useState} from 'react';
import styles from './styles.module.css';

function useWidth(fallback) {
  const ref = useRef(null);
  const [w, setW] = useState(fallback);
  useEffect(() => {
    const el = ref.current;
    if (!el || typeof ResizeObserver === 'undefined') return undefined;
    const ro = new ResizeObserver((entries) => setW(Math.max(280, Math.floor(entries[0].contentRect.width))));
    ro.observe(el);
    return () => ro.disconnect();
  }, []);
  return [ref, w];
}

const fmt = (v) => (v >= 1e6 ? `${(v / 1e6).toFixed(2)}M` : v >= 1e4 ? `${Math.round(v / 1e3)}k` : v >= 1e3 ? `${(v / 1e3).toFixed(1)}k` : `${Math.round(v)}`);
const full = (v) => Math.round(v).toLocaleString('en-US');

// clean ticks: 0 and steps of 1, 2 or 5 × 10^n, at most `n` of them
function ticks(max, n = 5) {
  const raw = max / n;
  const p = 10 ** Math.floor(Math.log10(raw));
  const step = [1, 2, 5, 10].map((m) => m * p).find((s) => s >= raw);
  const out = [];
  for (let v = 0; v <= max + 1e-9; v += step) out.push(v);
  if (out[out.length - 1] < max) out.push(out[out.length - 1] + step);
  return out;
}

function Tooltip({tip, docked}) {
  if (!tip) return null;
  // docked (narrow screens): a readout under the chart instead of a box over it
  return (
    <div className={docked ? styles.tooltipDocked : styles.tooltip} style={docked ? undefined : {left: tip.x, top: tip.y}} role="status">
      <div className={styles.tipTitle}>{tip.title}</div>
      {tip.rows.map((r) => (
        <div key={r.label} className={styles.tipRow}>
          {r.key && <span className={styles.key} style={{background: `var(${r.key})`}} aria-hidden="true" />}
          <strong>{r.value}</strong>
          <span>{r.label}</span>
        </div>
      ))}
    </div>
  );
}

/**
 * Horizontal bars, largest first: one bar per server.
 * rows: [{name, value, highlight?, detail?: [[label, value], ...]}]; unit: axis caption.
 */
export function BarChart({title, caption, rows, unit = 'requests per second'}) {
  const [ref, width] = useWidth(640);
  const [tip, setTip] = useState(null);
  const sorted = [...rows].sort((a, b) => b.value - a.value);
  // narrow screens put each name above its bar; wide ones keep a column of names long enough for the longest
  const stacked = width < 560;
  const labelW = stacked ? 4 : 230;
  const valueW = 56;
  const band = stacked ? 46 : 30;
  const bar = 18;
  const barTop = stacked ? 20 : (band - bar) / 2;
  const top = 8;
  const plotW = width - labelW - valueW - 8;
  const tk = ticks(sorted[0].value, width < 480 ? 4 : 5);
  const max = tk[tk.length - 1];
  const x = (v) => labelW + (v / max) * plotW;
  const h = top + sorted.length * band + 28;
  const show = (r, i, ev) => {
    const box = ref.current.getBoundingClientRect();
    const px = ev && ev.clientX ? ev.clientX - box.left : x(r.value);
    setTip({
      x: Math.min(px + 12, width - 200),
      y: top + i * band + band,
      title: r.name,
      rows: [{label: unit, value: full(r.value)}, ...(r.detail || []).map(([l, v]) => ({label: l, value: v}))],
    });
  };
  return (
    <figure className={styles.figure}>
      {title && <figcaption className={styles.title}>{title}</figcaption>}
      <div ref={ref} className={styles.plot} onMouseLeave={() => setTip(null)}>
        <svg width={width} height={h} role="img" aria-label={`${title || 'Bar chart'}: ${sorted.map((r) => `${r.name} ${full(r.value)}`).join(', ')}`}>
          {tk.map((t) => (
            <g key={t}>
              <line x1={x(t)} x2={x(t)} y1={top} y2={h - 24} className={styles.grid} />
              <text x={x(t)} y={h - 8} textAnchor="middle" className={styles.axis}>{fmt(t)}</text>
            </g>
          ))}
          {sorted.map((r, i) => {
            const y = top + i * band + barTop;
            const w = Math.max(2, x(r.value) - labelW);
            const rr = Math.min(4, w / 2);
            // square at the baseline, 4 px rounded at the data end
            const d = `M${labelW},${y} h${w - rr} a${rr},${rr} 0 0 1 ${rr},${rr} v${bar - 2 * rr} a${rr},${rr} 0 0 1 ${-rr},${rr} h${-(w - rr)} z`;
            return (
              <g
                key={r.name}
                tabIndex={0}
                className={styles.hit}
                onMouseMove={(ev) => show(r, i, ev)}
                onFocus={() => show(r, i)}
                onBlur={() => setTip(null)}
                onClick={(ev) => show(r, i, ev)}
                aria-label={`${r.name}: ${full(r.value)} ${unit}`}>
                <rect x={0} y={top + i * band} width={width} height={band} fill="transparent" />
                <text x={stacked ? labelW : labelW - 10} y={stacked ? y - 6 : y + bar / 2} dy={stacked ? 0 : '0.35em'} textAnchor={stacked ? 'start' : 'end'} className={r.highlight ? styles.labelStrong : styles.label}>
                  {r.name}
                </text>
                <path d={d} fill={r.highlight ? 'var(--chart-accent)' : 'var(--chart-muted)'} />
                <text x={labelW + w + 6} y={y + bar / 2} dy="0.35em" className={styles.value}>{fmt(r.value)}</text>
              </g>
            );
          })}
        </svg>
        <Tooltip tip={tip} docked={width < 560} />
      </div>
      {caption && <p className={styles.caption}>{caption}</p>}
    </figure>
  );
}

/**
 * Lines over discrete levels (evenly spaced, e.g. connections), one line per series, with a crosshair tooltip.
 * levels: [16, 32, ...]; series: [{name, values: [v | null per level], color: 1..6}]; xLabel: axis caption.
 */
export function LineChart({title, caption, levels, series, xLabel = 'connections', unit = 'requests per second'}) {
  const [ref, width] = useWidth(640);
  const [hover, setHover] = useState(null);
  const left = 44;
  const right = 12;
  const top = 12;
  const h = width < 480 ? 240 : 300;
  const bottom = 40;
  const all = series.flatMap((s) => s.values.filter((v) => v != null));
  const tk = ticks(Math.max(...all), 4);
  const max = tk[tk.length - 1];
  const step = (width - left - right) / Math.max(1, levels.length - 1);
  const x = (i) => left + i * step;
  const y = (v) => top + (1 - v / max) * (h - top - bottom);
  const at = (ev) => {
    const box = ref.current.getBoundingClientRect();
    const cx = (ev.touches ? ev.touches[0].clientX : ev.clientX) - box.left;
    setHover(Math.max(0, Math.min(levels.length - 1, Math.round((cx - left) / step))));
  };
  const tip =
    hover == null
      ? null
      : {
          // on the side away from the crosshair, so it never covers the points it explains
          x: x(hover) > width / 2 ? Math.max(0, x(hover) - 262) : x(hover) + 12,
          y: top,
          title: `${levels[hover].toLocaleString('en-US')} ${xLabel}`,
          rows: series
            .map((s) => ({label: s.name, v: s.values[hover], key: `--chart-s${s.color}`}))
            .filter((r) => r.v != null)
            .sort((a, b) => b.v - a.v)
            .map((r) => ({...r, value: full(r.v)})),
        };
  return (
    <figure className={styles.figure}>
      {title && <figcaption className={styles.title}>{title}</figcaption>}
      <ul className={styles.legend}>
        {series.map((s) => (
          <li key={s.name}>
            <span className={styles.lineKey} style={{background: `var(--chart-s${s.color})`}} aria-hidden="true" />
            {s.name}
          </li>
        ))}
      </ul>
      <div
        ref={ref}
        className={styles.plot}
        onMouseMove={at}
        onTouchStart={at}
        onTouchMove={at}
        onMouseLeave={() => setHover(null)}
        tabIndex={0}
        onKeyDown={(ev) => {
          if (ev.key === 'ArrowRight') setHover((v) => Math.min(levels.length - 1, v == null ? 0 : v + 1));
          if (ev.key === 'ArrowLeft') setHover((v) => Math.max(0, v == null ? 0 : v - 1));
          if (ev.key === 'Escape') setHover(null);
        }}
        aria-label={`${title || 'Line chart'}: use the arrow keys to read each level`}>
        <svg width={width} height={h} role="img" aria-label={title}>
          {tk.map((t) => (
            <g key={t}>
              <line x1={left} x2={width - right} y1={y(t)} y2={y(t)} className={styles.grid} />
              <text x={left - 6} y={y(t)} dy="0.35em" textAnchor="end" className={styles.axis}>{fmt(t)}</text>
            </g>
          ))}
          {levels.map((l, i) => (
            <text key={l} x={x(i)} y={h - bottom + 18} textAnchor="middle" className={styles.axis}>
              {l.toLocaleString('en-US')}
            </text>
          ))}
          <text x={(left + width - right) / 2} y={h - 4} textAnchor="middle" className={styles.axis}>{xLabel}</text>
          {hover != null && <line x1={x(hover)} x2={x(hover)} y1={top} y2={h - bottom} className={styles.crosshair} />}
          {series.map((s) => {
            const pts = s.values.map((v, i) => (v == null ? null : [x(i), y(v)]));
            const d = pts.reduce((acc, p, i) => (p ? acc + `${acc && pts[i - 1] ? 'L' : 'M'}${p[0]},${p[1]}` : acc), '');
            return (
              <g key={s.name}>
                <path d={d} fill="none" stroke={`var(--chart-s${s.color})`} strokeWidth={s.highlight ? 3 : 2} strokeLinejoin="round" strokeLinecap="round" />
                {hover != null && pts[hover] && (
                  <circle cx={pts[hover][0]} cy={pts[hover][1]} r={4.5} fill={`var(--chart-s${s.color})`} stroke="var(--chart-surface)" strokeWidth={2} />
                )}
              </g>
            );
          })}
        </svg>
        <Tooltip tip={tip} docked={width < 560} />
      </div>
      {caption && <p className={styles.caption}>{caption}</p>}
    </figure>
  );
}
