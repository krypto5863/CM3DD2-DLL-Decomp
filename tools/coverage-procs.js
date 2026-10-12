const fs=require("fs"),path=require("path");
const j=JSON.parse(fs.readFileSync(process.argv[2],"utf8"));
const procs=j.normalized_result.filter(p=>!p.procedure.external&&/^[0-9a-fx:]/i.test(p.address)&&!p.address.startsWith("EXTERNAL")).map(p=>({a:p.address.replace(/^.*0x/i,"").toLowerCase().replace(/^0+/,""),name:p.value,thunk:p.procedure.thunk}));
// referenced addresses in repo text
let text="";
const walk=d=>{for(const e of fs.readdirSync(d,{withFileTypes:true})){const p=path.join(d,e.name);if(e.isDirectory()){if(["build","build-msvc","build-review","build-vs2026","dist","third_party","UserDecomped",".git","Otehr Profiles","node_modules"].includes(e.name))continue;walk(p);}else if(/\.(cpp|h|c|md|mjs|js|csv|txt|json)$/.test(e.name)&&fs.statSync(p).size<5e6)text+=fs.readFileSync(p,"latin1")+"\n";}};
walk(".");
const refs=new Set((text.match(/(?:FUN_|0x)?1[0-9a-f]{8}\b/gi)||[]).map(s=>s.replace(/^(FUN_|0x)/i,"").toLowerCase().replace(/^0+/,"")));
const ranges={};
const out=[];
for(const p of procs){const covered=refs.has(p.a); p.covered=covered; out.push(p);}
fs.writeFileSync("C:/Users/Auroras/AppData/Local/Temp/claude/W--MeidoUnmanaged/7b9876d1-8943-4737-8e22-98591cdc0cb2/scratchpad/procs.json",JSON.stringify(out));
console.log("internal procs",out.length,"referenced",out.filter(p=>p.covered).length);
// bucket by 0x10000
const b={};for(const p of out){const k=parseInt(p.a,16)>>>12<<12; const key=(k).toString(16);b[key]=b[key]||{n:0,c:0};b[key].n++;if(p.covered)b[key].c++;}
let line="";for(const k of Object.keys(b).sort((x,y)=>parseInt(x,16)-parseInt(y,16))){line+=k.padStart(9)+" "+b[k].c+"/"+b[k].n+"\n";}
fs.writeFileSync("C:/Users/Auroras/AppData/Local/Temp/claude/W--MeidoUnmanaged/7b9876d1-8943-4737-8e22-98591cdc0cb2/scratchpad/buckets.txt",line);
