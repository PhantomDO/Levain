// Ouvre la page du cube (tests/web) dans un Firefox lancé par tools/web-smoke.sh, attend que le
// titre annonce le cube dessiné, capture la page par WebDriver BiDi et compare ses 64 × 64 premiers
// pixels à la référence rendue par Vulkan. Code de sortie non nul au moindre écart.
//   node tools/web-smoke.mjs <url> <référence.ppm> <capture.png> [port]
import { readFileSync, writeFileSync } from "node:fs";
import { inflateSync } from "node:zlib";

const [url, referencePath, capturePath, port = "9222"] = process.argv.slice(2);
const Tolerance = 2; // comme tests/smoke_render.cpp
const TimeoutMs = 30000;

// --- WebDriver BiDi, le protocole d'automatisation intégré à Firefox ---
const socket = new WebSocket(`ws://127.0.0.1:${port}/session`);
const pending = new Map();
let nextId = 1;
const send = (method, params = {}) =>
  new Promise((resolve, reject) => {
    const id = nextId++;
    pending.set(id, { resolve, reject });
    socket.send(JSON.stringify({ id, method, params }));
  });
socket.onmessage = (event) => {
  const message = JSON.parse(event.data);
  if (message.method === "log.entryAdded") {
    console.log(`console : ${message.params.text}`);
  } else if (pending.has(message.id)) {
    const { resolve, reject } = pending.get(message.id);
    pending.delete(message.id);
    message.type === "error" ? reject(new Error(message.message)) : resolve(message.result);
  }
};
await new Promise((resolve, reject) => {
  socket.onopen = resolve;
  socket.onerror = () => reject(new Error(`pas de Firefox sur le port ${port}`));
});
await send("session.new", { capabilities: {} });
// Un onglet neuf, et un abonnement limité à cet onglet : le reste demanderait l'accès aux fenêtres
// internes de Firefox (build/GOTCHA.md).
const { context } = await send("browsingContext.create", { type: "tab" });
await send("session.subscribe", { events: ["log.entryAdded"], contexts: [context] });
await send("browsingContext.navigate", { context, url, wait: "complete" });

let title = "";
for (const start = Date.now(); Date.now() - start < TimeoutMs; ) {
  const result = await send("script.evaluate", {
    expression: "document.title", target: { context }, awaitPromise: false });
  title = result.result.value;
  if (title.includes("dessiné") || title.includes("échec")) break;
  await new Promise((resolve) => setTimeout(resolve, 250));
}
const shot = await send("browsingContext.captureScreenshot", { context });
await send("session.end");
socket.close();
const png = Buffer.from(shot.data, "base64");
writeFileSync(capturePath, png);
if (!title.includes("dessiné")) {
  console.error(`ÉCHEC : la page n'a pas dessiné le cube (titre : « ${title} »)`);
  process.exit(1);
}

// --- Décodage minimal d'un PNG 8 bits RGB ou RGBA non entrelacé, ce que rend Firefox ---
function decodePng(bytes) {
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

// --- Comparaison avec la référence PPM (P6, 255) de tests/data ---
const reference = readFileSync(referencePath);
const [, refWidth, refHeight] = reference.toString("latin1", 0, 20).match(/^P6\s+(\d+)\s+(\d+)\s+255\s/).map(Number);
const header = reference.length - refWidth * refHeight * 3;
const image = decodePng(png);
if (image.width < refWidth || image.height < refHeight) {
  console.error(`ÉCHEC : capture de ${image.width} × ${image.height}, plus petite que la référence`);
  process.exit(1);
}
let different = 0;
for (let y = 0; y < refHeight; ++y) {
  for (let x = 0; x < refWidth; ++x) {
    for (let c = 0; c < 3; ++c) {
      const got = image.pixels[(y * image.width + x) * image.channels + c];
      const expected = reference[header + (y * refWidth + x) * 3 + c];
      if (Math.abs(got - expected) > Tolerance) {
        ++different;
        break;
      }
    }
  }
}
console.log(`${different} pixels différents sur ${refWidth * refHeight}`);
process.exit(different === 0 ? 0 : 1);
