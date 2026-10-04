// Build-only SVG renderer: npm install --prefix artifacts/icon-builder
// --no-audit --no-fund @resvg/resvg-js@2.6.2. No runtime dependency.
import {createRequire} from 'node:module';
import fs from 'node:fs';
const require=createRequire(new URL('../artifacts/icon-builder/package.json',import.meta.url));
const {Resvg}=require('@resvg/resvg-js');
for(const file of process.argv.slice(2)){
  const svg=fs.readFileSync(file,'utf8').replaceAll('currentColor','#ffffff');
  fs.writeFileSync(file+'.png',new Resvg(svg,{fitTo:{mode:'width',value:384}}).render().asPng());
}
