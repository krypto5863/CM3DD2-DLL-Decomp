import { readFileSync } from "node:fs";
import { parseCsv } from "./csv_parse.mjs";
const sjis = new TextDecoder("shift_jis");
const csvRows = parseCsv(sjis.decode(readFileSync("E:/COM3D2Test 2.5/COM3D2_5/neiFiles/edit_bg.csv")));
const lines = readFileSync("cells.txt", "utf8").trim().split(/\r?\n/).slice(1);
let checked = 0, bad = 0, outOfRange = 0;
for (const line of lines) {
  const [xs, ys, sizes, hex = ""] = line.split(" ");
  const x = +xs, y = +ys;
  if (x >= 8 || y >= 24) { outOfRange++; if (+sizes !== 0) { bad++; console.log("out-of-range non-empty", x, y); } continue; }
  const got = sjis.decode(Buffer.from(hex, "hex"));
  const want = csvRows[y][x] ?? "";
  checked++;
  if (got !== want) { bad++; if (bad <= 5) console.log("DIFF", x, y, JSON.stringify(got), JSON.stringify(want)); }
}
console.log({ checked, mismatches: bad, outOfRangeLines: outOfRange });
