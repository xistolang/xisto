// @ts-check
/** @type {import('@docusaurus/plugin-content-docs').SidebarsConfig} */
const sidebars = {
  docs: [
    'intro',
    {type: 'category', label: 'Getting started', collapsed: false, items: ['getting-started/install', 'getting-started/first-program', 'getting-started/nestjs']},
    {type: 'category', label: 'Guides', items: ['guides/projects-and-packages', 'guides/porting', 'guides/node-compatibility']},
    'language',
    {type: 'category', label: 'Reference', items: ['reference/cli', 'reference/project-files']},
    'contributing',
  ],
};
export default sidebars;
