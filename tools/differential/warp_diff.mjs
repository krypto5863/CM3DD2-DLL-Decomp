import { readdirSync, statSync, readFileSync, openSync, readSync, closeSync, mkdirSync, copyFileSync, rmSync, existsSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import path from "node:path";
const roots = [["G", "G:/KISS/COM3D2/GameData"], ["E", "E:/COM3D2Test 2.5/COM3D2_5/GameData"]];
const maxPairs = Number(process.argv[2] || 40), maxBytes = Number(process.argv[3] || 150e6);
const pairs = [];
for (const [tag, d] of roots) {
  const all = new Map(readdirSync(d).map(f => [f.toLowerCase(), f]));
  for (const [l, f] of all) {
    if (!l.endsWith("_2.arc")) continue;
    const p = path.join(d, f), fd = openSync(p, "r"), b = Buffer.alloc(4); readSync(fd, b, 0, 4, 0); closeSync(fd);
    if (b.toString("latin1") !== "warp") continue;
    const base = all.get(l.replace(/_2\.arc$/, ".arc")); if (!base) continue;
    pairs.push({ tag, patch: p, base: path.join(d, base), size: statSync(p).size + statSync(path.join(d, base)).size });
  }
}
pairs.sort((a, b) => a.size - b.size);
const pick = pairs.filter(p => p.size < maxBytes).slice(0, maxPairs);
// plus a spread of larger ones
const big = pairs.filter(p => p.size >= maxBytes && p.size < 800e6).filter((_, i) => i % 4 === 0).slice(0, 4);
const md5 = f => createHash("md5").update(readFileSync(f)).digest("hex");
let bad = 0, n = 0;
for (const p of [...pick, ...big]) {
  const res = {};
  for (const dll of ["orig_copy", "mine"]) {
    const dir = `wd_${dll}`; rmSync(dir, { recursive: true, force: true }); mkdirSync(dir);
    const bp = path.join(dir, path.basename(p.base)), pp = path.join(dir, path.basename(p.patch));
    copyFileSync(p.base, bp); copyFileSync(p.patch, pp);
    const out = execFileSync("./warp_probe.exe", [`./${dll}.dll`, path.resolve(bp), path.resolve(pp)], { encoding: "utf8", timeout: 600000 });
    res[dll] = out.replace(/\s+/g, " ") + " | " + md5(pp) + " " + statSync(pp).size + " | " + md5(bp);
  }
  n++;
  const same = res.orig_copy === res.mine;
  if (!same) { bad++; console.log("DIFF", p.tag, path.basename(p.patch), "\n  orig:", res.orig_copy, "\n  mine:", res.mine); }
  else console.log("same", p.tag, path.basename(p.patch), p.size, res.mine.slice(0, 40));
}
console.log({ pairs: n, bad });
rmSync("wd_orig_copy", { recursive: true, force: true }); rmSync("wd_mine", { recursive: true, force: true });
