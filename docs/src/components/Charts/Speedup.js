// Speedup and memory charts for the Benchmarks Game and PolyBench pages, drawn from the pages' own tables
// (docs/scripts/tablecharts.py writes docs/src/data/tablecharts.json; run it after editing a table).
import React from 'react';
import data from '@site/src/data/tablecharts.json';
import {BarChart} from './index';

const sec = (v) => (v < 1 ? `${Math.round(v * 1000)} ms` : `${v.toFixed(2)} s`);
const mb = (v) => (v == null ? '—' : `${v >= 100 ? Math.round(v) : v.toFixed(1)} MB`);
const times = (v) => (v >= 10 ? `${v.toFixed(0)}×` : `${v.toFixed(2)}×`);

function rowsOf(page, section) {
  const p = data[page];
  if (!p) return [null, []];
  const key = Object.keys(p.sections).find((k) => k.startsWith(section));
  return [p, key ? p.sections[key] : []];
}

// how many times faster Xisto is than the other side on each problem of one machine (other's time ÷ Xisto's)
export function SpeedupBars({page, section, unit = 'problem'}) {
  const [p, rows] = rowsOf(page, section);
  if (!rows.length) return null;
  const vals = rows.map((r) => r.o / r.x);
  const log = Math.max(...vals) / Math.max(1e-9, Math.min(...vals)) > 12;
  const wins = vals.filter((v) => v > 1.03).length;
  return (
    <BarChart
      title={`${section}: ${p.other}'s time ÷ Xisto's, per ${unit} (above 1× Xisto is faster)`}
      unit={`times faster than ${p.other}`}
      log={log}
      refLine={log ? null : 1}
      format={times}
      formatFull={times}
      rows={rows.map((r) => ({
        name: r.name,
        value: r.o / r.x,
        highlight: r.o / r.x > 1.03,
        detail: [
          ['Xisto', sec(r.x)],
          [p.other, sec(r.o)],
          ...(r.xm != null ? [['Xisto memory', mb(r.xm)], [`${p.other} memory`, mb(r.om)]] : []),
        ],
      }))}
      caption={`${wins} of ${rows.length} ${unit === 'kernel' ? 'kernels' : 'problems'} more than 3% faster in Xisto (accent); grey within 3% or slower.${log ? ' Log scale: each step is ten times the one before.' : ' The dashed line is 1×, the same time.'} Every value is in the table below.`}
    />
  );
}

// peak memory: the other side's ÷ Xisto's, per problem (above 1× Xisto uses less)
export function MemoryBars({page, section, unit = 'problem'}) {
  const [p, rows] = rowsOf(page, section);
  const rs = rows.filter((r) => r.xm && r.om);
  if (!rs.length) return null;
  const vals = rs.map((r) => r.om / r.xm);
  const log = Math.max(...vals) / Math.max(1e-9, Math.min(...vals)) > 12;
  return (
    <BarChart
      title={`${section}: ${p.other}'s peak memory ÷ Xisto's, per ${unit} (above 1× Xisto uses less)`}
      unit={`times less memory than ${p.other}`}
      log={log}
      refLine={log ? null : 1}
      format={times}
      formatFull={times}
      rows={rs.map((r) => ({
        name: r.name,
        value: r.om / r.xm,
        highlight: r.om / r.xm > 1.03,
        detail: [
          ['Xisto', mb(r.xm)],
          [p.other, mb(r.om)],
        ],
      }))}
      caption={`Peak RSS of each run.${log ? ' Log scale.' : ' The dashed line is 1×, the same memory.'}`}
    />
  );
}
