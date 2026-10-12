// Verification tools for the NEI/CSV carrier. Usage:
//   node tools/nei-carrier-probe.mjs <file.nei> <out.bin>   decrypt carrier with CSV key
//   tools/nei_dump.cpp: links cm3d2_reconstructed; prints every cell as hex (see the project notes (not part of this repository))
import { readFileSync } from "node:fs";
import { createDecipheriv } from "node:crypto";

// CSV carrier key from src/csv_api.cpp kCsvAesKey.
const KEY = Buffer.from("aac9d235228720f240c5617c01df6654", "hex");

function deriveIv(seed) {
  const s = [0x075bcd15, 0x159a55e5, 0x1f123bb5, seed >>> 0];
  const iv = Buffer.alloc(16);
  for (let i = 0; i < 4; i++) {
    const t = ((s[0] << 11) ^ s[0]) >>> 0;
    const prev = s[3];
    const next = ((((prev >>> 11) ^ t) >>> 8) ^ prev ^ t) >>> 0;
    s[0] = s[1]; s[1] = s[2]; s[2] = prev; s[3] = next;
    iv.writeUInt32LE(next, i * 4);
  }
  return iv;
}

const b = readFileSync(process.argv[2]);
const L = b.length;
const cipherLen = L - 5;
console.log({ size: L, cipherLen, aligned: cipherLen % 16 === 0 });
const pad = b[cipherLen] ^ b[cipherLen + 1];
const seed = (b.readUInt32LE(cipherLen + 1) ^ 0xbfbfbfbf) >>> 0;
console.log({ pad, seed: "0x" + seed.toString(16) });
const d = createDecipheriv("aes-128-cbc", KEY, deriveIv(seed));
d.setAutoPadding(false);
const plain = Buffer.concat([d.update(b.subarray(0, cipherLen)), d.final()]).subarray(0, cipherLen - pad);
console.log("plain length", plain.length);
console.log("head hex", plain.subarray(0, 48).toString("hex"));
console.log("head ascii", JSON.stringify(plain.subarray(0, 48).toString("latin1")));
require_out: {
  const fs = await import("node:fs");
  fs.writeFileSync(process.argv[3], plain);
}
