/* Reproduce the 8105-character subset without Windows command-line limits. */
'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const converter = require('./font-tools/node_modules/lv_font_conv/lib/cli');
const opentype = require('./font-tools/node_modules/lv_font_conv/node_modules/opentype.js');
const version = require('./font-tools/node_modules/lv_font_conv/package.json').version;
const dir = __dirname;
const data = path.join(dir, 'standard-characters');
const manifest = JSON.parse(fs.readFileSync(path.join(data, 'manifest.json'), 'utf8'));
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
(async () => {
  if (version !== '1.5.3') throw new Error('Expected lv_font_conv 1.5.3');
  for (const [file, expected] of Object.entries(manifest.sha256)) {
    if (hash(path.join(data,file)) !== expected) throw new Error(`Changed source: ${file}`);
  }
  const levels = [1,2,3].map((n,i) => {
    const chars = fs.readFileSync(path.join(data,`level-${n}.txt`),'utf8').replace(/\s/g,'');
    if ([...chars].length !== manifest.levels[i] || new Set(chars).size !== manifest.levels[i]) throw new Error(`Level ${n}`);
    return chars;
  });
  const chars = levels.join('');
  if ([...chars].length !== 8105 || new Set(chars).size !== 8105 || chars !== fs.readFileSync(path.join(data,'characters.txt'),'utf8').trim()) throw new Error('Canonical inventory mismatch');
  const fixed = '0x20-0x7E,0xA0-0xFF,0x2000-0x206F,0x3000-0x3030,0x3033-0x303F,0xFF00-0xFFEF';
  for (const [face,size,name] of [['Bold',16,'passport_cjk_16'],['Black',22,'passport_cjk_bold_22'],['Regular',12,'passport_cjk_12']]) {
    const fontfile = path.join(dir,`NotoSansCJKsc-${face}.otf`);
    const font = opentype.loadSync(fontfile);
    const missing = [...chars].filter(c => font.charToGlyphIndex(c) === 0);
    if (missing.length) throw new Error(`${face}: missing ${missing.join('')}`);
    await converter.run(['--font',fontfile,'--symbols',chars,'--range',fixed,'--size',String(size),'--bpp','2','--format','lvgl','--no-compress','--no-kerning','--lv-font-name',name,'--lv-include','lvgl.h','--output',path.join(dir,`${name}.c`)]);
    const output = path.join(dir, `${name}.c`);
    fs.writeFileSync(output, fs.readFileSync(output, 'utf8').replace(/[ \t]+$/gm, ''));
    console.log(`Generated ${name}: all 8105 standard characters`);
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
