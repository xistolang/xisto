import Link from '@docusaurus/Link';
import useBaseUrl from '@docusaurus/useBaseUrl';
import Layout from '@theme/Layout';
import ThemedImage from '@theme/ThemedImage';
import CodeBlock from '@theme/CodeBlock';
import styles from './index.module.css';

const results = [
  {
    value: '0 losses',
    label: 'against Rust',
    text: 'Benchmarks Game, 10 problems on a Xeon and a Graviton2: 9 wins and 1 tie on each, with peak memory at or below Rust\'s.',
    to: '/docs/benchmarks/benchmarks-game/vs-rust',
  },
  {
    value: '21 ms',
    label: 'NestJS startup',
    text: 'A real NestJS app (Conduit) starts in 21 ms, against 268 ms on Node.',
    to: '/docs/benchmarks/http#conduit-a-nestjs-app-8-nov-2026',
  },
  {
    value: '10 / 10',
    label: 'against Node and Bun',
    text: 'Every Benchmarks Game problem on an Apple M4, faster and in less memory than Node 26 and Bun 1.4.',
    to: '/docs/benchmarks/benchmarks-game/vs-node',
  },
  {
    value: '1.3 MB',
    label: 'peak memory',
    text: 'A small program (fib) peaks at 1.3 MB; the same file on Node takes 66.1 MB.',
    to: '/docs/#three-numbers',
  },
];

const example = `interface Order {
  id: number;
  customer: string;
  total: number;
}

const orders: Order[] = [];
for (let i = 0; i < 1_000_000; i++) {
  orders.push({ id: i, customer: \`c\${i % 1000}\`, total: (i % 97) * 1.5 });
}

const big = orders.filter((o) => o.total > 100);
const revenue = big.reduce((sum, o) => sum + o.total, 0);
console.log(\`\${big.length} orders over 100, revenue \${revenue}\`);`;

const paths = [
  {
    title: 'Build a NestJS API',
    text: 'Create a project, add the ported NestJS packages and serve your first route, step by step.',
    to: '/docs/getting-started/nestjs',
    cta: 'Follow the tutorial',
  },
  {
    title: 'Measure your project',
    text: 'xisto check reads your tsconfig and counts what has to change before your code compiles, by kind of change.',
    to: '/docs/guides/porting#1-measure-with-xisto-check',
    cta: 'Run xisto check',
  },
  {
    title: 'Port it with an AI agent',
    text: 'The xisto-port skill gives a coding agent the method: measure, fix in the right order, compare with Node.',
    to: '/docs/guides/porting#porting-with-an-ai-agent-the-xisto-port-skill',
    cta: 'Get the skill',
  },
];

const points = [
  {
    title: 'Native executables',
    text: 'Your .ts files are compiled ahead of time into one executable that needs no runtime installed to run.',
  },
  {
    title: 'Low memory, fast startup',
    text: 'No engine to load and no JIT to warm up: the program starts doing its work at once and keeps only its own data in memory.',
  },
  {
    title: 'Runs your TypeScript',
    text: 'Classes, generics, async/await, decorators and modules, under a few stricter rules. The same source still runs on Node.',
  },
];

