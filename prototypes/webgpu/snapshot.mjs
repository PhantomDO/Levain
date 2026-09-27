// Ouvre une page dans un Firefox déjà lancé avec --remote-debugging-port, attend, puis rend son
// titre, les messages de sa console et une capture PNG, par WebDriver BiDi (le protocole
// d'automatisation intégré aux navigateurs). Node suffit : il a un client WebSocket.
//   node snapshot.mjs <url> <capture.png> [secondes] [port]
import { writeFileSync } from "node:fs";

const [url, capture, seconds = "6", port = "9222"] = process.argv.slice(2);
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
await new Promise((resolve) => (socket.onopen = resolve));
await send("session.new", { capabilities: {} });
// Un onglet neuf plutôt que le premier contexte venu, qui peut être une fenêtre interne de Firefox.
const { context } = await send("browsingContext.create", { type: "tab" });
// Limité à l'onglet : s'abonner à tous les contextes demanderait l'accès aux fenêtres internes de
// Firefox. La taille de la page vient de --window-size, au lancement.
await send("session.subscribe", { events: ["log.entryAdded"], contexts: [context] });
await send("browsingContext.navigate", { context, url, wait: "complete" });
await new Promise((resolve) => setTimeout(resolve, Number(seconds) * 1000));
const title = await send("script.evaluate", {
  expression: "document.title", target: { context }, awaitPromise: false });
console.log(`titre : ${title.result.value}`);
const shot = await send("browsingContext.captureScreenshot", { context });
writeFileSync(capture, Buffer.from(shot.data, "base64"));
await send("session.end");
socket.close();
