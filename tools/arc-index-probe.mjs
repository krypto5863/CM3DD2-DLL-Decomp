// Probe: decrypt the warp archive index (verified on material_cas069_2.arc -> "material_cas069.arc").
// Usage: node tools/arc-index-probe.mjs <path to .arc>
// Key = loader local_2c8 words 0xecb97957 0x9f48d853 0xc50013a9 0x9656b303 (LE). NOT the CSV key.
import { readFileSync } from "node:fs";
import { createDecipheriv } from "node:crypto";

// Key copied from src/csv_api.cpp kCsvAesKey (startup constant at 0x180213ea8).
const KEY = Buffer.alloc(16); [0xecb97957,0x9f48d853,0xc50013a9,0x9656b303].forEach((w,i)=>KEY.writeUInt32LE(w>>>0,i*4));

// Same XSHIFT IV derivation as src/csv_api.cpp deriveIv.
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

const file = process.argv[2];
const b = readFileSync(file);
const magic = b.toString("latin1", 0, 4);
const L = b.readUInt32LE(8);
console.log({ file, magic, version: b.readUInt32LE(4), indexLen: L, mod16: L % 16 });
if (L % 16 !== 5) { console.log("index length is not 5 mod 16; stopping"); process.exit(0); }

const index = b.subarray(0x0c, 0x0c + L);
const cipherLen = L - 5;
const cipher = index.subarray(0, cipherLen);
const pad = index[cipherLen] ^ index[cipherLen + 1];
const encSeed = index.readUInt32LE(cipherLen + 1);
const seed = (encSeed ^ 0xbfbfbfbf) >>> 0;
console.log({ cipherLen, pad, seed: "0x" + seed.toString(16) });

const d = createDecipheriv("aes-128-cbc", KEY, deriveIv(seed));
d.setAutoPadding(false);
const plain = Buffer.concat([d.update(cipher), d.final()]);
const plainLen = cipherLen - pad;
const text = plain.subarray(0, plainLen);
console.log("plaintext length", plainLen);
console.log("hex  ", text.toString("hex"));
console.log("ascii", JSON.stringify(text.toString("latin1")));
