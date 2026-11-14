// The TechEmpower page's charts, drawn from docs/src/data/techempower.json (bench/techempower/chartdata.py).
import React from 'react';
import data from '@site/src/data/techempower.json';
import {BarChart, LineChart} from './index';

const mb = (v) => `${v.toFixed(1)} MB`;

export function BestBars({test}) {
  const t = data[test];
  const rows = t.servers.map((s) => ({
    name: s.name,
    value: s.best,
    highlight: s.id.startsWith('xisto'),
    detail: [
      ['connections at best', s.bestLevel.toLocaleString('en-US')],
      ['ms p99 there', s.p99AtBest == null ? '—' : s.p99AtBest.toFixed(1)],
      ['ms CPU per 1,000 requests', s.cpuAtBest.toFixed(1)],
      ['peak RSS', mb(s.peakRss)],
    ],
  }));
  const crashed = t.crashed.length ? ` ${t.crashed.join(', ')} crashed under load and has no bar.` : '';
  return (
    <BarChart
      title={`${test}: best requests per second of each server`}
      rows={rows}
      caption={`The best of the levels, median of 3 runs at each level; Xisto's servers in the accent colour.${crashed} Every value is in the table below.`}
    />
  );
}

export function LevelLines({test}) {
  const t = data[test];
  let slot = 0;
  const series = t.lines
    .map((id) => t.servers.find((s) => s.id === id))
    .filter(Boolean)
    .map((s) => ({name: s.name, values: s.values, color: ++slot, highlight: s.id === 'xisto-native'}));
  return (
    <LineChart
      title={`${test}: requests per second by number of connections`}
      levels={t.levels}
      series={series}
      caption="Six of the servers, to keep the lines readable; all of them are in the table below."
    />
  );
}
