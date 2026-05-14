// Generates favicon.ico + PNG icons from public/favicon.svg.
// Run: npm run icons   (or: node scripts/build-favicons.mjs)
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import sharp from 'sharp';
import pngToIco from 'png-to-ico';

const here = dirname(fileURLToPath(import.meta.url));
const pub  = resolve(here, '..', 'public');
const svg  = readFileSync(resolve(pub, 'favicon.svg'));

const pngSizes = [16, 32, 48, 180, 192, 512];
const buffers = {};
for (const s of pngSizes) {
  buffers[s] = await sharp(svg).resize(s, s).png().toBuffer();
}

writeFileSync(resolve(pub, 'favicon-16x16.png'),    buffers[16]);
writeFileSync(resolve(pub, 'favicon-32x32.png'),    buffers[32]);
writeFileSync(resolve(pub, 'apple-touch-icon.png'), buffers[180]);
writeFileSync(resolve(pub, 'icon-192.png'),         buffers[192]);
writeFileSync(resolve(pub, 'icon-512.png'),         buffers[512]);

const ico = await pngToIco([buffers[16], buffers[32], buffers[48]]);
writeFileSync(resolve(pub, 'favicon.ico'), ico);

console.log('favicons generated:',
  ['favicon.ico','favicon-16x16.png','favicon-32x32.png','apple-touch-icon.png','icon-192.png','icon-512.png'].join(', '));
