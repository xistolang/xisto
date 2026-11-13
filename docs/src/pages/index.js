import Link from '@docusaurus/Link';
import useBaseUrl from '@docusaurus/useBaseUrl';
import Layout from '@theme/Layout';
import ThemedImage from '@theme/ThemedImage';
import styles from './index.module.css';

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
      <main className={styles.strip}>
        <div className={styles.grid}>
          {points.map((p) => (
            <section key={p.title} className={styles.card}>
              <h2 className={styles.cardTitle}>{p.title}</h2>
              <p className={styles.cardText}>{p.text}</p>
            </section>
          ))}
        </div>
      </main>
    </Layout>
  );
}
