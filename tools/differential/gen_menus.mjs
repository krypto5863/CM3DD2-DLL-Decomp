import { writeFileSync, mkdirSync } from "node:fs";
const out = process.argv[2];
mkdirSync(out, { recursive: true });
const str = s => { const b = Buffer.from(s, "utf8"); let n = b.length; const h = n < 128 ? [n] : [(n & 0x7f) | 0x80, n >> 7]; return Buffer.concat([Buffer.from(h), b]); };
function menu(version, src, item, cat, info, cmds) {
  const body = Buffer.concat(cmds.map(c => Buffer.concat([Buffer.from([c.length]), ...c.map(str)])));
  return Buffer.concat([str("CM3D2_MENU"), Buffer.from(new Uint32Array([version]).buffer), str(src), str(item), str(cat), str(info),
    Buffer.from(new Uint32Array([body.length + 1]).buffer), body, Buffer.from([0])]);
}
const files = {
  "a_plain.menu": [["name", "Plain Name"], ["icon", "a.tex"], ["category", "WEAR"], ["setumei", "line1《改行》line2《改行》end"]],
  "b_color.menu": [["name", "Sp　ace"], ["icons", "b.tex"], ["color_set", "Skirt", "SubName"], ["category", "skirt"], ["priority", "50"], ["saveitem", "x.model"]],
  "c_tex.menu": [["icon", "c.tex"], ["tex", "m", "t", "f", "1", "c3"], ["tex", "m", "t", "f", "1"], ["unsetitem"], ["collabo"]],
  "d_jp.menu": [["icon", "d.tex"], ["テクスチャ変更", "a", "b", "c", "d", "黒"], ["メニューフォルダ", "MAN"], ["name", "NoSpaceHere"]],
  "e_zpart_rest.menu": [["icon", "e.tex"], ["category", "nosuchcat"], ["color_set", "unknown"]],
  "f_zonly.menu": [["icon", "f.tex"]],
  "g_zx.menu": [["icon", "g.tex"]],
  "x_zurashi_a.menu": [["icon", "skipped.tex"]],
  "y_mekure_b.menu": [["icon", "skipped.tex"]],
  "h_noicon.menu": [["name", "NoIcon"]],
  "i_empty.menu": [],
  "j_unknowncmd.menu": [["frobnicate", "1", "2"], ["icon", "j.tex"], ["maskitem", "x"], ["name", " leadingspace"]],
  "k_a_zb_c_d.menu": [["icon", "k.tex"], ["unsetitem"], ["メニューフォルダ", "other"]],
};
for (const [n, c] of Object.entries(files)) writeFileSync(out + "/" + n, menu(1000, n, "item_" + n, "cat", "info " + n, c));
writeFileSync(out + "/z_badmagic.menu", Buffer.from("not a menu at all"));
writeFileSync(out + "/z_tiny.menu", Buffer.from([3, 1, 2]));
console.log("ok");
