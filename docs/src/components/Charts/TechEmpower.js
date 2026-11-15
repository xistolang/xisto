// The TechEmpower pages' charts and tables, drawn from docs/src/data/techempower.json (bench/techempower/chartdata.py).
import React from 'react';
import data from '@site/src/data/techempower.json';
import {BarChart, LineChart} from './index';

const mb = (v) => `${v.toFixed(1)} MB`;
const ms = (v) => (v == null ? '—' : v >= 100 ? `${Math.round(v)} ms` : `${v.toFixed(1)} ms`);
const int = (v) => (v == null ? '—' : Math.round(v).toLocaleString('en-US'));
const XISTO = ['xisto-native', 'xisto-nest-procs', 'xisto-nest'];
// one colour per server on every chart of every page (the six validated categorical slots); the servers without a
// slot (Nest with the lock, fastify) are in the bar charts and the tables
const SLOT = {'xisto-native': 1, 'xisto-nest-procs': 2, actix: 3, bun: 4, 'go-std': 5, nodejs: 6};

const pick = (test, only) => {
  const t = data[test];
  return only ? only.map((id) => t.servers.find((s) => s.id === id)).filter(Boolean) : t.servers;
};
const crashedNote = (test) => {
  const c = data[test].crashed;
  return c.length ? ` ${c.join(', ')} crashed under load and is not drawn.` : '';
};

