import { Warc } from "./warc_native.mjs";
import { writeFileSync, mkdirSync } from "node:fs";
// Writes every .menu member of a warc archive (original-case names from tag 3) into a folder.
const [, , arc, out, limit = "100000"] = process.argv;
const w = new Warc(arc);
const names = new Map(w.names().map(n => [n.key, n.name]));
const sec = w.sections.get(0);
const p = w.read(sec.payloadAt, sec.len);
mkdirSync(out, { recursive: true });
let n = 0;
function walk(t) {
  const h = p.readUInt32LE(t), dirs = p.readUInt32LE(t + 16), files = p.readUInt32LE(t + 20);
  for (let i = 0; i < files && n < Number(limit); i++) {
    const rec = t + h + (dirs + i) * 16;
    const key = p.readBigUInt64LE(rec), value = Number(p.readBigUInt64LE(rec + 8));
    const name = names.get(key);
    if (name && name.endsWith(".menu")) { writeFileSync(out + "/" + name, w.member(value)); n++; }
  }
  for (let i = 0; i < dirs; i++) walk(t + Number(p.readBigUInt64LE(t + h + i * 16 + 8)));
}
walk(0);
console.log("wrote", n, "menus");
