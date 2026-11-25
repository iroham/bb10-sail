// Sail: render the interface icons from Tabler Icons (MIT, https://tabler.io/icons) to PNG.
// Usage: node tools/icons.js <dir containing node_modules with @tabler/icons and @resvg/resvg-js>
// Output: assets/icons/<name>.png, 96 px, one stroke weight for the whole set.
const path = require('path'), fs = require('fs');
const nm = path.join(process.argv[2] || '.', 'node_modules');
const { Resvg } = require(path.join(nm, '@resvg/resvg-js'));
const INK = '#ECEEF1', AMBER = '#F2B04A';
const set = {                      // our name: [tabler name, colour]
  back: ['arrow-left', INK], forward: ['arrow-right', INK], reload: ['refresh', INK], stop: ['x', INK],
  menu: ['dots-vertical', INK], lock: ['lock', INK], warn: ['alert-triangle', AMBER], search: ['search', INK],
  home: ['home', INK], copy: ['copy', INK], share: ['share-2', INK], offline: ['wifi-off', INK],
  alert: ['alert-triangle', INK], shield: ['shield-lock', INK],
  tabs: ['square', INK], plus: ['plus', INK], star: ['star', INK], unstar: ['star-off', INK],
  history: ['history', INK], download: ['download', INK], settings: ['settings', INK], paste: ['clipboard-text', INK], link: ['link', INK], bookmarks: ['bookmarks', INK], trash: ['trash', INK], world: ['world', INK],
};
const out = path.join(__dirname, '..', 'assets', 'icons');
fs.mkdirSync(out, { recursive: true });
for (const [name, [src, colour]] of Object.entries(set)) {
  let svg = fs.readFileSync(path.join(nm, '@tabler/icons/icons/outline', src + '.svg'), 'utf8');
  svg = svg.replace(/stroke="currentColor"/g, `stroke="${colour}"`).replace(/stroke-width="2"/g, 'stroke-width="1.75"');
  const png = new Resvg(svg, { fitTo: { mode: 'width', value: 96 } }).render().asPng();
  fs.writeFileSync(path.join(out, name + '.png'), png);
}
console.log(Object.keys(set).length, 'icons ->', out);
