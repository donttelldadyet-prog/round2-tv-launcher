/* Builds the live preview: play.jsx + r2-ui.jsx + React → play.bundle.js (one file, works with no internet).
   Run from the repo: node manager/ui/build.cjs      (needs esbuild + react 18/19 + react-dom installed: npm i -D esbuild react react-dom) */
const path = require('path');
let esbuild; for (const p of [undefined, '/opt/npm-tools/node_modules']) { try { esbuild = require(p ? require.resolve('esbuild', { paths: [p] }) : 'esbuild'); break; } catch (e) {} }
if (!esbuild) { console.error('esbuild not found – npm i -D esbuild react react-dom'); process.exit(1); }
esbuild.build({
  entryPoints: [path.join(__dirname, 'play.jsx')], outfile: path.join(__dirname, 'play.bundle.js'),
  bundle: true, minify: true, format: 'iife', target: ['safari15', 'chrome100'], jsx: 'transform',
  loader: { '.js': 'jsx' }, define: { 'process.env.NODE_ENV': '"production"' },
  nodePaths: ['/opt/npm-tools/node_modules'], legalComments: 'none',
  banner: { js: '/* Round2 UI live preview – built from play.jsx + r2-ui.jsx by build.cjs; includes React (MIT, Meta Platforms) */' },
}).then(() => console.log('built play.bundle.js')).catch(() => process.exit(1));
