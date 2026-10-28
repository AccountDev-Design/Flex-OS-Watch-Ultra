// RELOJ: la pagina del movil aplica el perfil de medios que manda el reloj en
// /api/hello (wweb_media_json de wm_web_logic.c) y descarta valores fuera de
// margen. Uso: node webui_media_check.js app.js wm_web_logic.c
const fs = require('fs');
const src = fs.readFileSync(process.argv[2], 'utf8');
const c = fs.readFileSync(process.argv[3], 'utf8');
const fnSrc = c.slice(c.indexOf('wweb_media_json(void)'));
const body = fnSrc.match(/return\s+((?:"(?:[^"\\]|\\.)*"\s*)+);/)[1].match(/"((?:[^"\\]|\\.)*)"/g).map(s => JSON.parse(s)).join('');
const media = JSON.parse('{' + body + '}');
const FX = {}; const DEV = { name: 'Flex OS Watch Ultra' };
const prof = src.slice(src.indexOf('FX.PROFILES = {'), src.indexOf('FX.PROFILE_HINT = {'));
const hint = src.slice(src.indexOf('FX.PROFILE_HINT = {'), src.indexOf('};', src.indexOf('FX.PROFILE_HINT = {')) + 2);
const fn = src.slice(src.indexOf('function applyMedia'), src.indexOf('// RELOJ: nombre, identidad'));
eval(prof + hint + fn + 'applyMedia(media); applyMedia({photo:{rec:{max:999999}}}); applyMedia("x"); applyMedia(null);');
const P = FX.PROFILES;
const ok = P.photo.rec.max === 1280 && P.video.rec.long === 360 && P.video.high.long === 400 && P.video.high.fps === 15 && P.audio.rec.rate === 22050 &&
  FX.PROFILE_HINT.rec.includes('Flex OS Watch Ultra') && FX.PROFILE_HINT.rec.includes('1280 px');
console.log(ok ? 'perfil del reloj aplicado: OK' : 'FALLO ' + JSON.stringify(P) + FX.PROFILE_HINT.rec);
process.exit(ok ? 0 : 1);
