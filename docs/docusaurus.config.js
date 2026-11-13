// @ts-check
// xisto documentation (Docusaurus). Published to GitHub Pages by .github/workflows/docs.yml;
// each release freezes a version of these docs (docs:version, see the release workflow).
import {themes as prismThemes} from 'prism-react-renderer';
import fs from 'node:fs';

// code blocks: high contrast on the site's surfaces (light: white, dark: #0F1526)
const lightCode = {...prismThemes.github, plain: {...prismThemes.github.plain, color: '#151B2A', backgroundColor: '#FFFFFF'}};
const darkCode = {...prismThemes.oneDark, plain: {color: '#E6EAF2', backgroundColor: '#0F1526'}};

// released versions (newest first), written by `docusaurus docs:version` in the release workflow
const released = fs.existsSync('./versions.json') ? JSON.parse(fs.readFileSync('./versions.json', 'utf8')) : [];

/** @type {import('@docusaurus/types').Config} */
const config = {
  title: 'Xisto',
  tagline: 'Software that respects the hardware it runs on.',
  favicon: 'img/favicon.svg',
  future: {v4: true},
  url: 'https://xisto-lang.org',
  baseUrl: '/',
  organizationName: 'xistolang',
  projectName: 'xisto',
  trailingSlash: false,
  onBrokenLinks: 'throw',
  markdown: {hooks: {onBrokenMarkdownLinks: 'throw'}},
  i18n: {defaultLocale: 'en', locales: ['en']},
  presets: [
    [
      'classic',
      /** @type {import('@docusaurus/preset-classic').Options} */
      ({
        docs: {
          routeBasePath: 'docs',
          sidebarPath: './sidebars.js',
          editUrl: 'https://github.com/xistolang/xisto/tree/main/docs/',
          showLastUpdateTime: false,
          // the latest release is the default; "Next" (main) documents what is not released yet
          lastVersion: released.length ? released[0] : 'current',
          versions: released.length
            ? {current: {label: 'Next (unreleased)', path: 'next', banner: 'unreleased'}}
            : {current: {label: 'Next', banner: 'none'}},
        },
        blog: false,
        theme: {customCss: './src/css/custom.css'},
      }),
    ],
  ],
  themes: [
    [
      '@easyops-cn/docusaurus-search-local',
      /** local search: no external service, nothing about the reader leaves the page */
      ({hashed: true, docsRouteBasePath: 'docs', indexBlog: false, highlightSearchTermsOnTargetPage: true}),
    ],
  ],
  themeConfig:
    /** @type {import('@docusaurus/preset-classic').ThemeConfig} */
    ({
      colorMode: {defaultMode: 'dark', respectPrefersColorScheme: true},
      navbar: {
        title: 'Xisto',
        logo: {alt: 'Xisto', src: 'img/logo.svg', srcDark: 'img/logo.svg', width: 32, height: 32},
        items: [
          {type: 'docSidebar', sidebarId: 'docs', position: 'left', label: 'Docs'},
          {to: '/docs/benchmarks', label: 'Benchmarks', position: 'left'},
          {type: 'docsVersionDropdown', position: 'right'},
          {href: 'https://github.com/xistolang/xisto', label: 'GitHub', position: 'right'},
          {href: 'https://github.com/xistolang/xisto/releases', label: 'Releases', position: 'right'},
        ],
      },
      footer: {
        style: 'light',
        links: [
          {title: 'Docs', items: [{label: 'Getting started', to: '/docs/getting-started/install'}, {label: 'The language', to: '/docs/language'}, {label: 'Benchmarks', to: '/docs/benchmarks'}, {label: 'CLI reference', to: '/docs/reference/cli'}]},
          {title: 'About', items: [{label: 'Why Xisto', to: '/docs/why'}, {label: 'Divergences from TypeScript', to: '/docs/language/divergences'}, {label: 'Contributing', to: '/docs/contributing'}]},
          {title: 'Project', items: [{label: 'GitHub', href: 'https://github.com/xistolang/xisto'}, {label: 'Releases', href: 'https://github.com/xistolang/xisto/releases'}, {label: 'Packages', href: 'https://github.com/xistolang/packages'}, {label: 'Report an issue', href: 'https://github.com/xistolang/xisto/issues'}]},
        ],
        copyright: `Copyright © 2026 The Xisto Authors. Xisto is licensed under Apache-2.0 WITH LLVM-exception.`,
      },
      prism: {theme: lightCode, darkTheme: darkCode, additionalLanguages: ['bash', 'json']},
    }),
};

export default config;
