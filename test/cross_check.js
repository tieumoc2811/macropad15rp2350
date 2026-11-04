// Doi chieu firmware (C) voi trang web (JS): bang ma, bo cuc layer 3, ma phim, va thuat toan tren chuoi su kien ngau nhien.
"use strict";
const fs = require('fs'), path = require('path'), vm = require('vm'), { execFileSync } = require('child_process');
const root = path.join(__dirname, '..', 'firmware');
const html = fs.readFileSync(path.join(root, 'macropad15.html'), 'utf8');
const src = fs.readFileSync(path.join(root, 'main.c'), 'utf8');
const hdr = fs.readFileSync(path.join(root, 'morse.h'), 'utf8');
let fails = 0, checks = 0;
const check = (c, msg) => { checks++; if (!c) { fails++; console.log('FAIL: ' + msg); } };

const eng = /\/\/ <morse-engine>([\s\S]*?)\/\/ <\/morse-engine>/.exec(html);
if (!eng) { console.log('FAIL: khong thay <morse-engine> trong macropad15.html'); process.exit(1); }
const pre = ['const KC_TRNS = 1;', /const MO = [^\n]*/.exec(html)[0]].join('\n');   // dung dung dong khai bao that trong trang (MO, TL, TD, NKK, CONS)
const js = vm.runInNewContext(pre + eng[1] + '\n({ MORSE_TAB, MORSE_MAX, MORSE_LAYOUT, KC_MORSE, morseNew, morseCommit, morsePress, morseFind })', {});

// 1) bang ma
const cTab = [...hdr.matchAll(/\{"([.\-]+)",\s*'((?:\\.|[^'\\])+)'\}/g)].map(m => [m[2].replace(/^\\(.)$/, '$1'), m[1]]);
check(cTab.length === js.MORSE_TAB.length && cTab.length === 54, 'so muc bang: C ' + cTab.length + ' JS ' + js.MORSE_TAB.length);
cTab.forEach((e, i) => check(js.MORSE_TAB[i] && js.MORSE_TAB[i][0] === e[0] && js.MORSE_TAB[i][1] === e[1], 'bang ma muc ' + i));
check(+/#define MORSE_MAX (\d+)/.exec(hdr)[1] === js.MORSE_MAX, 'MORSE_MAX C != JS');

// 2) ma phim + danh muc phim cua trang
for (const [n, k] of [['DOT', 'dot'], ['DASH', 'dash'], ['OK', 'ok']]) {
  const v = parseInt(new RegExp('#define KC_MORSE_' + n + ' (0x[0-9A-Fa-f]+)').exec(src)[1], 16);
  check(v === js.KC_MORSE[k], 'KC_MORSE_' + n + ' C 0x' + v.toString(16) + ' JS 0x' + js.KC_MORSE[k].toString(16));
}
check(/A\(0x7060,[^)]*'morse'/.test(html) && /A\(0x7061,[^)]*'morse'/.test(html) && /A\(0x7062,[^)]*'morse'/.test(html), 'danh muc phim cua trang thieu ma Morse');
check(+/#define FW_VER (\d+)/.exec(src)[1] >= 6 && /devInfo\.ver < 6/.test(html), 'FW_VER >= 6 va trang canh bao firmware < 6');

// 3) bo cuc layer 3 mac dinh: C (mo[] trong defaults) == JS (MORSE_LAYOUT)
const mo = /static const uint16_t mo\[NK\] = \{([\s\S]*?)\};/.exec(src)[1].replace(/\/\/[^\n]*/g, '');
const sym = { 'KC_TRNS': 1, 'KC_MORSE_DOT': 0x7060, 'KC_MORSE_DASH': 0x7061, 'KC_MORSE_OK': 0x7062 };
const cl = mo.split(',').map(t => t.trim()).filter(Boolean).map(t => { let m; if ((m = /^TL\((\d+)\)$/.exec(t))) return 0x4100 | +m[1]; if (t in sym) return sym[t]; if (/^(0x[0-9a-f]+|\d+)$/i.test(t)) return Number(t); throw new Error('token la trong mo[]: ' + t); });
check(cl.length === 20 && js.MORSE_LAYOUT.length === 20, 'mo[] co ' + cl.length + ' phan tu');
check(cl.every((v, i) => v === js.MORSE_LAYOUT[i]), 'bo cuc layer 3: C ' + JSON.stringify(cl.map(x => x.toString(16))) + ' != JS ' + JSON.stringify(js.MORSE_LAYOUT.map(x => x.toString(16))));
check(/KM\(3, i\) = mo\[i\]/.test(src) && /w\[K\(3, i\)\] = MORSE_LAYOUT\[i\]/.test(html), 'defaults() cua C hoac JS chua dat layer 3 = bo cuc Morse');
// ten layer mac dinh: C lname_get == JS layerLabel
check(/else if \(mo\) strcpy\(o, "MORSE"\)/.test(src) && /hasMorse\(l\) \? 'MORSE'/.test(html), 'ten mac dinh MORSE o C va JS');

// 4) thuat toan: chuoi su kien ngau nhien chay tren C va JS, so sanh may tinh sau tung su kien
let seed = 0x9E3779B9 >>> 0; const rnd = () => { seed ^= seed << 13; seed >>>= 0; seed ^= seed >>> 17; seed ^= seed << 5; seed >>>= 0; return seed; };
let stream = ''; for (let i = 0; i < 60000; i++) { const r = rnd() % 20; stream += r < 8 ? 'd' : r < 15 ? 'h' : r < 18 ? 'o' : r < 19 ? 's' : 'b'; }
const cOut = execFileSync(path.join(__dirname, 'build', 'morse_test'), ['--stream'], { input: stream, maxBuffer: 1 << 28 }).toString().split('\n');
const m = js.morseNew(); let host = ''; const io = { room: () => 99, bs() { host = host.slice(0, -1); }, put(ch) { host += ch; } };
let bad = 0;
for (let i = 0; i < stream.length; i++) {
  const e = stream[i];
  if (e === 'd' || e === 'h') js.morsePress(m, e === 'h', io); else if (e === 'o') js.morseCommit(m);
  else if (e === 's') { js.morseCommit(m); host += ' '; } else { js.morseCommit(m); host = host.slice(0, -1); }
  const mine = host.length + '|' + host.slice(-40);
  if (cOut[i] !== mine) { if (bad++ < 3) console.log('FAIL: su kien #' + i + ' (' + e + '): C ' + JSON.stringify(cOut[i]) + ' JS ' + JSON.stringify(mine)); }
}
check(bad === 0, bad + ' su kien C va JS khac nhau'); checks += stream.length;
console.log(checks + ' doi chieu C<->JS, ' + fails + ' loi');
process.exit(fails ? 1 : 0);