export function BestBars({test, only}) {
  const servers = pick(test, only);
  const rows = servers.map((s) => ({
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
  return (
    <BarChart
      title={`${test}: best requests per second (higher is better)`}
      rows={rows}
      caption={`The best of the levels, median of 3 runs at each level; Xisto's servers in the accent colour.${only ? '' : crashedNote(test)} Every value is in the tables below.`}
    />
  );
}

// requests per second (or p99) at each number of connections
export function LevelLines({test, only, metric = 'values'}) {
  const t = data[test];
  const ids = only || t.lines;
  const series = ids
    .map((id) => t.servers.find((s) => s.id === id))
    .filter((s) => s && SLOT[s.id])
    .map((s) => ({name: s.name, values: s[metric], color: SLOT[s.id], highlight: s.id === 'xisto-native'}))
    .filter((s) => s.values.some((v) => v != null));
  if (metric === 'p99') {
    return (
      <LineChart
        title={`${test}: p99 latency by number of connections (lower is better, log scale)`}
        levels={t.levels}
        series={series}
        log
        lowerIsBetter
        format={(v) => (v >= 1000 ? `${v / 1000} s` : `${v} ms`)}
        formatFull={ms}
        caption="The 99th percentile of each level's median run, as wrk reports it; each step of the axis is ten times the one below. A level without a value had no p99 in wrk's output."
      />
    );
  }
  return (
    <LineChart
      title={`${test}: requests per second by number of connections (higher is better)`}
      levels={t.levels}
      series={series}
      caption={
        only
          ? 'Every level of the round; the values are in the table below.'
          : 'Six of the servers, to keep the lines readable; all of them are in the tables below.'
      }
    />
  );
}

// CPU per 1,000 requests at each server's best level, or the peak memory over every level
export function CostBars({test, only, metric}) {
  const servers = pick(test, only);
  const cpu = metric === 'cpu';
  const rows = servers.map((s) => ({
    name: s.name,
    value: cpu ? s.cpuAtBest : s.peakRss,
    highlight: s.id.startsWith('xisto'),
    detail: cpu
      ? [['requests per second there', int(s.best)], ['connections', s.bestLevel.toLocaleString('en-US')]]
      : [['best requests per second', int(s.best)]],
  }));
  return (
    <BarChart
      title={
        cpu
          ? `${test}: CPU time per 1,000 requests, in ms (lower is better)`
          : `${test}: peak memory of all the server's processes, in MB (lower is better)`
      }
      unit={cpu ? 'ms of CPU per 1,000 requests' : 'MB peak RSS'}
      format={(v) => v.toFixed(1)}
      formatFull={(v) => (cpu ? `${v.toFixed(1)} ms` : mb(v))}
      lowerIsBetter
      rows={rows}
      caption={
        cpu
          ? "The server's user + system time over the run at its best level, divided by the requests it answered."
          : 'The largest resident set size summed over all the server processes, across every level.'
      }
    />
  );
}

// how many times the reference's requests per second each Xisto server answers, level by level
export function RatioBars({test, against}) {
  const t = data[test];
  const r = t.servers.find((s) => s.id === against);
  const x = t.servers.find((s) => s.id === 'xisto-native');
  if (!r || !x) return null;
  const rows = t.levels
    .map((l, i) => ({l, xv: x.values[i], rv: r.values[i]}))
    .filter((v) => v.xv != null && v.rv != null)
    .map((v) => ({
      name: `${v.l.toLocaleString('en-US')} connections`,
      value: v.xv / v.rv,
      highlight: v.xv >= v.rv,
      detail: [
        [`${x.name} req/s`, int(v.xv)],
        [`${r.name} req/s`, int(v.rv)],
      ],
    }));
  return (
    <BarChart
      title={`${test}: ${x.name} requests per second ÷ ${r.name}'s, at each level (above 1× Xisto answers more)`}
      unit="times"
      format={(v) => `${v.toFixed(2)}×`}
      formatFull={(v) => `${v.toFixed(2)}×`}
      keepOrder
      rows={rows}
      caption={`Accent: the levels where Xisto answers more requests; grey: where ${r.name} does.`}
    />
  );
}

// the head-to-head table under the charts: every level, the reference against each Xisto server
export function VsTable({test, against}) {
  const t = data[test];
  const ids = [...XISTO, ...(Array.isArray(against) ? against : [against])];
  const servers = ids.map((id) => t.servers.find((s) => s.id === id)).filter(Boolean);
  const crashed = t.crashed;
  return (
    <>
      <table>
        <thead>
          <tr>
            <th>server</th>
            {t.levels.map((l) => (
              <th key={l} style={{textAlign: 'right'}}>{l.toLocaleString('en-US')}</th>
            ))}
          </tr>
        </thead>
        <tbody>
          {servers.map((s) => (
            <tr key={s.id}>
              <td>{s.id.startsWith('xisto') ? <strong>{s.name}</strong> : s.name}</td>
              {s.values.map((v, i) => (
                <td key={i} style={{textAlign: 'right'}}>{int(v)}</td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
      <p>
        Requests per second at each number of connections{test === 'plaintext' ? ' (HTTP pipelining 16 deep)' : ''}.
        {crashed.length ? ` ${crashed.join(', ')} crashed under load.` : ''}
      </p>
      <table>
        <thead>
          <tr>
            <th>server</th>
            <th style={{textAlign: 'right'}}>best req/s</th>
            <th style={{textAlign: 'right'}}>connections there</th>
            <th style={{textAlign: 'right'}}>p50 there</th>
            <th style={{textAlign: 'right'}}>p99 there</th>
            <th style={{textAlign: 'right'}}>CPU ms per 1k requests</th>
            <th style={{textAlign: 'right'}}>peak RSS</th>
          </tr>
        </thead>
        <tbody>
          {servers.map((s) => {
            const i = t.levels.indexOf(s.bestLevel);
            return (
              <tr key={s.id}>
                <td>{s.id.startsWith('xisto') ? <strong>{s.name}</strong> : s.name}</td>
                <td style={{textAlign: 'right'}}>{int(s.best)}</td>
                <td style={{textAlign: 'right'}}>{s.bestLevel.toLocaleString('en-US')}</td>
                <td style={{textAlign: 'right'}}>{ms(s.p50 ? s.p50[i] : null)}</td>
                <td style={{textAlign: 'right'}}>{ms(s.p99AtBest)}</td>
                <td style={{textAlign: 'right'}}>{s.cpuAtBest.toFixed(1)}</td>
                <td style={{textAlign: 'right'}}>{mb(s.peakRss)}</td>
              </tr>
            );
          })}
        </tbody>
      </table>
      <p>p99 at each level:</p>
      <table>
        <thead>
          <tr>
            <th>server</th>
            {t.levels.map((l) => (
              <th key={l} style={{textAlign: 'right'}}>{l.toLocaleString('en-US')}</th>
            ))}
          </tr>
        </thead>
        <tbody>
          {servers.map((s) => (
            <tr key={s.id}>
              <td>{s.id.startsWith('xisto') ? <strong>{s.name}</strong> : s.name}</td>
              {(s.p99 || []).map((v, i) => (
                <td key={i} style={{textAlign: 'right'}}>{ms(v)}</td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
    </>
  );
}

// a short summary for the top of a subpage, computed from the data so it never drifts from the charts
export function VsHeadline({against}) {
  const line = (test) => {
    const t = data[test];
    const x = t.servers.find((s) => s.id === 'xisto-native');
    const r = t.servers.find((s) => s.id === against);
    if (!x || !r) return null;
    const ratio = x.best / r.best;
    const cpu = r.cpuAtBest / x.cpuAtBest;
    const mem = r.peakRss / x.peakRss;
    return (
      <li key={test}>
        <strong>{test}</strong>: {int(x.best)} against {int(r.best)} requests per second at best (
        {ratio >= 1 ? `${ratio.toFixed(2)}× more` : `${(1 / ratio).toFixed(2)}× fewer`}), with{' '}
        {cpu >= 1 ? `${cpu.toFixed(1)}× less` : `${(1 / cpu).toFixed(1)}× more`} CPU per request and{' '}
        {mem >= 1 ? `${mem.toFixed(1)}× less` : `${(1 / mem).toFixed(1)}× more`} memory ({mb(x.peakRss)} against{' '}
        {mb(r.peakRss)}).
      </li>
    );
  };
  return <ul>{['json', 'plaintext'].map(line)}</ul>;
}
