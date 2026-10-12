import { mkdirSync, copyFileSync, rmSync, readFileSync, writeFileSync, statSync, existsSync, readdirSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import path from "node:path";
// Differential warp cases: each case lays out scratch copies and returns warp_probe ops. Paths are written with "/" and
// converted with W() where the probe must see backslashes.
const BS = String.fromCharCode(92);
const W = s => s.split("/").join(BS);
const G = "G:/KISS/COM3D2/GameData", PATCH = "script_charaevent004_2.arc", BASE = "script_charaevent004.arc";
const md5 = f => existsSync(f) ? createHash("md5").update(readFileSync(f)).digest("hex").slice(0, 10) + ":" + statSync(f).size : "missing";
function cp(r, sub, name) { const d = sub ? `${r}/${sub}` : r; mkdirSync(d, { recursive: true }); copyFileSync(`${G}/${name}`, `${d}/${name}`); }
function patchBytes(edit) { const b = Buffer.from(readFileSync(`${G}/${PATCH}`)); return edit ? edit(b) : b; }
// r is the scratch root with forward slashes.
const cases = {
  patch_only: r => { cp(r, "", PATCH); return [W(`${r}/${PATCH}`)]; },
  base_then_patch: r => { cp(r, "", PATCH); cp(r, "", BASE); return [W(`${r}/${BASE}`), W(`${r}/${PATCH}`)]; },
  patch_then_base: r => { cp(r, "", PATCH); cp(r, "", BASE); return [W(`${r}/${PATCH}`), W(`${r}/${BASE}`)]; },
  base_other_dir: r => { cp(r, "a", BASE); cp(r, "b", PATCH); return [W(`${r}/a/${BASE}`), W(`${r}/b/${PATCH}`)]; },
  base_other_dir_pref: r => { cp(r, "a", BASE); cp(r, "b", PATCH); return ["pref:" + W(`${r}/a`), W(`${r}/a/${BASE}`), W(`${r}/b/${PATCH}`)]; },
  base_other_dir_pref_slash: r => { cp(r, "a", BASE); cp(r, "b", PATCH); return ["pref:" + W(`${r}/a/`), W(`${r}/a/${BASE}`), W(`${r}/b/${PATCH}`)]; },
  pref_cleared: r => { cp(r, "a", BASE); cp(r, "b", PATCH); return ["pref:" + W(`${r}/a`), "clearpref", W(`${r}/a/${BASE}`), W(`${r}/b/${PATCH}`)]; },
  relative_with_base: r => { cp(r, "d", PATCH); cp(r, "d", BASE); return ["base:" + W(`${r}/`), W(`d/${BASE}`), W(`d/${PATCH}`)]; },
  relative_base_in_dir: r => { cp(r, "d", PATCH); cp(r, "d", BASE); return ["base:" + W(`${r}/d`), BASE, PATCH]; },
  forward_slashes: r => { cp(r, "", PATCH); cp(r, "", BASE); return [`${r}/${BASE}`, `${r}/${PATCH}`]; },
  mixed_slashes: r => { cp(r, "", PATCH); cp(r, "", BASE); return [W(`${r}/${BASE}`), `${r}/${PATCH}`]; },
  patch_twice: r => { cp(r, "", PATCH); cp(r, "", BASE); return [W(`${r}/${BASE}`), W(`${r}/${PATCH}`), W(`${r}/${PATCH}`)]; },
  upper_case_dir: r => { cp(r, "UpperDir", PATCH); cp(r, "UpperDir", BASE); return [W(`${r}/UpperDir/${BASE}`), W(`${r}/UPPERDIR/${PATCH}`)]; },
  base_wrong_name: r => { cp(r, "", PATCH); copyFileSync(`${G}/${BASE}`, `${r}/other.arc`); return [W(`${r}/other.arc`), W(`${r}/${PATCH}`)]; },
  truncated_patch: r => { writeFileSync(`${r}/${PATCH}`, patchBytes().subarray(0, 200)); cp(r, "", BASE); return [W(`${r}/${BASE}`), W(`${r}/${PATCH}`)]; },
  tiny_patch: r => { writeFileSync(`${r}/${PATCH}`, Buffer.from("warp")); cp(r, "", BASE); return [W(`${r}/${BASE}`), W(`${r}/${PATCH}`)]; },
  corrupt_index: r => { writeFileSync(`${r}/${PATCH}`, patchBytes(b => { b[20] ^= 0xff; return b; })); cp(r, "", BASE); return [W(`${r}/${BASE}`), W(`${r}/${PATCH}`)]; },
  corrupt_blob: r => { writeFileSync(`${r}/${PATCH}`, patchBytes(b => { b[200] ^= 0xff; return b; })); cp(r, "", BASE); return [W(`${r}/${BASE}`), W(`${r}/${PATCH}`)]; },
  bad_index_length: r => { writeFileSync(`${r}/${PATCH}`, patchBytes(b => { b.writeUInt32LE(0x7fffffff, 8); return b; })); cp(r, "", BASE); return [W(`${r}/${BASE}`), W(`${r}/${PATCH}`)]; },
  short_base: r => { cp(r, "", PATCH); writeFileSync(`${r}/${BASE}`, Buffer.from("abc")); return [W(`${r}/${BASE}`), W(`${r}/${PATCH}`)]; },
  not_warp_garbage: r => { writeFileSync(`${r}/${PATCH}`, Buffer.from("hello world, not an archive")); return [W(`${r}/${PATCH}`)]; },
};
function listState(root) {
  const out = [];
  const walk = d => { for (const f of readdirSync(d, { withFileTypes: true })) { const p = path.join(d, f.name); if (f.isDirectory()) walk(p); else out.push(path.relative(root, p).toLowerCase() + "=" + md5(p)); } };
  walk(root);
  return out.sort().join(";");
}
let bad = 0;
for (const [name, build] of Object.entries(cases)) {
  const res = {};
  for (const dll of ["orig_copy", "mine"]) {
    const root = path.resolve(`wc_${dll}`).split(BS).join("/");
    rmSync(root, { recursive: true, force: true }); mkdirSync(root);
    const ops = build(root);
    let out;
    try { out = execFileSync("./warp_probe.exe", [`./${dll}.dll`, ...ops], { encoding: "utf8", timeout: 120000 }); } catch (e) { out = "CRASH " + (e.status ?? e.signal) + " " + String(e.stdout || "").slice(0, 200); }
    res[dll] = (out.replace(/\s+/g, " ") + " || " + listState(root)).split(root).join("<R>").split(W(root)).join("<R>");
  }
  const same = res.orig_copy === res.mine;
  if (!same) bad++;
  console.log(same ? "same" : "DIFF", name, same ? res.mine.slice(0, 120) : "\n  orig: " + res.orig_copy.slice(0, 400) + "\n  mine: " + res.mine.slice(0, 400));
}
console.log({ cases: Object.keys(cases).length, bad });
for (const d of ["wc_orig_copy", "wc_mine"]) rmSync(d, { recursive: true, force: true });
