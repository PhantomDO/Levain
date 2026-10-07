// Ouvre une page dans un Firefox lancé par tools/web-smoke.sh, attend que son titre annonce une
// image rendue, et la capture par WebDriver BiDi. Avec une référence, compare ses premiers pixels à
// la référence rendue par Vulkan ; avec « - », la page doit seulement tourner (le sandbox, dont le
// titre donne les images/s). Avec « ui », les panneaux de l'interface doivent dessiner. Code de
// sortie non nul au moindre écart.
//   node tools/web-smoke.mjs <url> <référence.ppm | -> <capture.png> [port] [ui]
import { readFileSync, writeFileSync } from "node:fs";
import { decodePng } from "./png.mjs";

const [url, referencePath, capturePath, port = "9222", expect = ""] = process.argv.slice(2);
if (expect !== "" && expect !== "ui") {
  console.error(`ÉCHEC : attente inconnue « ${expect} » (ui ou rien)`);
  process.exit(1);
}
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
  if (/dessiné|images\/s|échec/.test(title)) break;
  await new Promise((resolve) => setTimeout(resolve, 250));
}
// Lu avant la fin de la session : le calque des mesures du sandbox (#294), vide sur une autre page.
const overlayState = (await send("script.evaluate", {
  expression: "JSON.stringify({ report: window.levainLastReport ?? null, text: document.getElementById('stats')?.textContent ?? '' })",
  target: { context }, awaitPromise: false })).result.value;
const shot = await send("browsingContext.captureScreenshot", { context });
await send("session.end");
socket.close();
const png = Buffer.from(shot.data, "base64");
writeFileSync(capturePath, png);
if (!/dessiné|images\/s/.test(title)) {
  console.error(`ÉCHEC : la page n'a rien rendu (titre : « ${title} »)`);
  process.exit(1);
}
if (referencePath === "-") {
  console.log(`la page tourne : ${title}`);
  // Le calque des mesures (#294) : le moteur doit en avoir rendu compte, et la page l'afficher.
  const { report, text } = JSON.parse(overlayState);
  console.log(`calque :\n${text}`);
  const sane = report?.imagesPerSecond > 0 && report.engineMs > 0 && report.enginePercent <= 100 &&
    report.width > 0 && report.height > 0;
  // L'UI (ADR-0032), demandée par « ui » : les panneaux ouverts dessinent, et leur temps CPU
  // arrive. ImGui tourne à chaque image, panneaux ou pas : son temps seul ne prouverait rien.
  const uiSane = expect !== "ui" ||
    (report?.uiDraws > 0 && report.uiMs > 0 && text.includes("interface :"));
  if (!uiSane) {
    console.error("ÉCHEC : les panneaux de l'interface n'ont rien dessiné, ou la page n'en rend pas compte");
    process.exit(1);
  }
  if (!sane || !text.includes("images/s") || !text.includes("navigateur :")) {
    console.error("ÉCHEC : le calque des mesures est vide");
    process.exit(1);
  }
  process.exit(0);
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
