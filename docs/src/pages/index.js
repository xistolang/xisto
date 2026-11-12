import Link from '@docusaurus/Link';
import useBaseUrl from '@docusaurus/useBaseUrl';
import Layout from '@theme/Layout';
import CodeBlock from '@theme/CodeBlock';

const features = [
  {title: 'Native executables', text: 'xisto compiles typed TypeScript ahead of time, through C, into one executable that needs nothing installed to run.'},
  {title: 'Small and quick to start', text: 'No JavaScript runtime to load: a program starts in milliseconds and uses a fraction of the memory of one.'},
  {title: 'TypeScript as you write it', text: 'Classes, generics, async/await, decorators and Node-style modules, under a few stricter rules that keep the types real.'},
  {title: 'Same output as Node', text: 'The test suite compares every program’s output with Node’s byte for byte; npm packages are ported with the same API.'},
];

export default function Home() {
  return (
    <Layout title="xisto" description="An ahead-of-time compiler for typed TypeScript to native executables">
      <header className="hero hero--xisto">
        <div className="container">
          <img className="hero-logo" src={useBaseUrl('/img/logo.svg')} alt="" />
          <h1 className="hero__title">xisto</h1>
          <p className="hero__subtitle">Software that respects the hardware it runs on.</p>
          <p>An ahead-of-time compiler for typed TypeScript: your <code>.ts</code> files become a small, fast-starting native executable.</p>
          <div className="install">
            <CodeBlock language="sh">brew install xistolang/tap/xisto</CodeBlock>
          </div>
          <div className="buttons">
            <Link className="button button--primary button--lg" to="/docs/getting-started/install">Get started</Link>
            <Link className="button button--secondary button--lg" href="https://github.com/xistolang/xisto">GitHub</Link>
          </div>
        </div>
      </header>
      <main className="container">
        <div className="features">
          {features.map((f) => (<div key={f.title}><h3>{f.title}</h3><p>{f.text}</p></div>))}
        </div>
      </main>
    </Layout>
  );
}