export default function Home() {
  return (
    <Layout
      title="Familiar syntax. Native performance."
      description="Xisto: a strict subset of TypeScript, compiled to small native executables.">
      <header className={styles.hero}>
        <div className={styles.heroInner}>
          <ThemedImage
            className={styles.heroLogo}
            alt="Xisto"
            sources={{light: useBaseUrl('/img/logo-full.svg'), dark: useBaseUrl('/img/logo-full-dark.svg')}}
          />
          <h1 className={styles.title}>Familiar syntax. Native performance.</h1>
          <p className={styles.subtitle}>
            A strict subset of TypeScript, compiled to small native executables.
          </p>
          <div className={styles.terminal} role="img" aria-label="Terminal: xisto build server.ts, then ./server">
            <div className={styles.terminalBar} aria-hidden="true">
              <span /><span /><span />
            </div>
            <pre className={styles.terminalBody}>
              <code>
                <span className={styles.prompt}>$</span> xisto build server.ts{'\n'}
                <span className={styles.prompt}>$</span> ./server
              </code>
            </pre>
          </div>
          <div className={styles.buttons}>
            <Link className="button button--primary button--lg" to="/docs/getting-started/install">
              Get started
            </Link>
            <Link className="button button--secondary button--lg" to="/docs/benchmarks">
              View benchmarks
            </Link>
          </div>
          <p className={styles.manifesto}>Software that respects the hardware it runs on.</p>
        </div>
      </header>
      <main>
        <section className={styles.results} aria-labelledby="results-title">
          <div className={styles.section}>
            <h2 id="results-title" className={styles.sectionTitle}>Measured, with the method and the code</h2>
            <div className={styles.statGrid}>
              {results.map((r) => (
                <Link key={r.label} to={r.to} className={styles.stat}>
                  <span className={styles.statValue}>{r.value}</span>
                  <span className={styles.statLabel}>{r.label}</span>
                  <span className={styles.statText}>{r.text}</span>
                </Link>
              ))}
            </div>
            <p className={styles.note}>
              Results of specific tests on stated machines (13 Nov 2026), not a promise for every program. Every number
              links to its table, its raw result file and the <Link to="/docs/performance/methodology">methodology</Link>;
              the cases where Xisto ties or loses are shown there too.
            </p>
          </div>
        </section>

        <section className={styles.codeSection} aria-labelledby="code-title">
          <div className={styles.section}>
            <h2 id="code-title" className={styles.sectionTitle}>The TypeScript you already write</h2>
            <div className={styles.codeGrid}>
              <div className={styles.codePane}>
                <CodeBlock language="ts" title="orders.ts">{example}</CodeBlock>
              </div>
              <div className={styles.codePane}>
                <div className={styles.terminal} role="img" aria-label="Terminal: xisto build orders.ts, then ./orders">
                  <div className={styles.terminalBar} aria-hidden="true"><span /><span /><span /></div>
                  <pre className={styles.terminalBody}>
                    <code>
                      <span className={styles.prompt}>$</span> xisto build orders.ts{'\n'}
                      <span className={styles.prompt}>$</span> ls -lh orders{'\n'}
                      <span className={styles.dim}>-rwxr-xr-x  69K  orders</span>{'\n'}
                      <span className={styles.prompt}>$</span> ./orders{'\n'}
                      309270 orders over 100, revenue 37808257.5
                    </code>
                  </pre>
                </div>
                <dl className={styles.facts}>
                  <div><dt>Xisto</dt><dd>17 ms, 76 MB peak</dd></div>
                  <div><dt>The same file on Node 26</dt><dd>116 ms, 208 MB peak</dd></div>
                </dl>
                <p className={styles.factsNote}>
                  Apple M4, xisto 0.3.0, median of 11 runs. Same output on both, byte for byte.
                </p>
              </div>
            </div>
          </div>
        </section>

        <section className={styles.paths} aria-labelledby="paths-title">
          <div className={styles.section}>
            <h2 id="paths-title" className={styles.sectionTitle}>Start with your own code</h2>
            <div className={styles.grid}>
              {paths.map((p) => (
                <Link key={p.title} to={p.to} className={`${styles.card} ${styles.cardLink}`}>
                  <h3 className={styles.cardTitle}>{p.title}</h3>
                  <p className={styles.cardText}>{p.text}</p>
                  <span className={styles.cardCta}>{p.cta} →</span>
                </Link>
              ))}
            </div>
          </div>
        </section>

        <section className={styles.strip} aria-label="What you get">
          <div className={styles.grid}>
            {points.map((p) => (
              <section key={p.title} className={styles.card}>
                <h3 className={styles.cardTitle}>{p.title}</h3>
                <p className={styles.cardText}>{p.text}</p>
              </section>
            ))}
          </div>
        </section>
      </main>
    </Layout>
  );
}
