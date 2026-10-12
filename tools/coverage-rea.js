// Mechanical, reproducible ledger overlay from retained REA call-graph evidence.
// Does NOT classify unnamed routines as game/CRT code or claim source equivalence.
// Usage: node tools/coverage-rea.js <bundle> <inventory> <exports.csv> <out.csv>
const fs = require('fs');
const path = require('path');
const [bundlePath, inventoryPath, exportsPath, outputPath] = process.argv.slice(2);
if (!outputPath) throw new Error('Expected bundle, inventory, exports and output paths');
const sha = 'cacba71ac2ae6b8676f7628322d609cc704d185665e92a82fe74d4d34d3daba6';
const bundle = JSON.parse(fs.readFileSync(bundlePath, 'utf8'));
const inventory = JSON.parse(fs.readFileSync(inventoryPath, 'utf8'));
const canonical = a => a.startsWith('EXTERNAL:') ? a : '0x' + a.replace(/^0x/i, '').toLowerCase();
const rows = fs.readFileSync(exportsPath, 'utf8').trim().split(/\r?\n/).slice(1).map(line => {
  const fields = [...line.matchAll(/"((?:[^"]|"")*)"/g)].map(m => m[1].replace(/""/g, '"'));
  if (fields.length !== 4) throw new Error('Unexpected export CSV schema');
  return { address: canonical(fields[0]), name: fields[1], group: fields[2] };
}).filter(row => row.group !== 'entry');
const records = bundle.records.filter(r => r.operation === 'get_call_graph');
const nodes = new Map(), examined = new Set(), evidenceByNode = new Map();
const queried = new Set();
for (const record of records) {
  if (record.subject?.digest?.sha256?.toLowerCase() !== sha) throw new Error('Wrong artifact in graph evidence');
  queried.add(canonical(record.parameters.address));
  for (const [depth, items] of Object.entries(record.normalized_result)) for (const node of items) {
    const address = canonical(node.address);
    if (node.status !== 'ok') throw new Error('Incomplete REA graph node: ' + address);
    // get_call_graph depth4 is a boundary, not proof that a procedure has no callees.
    if (Number(depth) < 4) {
      examined.add(address);
      if (!nodes.has(address)) nodes.set(address, new Set());
      for (const child of node.calls) nodes.get(address).add(canonical(child));
    } else if (!nodes.has(address)) nodes.set(address, new Set());
    if (!evidenceByNode.has(address)) evidenceByNode.set(address, record.evidence_id);
  }
}
for (const row of rows) if (!queried.has(row.address)) throw new Error('Missing export root graph: ' + row.name);
const unresolvedBoundary = [...nodes.keys()].filter(a => !a.startsWith('EXTERNAL:') && !examined.has(a));
if (unresolvedBoundary.length) throw new Error('Unexpanded native graph boundary: ' + unresolvedBoundary.length);
const groups = new Map();
for (const row of rows) {
  const seen = new Set(), pending = [row.address];
  while (pending.length) {
    const address = pending.pop();
    if (seen.has(address)) continue;
    seen.add(address);
    if (!groups.has(address)) groups.set(address, new Set());
    groups.get(address).add(row.group);
    for (const child of nodes.get(address) || []) pending.push(child);
  }
}
const source = new Map();
for (const file of fs.readdirSync('src')) {
  if (!/\.(cpp|h|c)$/.test(file)) continue;
  const lines = fs.readFileSync(path.join('src', file), 'utf8').split(/\r?\n/);
  lines.forEach((line, i) => {
    for (const m of line.matchAll(/(?:FUN_|0x)(1[0-9a-f]{8})\b/gi)) {
      const address = canonical(m[1]);
      if (!source.has(address)) source.set(address, new Set());
      source.get(address).add('src/' + file + ':' + (i + 1));
    }
  });
}
const csv = value => '"' + String(value).replace(/"/g, '""') + '"';
const header = ['address', 'name', 'static_export_groups', 'reachability', 'source_address_references',
  'verification', 'graph_evidence'];
const output = [header.map(csv).join(',')];
let reachable = 0, referenced = 0;
for (const item of inventory) {
  const address = canonical(item.a);
  const membership = [...(groups.get(address) || [])].sort();
  const refs = [...(source.get(address) || [])].sort();
  if (membership.length) reachable++;
  if (refs.length) referenced++;
  output.push([address, item.name, membership.join(';'), membership.length ? 'static-reached' : 'not-static-reached',
    refs.join(';'), refs.length ? 'referenced-not-proven' : 'unverified/no-source-address-reference',
    evidenceByNode.get(address) || ''].map(csv).join(','));
}
// Generated report, never a semantic "covered" assertion. Imported/CRT ownership
// still needs symbol/code evidence; export-group reachability alone cannot prove it.
fs.writeFileSync(outputPath, output.join('\n') + '\n');
console.log(JSON.stringify({artifact_sha256: sha, export_rows: rows.length, unique_export_roots: new Set(rows.map(r => r.address)).size,
  graph_queries: records.length, observed_nodes: nodes.size, native_nodes: [...nodes.keys()].filter(a => !a.startsWith('EXTERNAL:')).length,
  external_nodes: [...nodes.keys()].filter(a => a.startsWith('EXTERNAL:')).length, inventory: inventory.length,
  static_reached_inventory: reachable, source_referenced_inventory: referenced, unresolved_native_boundary: unresolvedBoundary.length}));
