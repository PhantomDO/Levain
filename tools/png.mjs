// Partagé par tools/web-smoke.mjs et tools/khronos-compare.mjs.
import { inflateSync } from "node:zlib";

// --- Décodage minimal d'un PNG 8 bits RGB ou RGBA non entrelacé, ce que rend Firefox ---
export function decodePng(bytes) {
  let offset = 8;
  let width = 0, height = 0, channels = 0;
  const data = [];
  while (offset < bytes.length) {
    const length = bytes.readUInt32BE(offset);
    const type = bytes.toString("latin1", offset + 4, offset + 8);
    const body = bytes.subarray(offset + 8, offset + 8 + length);
    if (type === "IHDR") {
      width = body.readUInt32BE(0);
      height = body.readUInt32BE(4);
      const [depth, colour, , , interlace] = body.subarray(8, 13);
      channels = { 2: 3, 6: 4 }[colour];
      if (depth !== 8 || !channels || interlace !== 0)
        throw new Error(`PNG non géré (profondeur ${depth}, couleur ${colour}, entrelacé ${interlace})`);
    } else if (type === "IDAT") {
      data.push(body);
    }
    offset += 12 + length;
  }
  // Chaque ligne commence par son filtre (spécification PNG, §9) : on les défait un à un.
  const raw = inflateSync(Buffer.concat(data));
  const stride = width * channels;
  const pixels = Buffer.alloc(height * stride);
  const paeth = (a, b, c) => {
    const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
  };
  for (let y = 0; y < height; ++y) {
    const filter = raw[y * (stride + 1)];
    for (let x = 0; x < stride; ++x) {
      const value = raw[y * (stride + 1) + 1 + x];
      const left = x >= channels ? pixels[y * stride + x - channels] : 0;
      const up = y > 0 ? pixels[(y - 1) * stride + x] : 0;
      const upLeft = x >= channels && y > 0 ? pixels[(y - 1) * stride + x - channels] : 0;
      const predictor = [0, left, up, (left + up) >> 1, paeth(left, up, upLeft)][filter];
      pixels[y * stride + x] = (value + predictor) & 0xff;
    }
  }
  return { width, height, channels, pixels };
}
