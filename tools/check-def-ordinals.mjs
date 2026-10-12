// Usage: node tools/check-def-ordinals.mjs <original cm3d2.dll> exports/cm3d2_file.def
// Verifies every .def ordinal against the PE export table of the original DLL.
import {readFileSync} from "node:fs";
const [,, dll, def] = process.argv;
const b = readFileSync(dll);
const pe = b.readUInt32LE(0x3c);
const nsec = b.readUInt16LE(pe+6), optsz = b.readUInt16LE(pe+20);
const opt = pe+24;
const expRva = b.readUInt32LE(opt+112);
const secs=[];
for (let i=0;i<nsec;i++){const s=opt+optsz+i*40;secs.push({va:b.readUInt32LE(s+12),vs:b.readUInt32LE(s+8),raw:b.readUInt32LE(s+20)});}
const off = r => {for(const s of secs) if(r>=s.va&&r<s.va+Math.max(s.vs,1)) return r-s.va+s.raw; throw new Error("rva")};
const e = off(expRva);
const base=b.readUInt32LE(e+16), nn=b.readUInt32LE(e+24), nameT=off(b.readUInt32LE(e+32)), ordT=off(b.readUInt32LE(e+36));
const str=o=>{let x=o;while(b[x])x++;return b.toString("latin1",o,x)};
const map={};
for(let i=0;i<nn;i++){map[str(off(b.readUInt32LE(nameT+i*4)))]=b.readUInt16LE(ordT+i*2)+base;}
console.log("export names:",nn);
let bad=0,n=0;
for(const l of readFileSync(def,"utf8").split(/\r?\n/)){const m=l.match(/^\s+(\w+)\s+@(\d+)/);if(!m)continue;n++;
 if(map[m[1]]!==+m[2]){bad++;console.log("MISMATCH",m[1],"def",m[2],"dll",map[m[1]]);}}
console.log("def entries",n,"mismatches",bad);
