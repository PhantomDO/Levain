// Les deux moitiés de tools/khronos-compare.sh (#125, #131).
//   node tools/khronos-compare.mjs capture <ibl|direct> <url> <capture.png> <port>
//     Ouvre le glTF Sample Viewer dans le Firefox lancé par le script, capture son image et écrit
//     sur la sortie la position de sa caméra, « x,y,z », relue dans les appels WebGL de la page.
//     En `direct`, coupe d'abord l'IBL (son interrupteur « Image Based ») et écrit sur une seconde
//     ligne la direction d'où vient sa lumière principale.
//   node tools/khronos-compare.mjs measure <viewer.png> <levain.png> <x,y,z>
//     Mesure l'écart de couleur, sphère par sphère, entre les deux images de MetalRoughSpheres.
import { readFileSync, writeFileSync } from "node:fs";
import { decodePng } from "./png.mjs";

export const Width = 1920;
export const Height = 1080;
/// Le champ vertical du viewer (PerspectiveCamera.yfov), celui de `--view khronos`.
const FovDegrees = 45;
const TimeoutMs = 60000;

const [command, ...args] = process.argv.slice(2);
if (command === "capture") {
  await capture(...args);
} else if (command === "measure") {
  measure(...args);
} else {
  console.error("usage : khronos-compare.mjs capture|measure …");
  process.exit(2);
}

async function capture(mode, url, capturePath, port) {
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
    if (pending.has(message.id)) {
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
  const { context } = await send("browsingContext.create", { type: "tab" });
  await send("browsingContext.setViewport", { context, viewport: { width: Width, height: Height } });
  // Le viewer ne publie pas sa caméra : on la lit dans ce qu'il envoie au GPU. Ce script tourne
  // avant ceux de la page et note le dernier vec3 envoyé à chaque uniforme, par son nom.
  await send("script.addPreloadScript", {
    functionDeclaration: `() => {
      const gl = WebGL2RenderingContext.prototype;
      const locate = gl.getUniformLocation;
      gl.getUniformLocation = function (program, name) {
        const location = locate.call(this, program, name);
        if (location) location.levainName = name;
        return location;
      };
      const send3 = gl.uniform3fv;
      window.levainUniforms = {};
      gl.uniform3fv = function (location, data, ...rest) {
        if (location?.levainName) window.levainUniforms[location.levainName] = Array.from(data);
        return send3.call(this, location, data, ...rest);
      };
    }`,
  });
  await send("browsingContext.navigate", { context, url, wait: "complete" });
  let camera;
  for (const start = Date.now(); !camera && Date.now() - start < TimeoutMs; ) {
    await new Promise((resolve) => setTimeout(resolve, 500));
    const result = await send("script.evaluate", {
      expression: "JSON.stringify(window.levainUniforms.u_Camera ?? null)",
      target: { context },
      awaitPromise: false,
    });
    camera = JSON.parse(result.result.value);
  }
  if (!camera) {
    console.error("ÉCHEC : le viewer n'a dessiné aucune image (u_Camera jamais envoyée)");
    process.exit(1);
  }
  const evaluate = async (expression) =>
    (await send("script.evaluate", { expression, target: { context }, awaitPromise: false }))
      .result.value;
  let light;
  if (mode === "direct") {
    // L'interface est cachée (noUI), mais ses composants existent : cliquer l'interrupteur suffit.
    // Sans IBL, le viewer allume deux lumières directionnelles fixes : la principale (u_Lights[0],
    // intensité 1) et une d'appoint (0,5) venant de l'opposé, qui n'éclaire pas la face des sphères
    // tournée vers l'œil, là où se fait la mesure.
    const clicked = await evaluate(`(() => {
      const label = [...document.querySelectorAll("label.switch")]
        .find((candidate) => candidate.textContent.includes("Image Based"));
      if (!label) return false;
      label.querySelector("input").click();
      return true;
    })()`);
    if (!clicked) {
      console.error("ÉCHEC : interrupteur « Image Based » introuvable dans le viewer");
      process.exit(1);
    }
    for (const start = Date.now(); !light && Date.now() - start < TimeoutMs; ) {
      await new Promise((resolve) => setTimeout(resolve, 500));
      light = JSON.parse(await evaluate(`JSON.stringify(window.levainUniforms["u_Lights[0].direction"] ?? null)`));
    }
    if (!light) {
      console.error("ÉCHEC : le viewer sans IBL n'a envoyé aucune lumière");
      process.exit(1);
    }
  }
  // Le modèle dessiné, le viewer filtre encore le ciel pour l'IBL : quelques secondes de marge.
  await new Promise((resolve) => setTimeout(resolve, 8000));
  const shot = await send("browsingContext.captureScreenshot", { context });
  await send("session.end");
  socket.close();
  writeFileSync(capturePath, Buffer.from(shot.data, "base64"));
  console.log(camera.join(","));
  // La direction de la lumière, vers où elle va ; le sandbox veut d'où elle vient.
  if (light) console.log(light.map((component) => -component).join(","));
}

/// sRGB 8 bits vers CIELAB (illuminant D65) : un écart de 1 est à peine visible (ΔE76).
function labOf([r, g, b]) {
  const linear = [r, g, b].map((c) => {
    const v = c / 255;
    return v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4;
  });
  const xyz = [
    (0.4124 * linear[0] + 0.3576 * linear[1] + 0.1805 * linear[2]) / 0.95047,
    0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2],
    (0.0193 * linear[0] + 0.1192 * linear[1] + 0.9505 * linear[2]) / 1.08883,
  ].map((t) => (t > 216 / 24389 ? Math.cbrt(t) : (24389 / 27 * t + 16) / 116));
  return [116 * xyz[1] - 16, 500 * (xyz[0] - xyz[1]), 200 * (xyz[1] - xyz[2])];
}

function measure(viewerPath, levainPath, cameraText) {
  const camera = cameraText.split(",").map(Number);
  const images = [viewerPath, levainPath].map((path) => decodePng(readFileSync(path)));
  for (const image of images) {
    if (image.width !== Width || image.height !== Height) {
      console.error(`ÉCHEC : image de ${image.width} × ${image.height}, ${Width} × ${Height} attendu`);
      process.exit(1);
    }
  }
  // Les 7 × 7 sphères blanches de MetalRoughSpheres, au premier plan (z = 0 dans le monde, rayon
  // 0,4) : de 1,2 en 1,2, la rugosité croît de gauche à droite, le métal de bas en haut. Chaque
  // sphère se lit sur un disque de 6 pixels autour de son centre, où la normale fait face à l'œil.
  const focal = 1 / Math.tan((FovDegrees * Math.PI) / 360);
  const aspect = Width / Height;
  const meanAround = (image, cx, cy) => {
    const sum = [0, 0, 0];
    let count = 0;
    for (let dy = -6; dy <= 6; ++dy) {
      for (let dx = -6; dx <= 6; ++dx) {
        if (dx * dx + dy * dy > 36) continue;
        const index = ((cy + dy) * image.width + cx + dx) * image.channels;
        for (let c = 0; c < 3; ++c) sum[c] += image.pixels[index + c];
        ++count;
      }
    }
    return sum.map((s) => s / count);
  };
  const rows = [];
  let total = 0;
  let worst = { deltaE: -1 };
  for (let row = 0; row < 7; ++row) {
    const cells = [];
    for (let column = 0; column < 7; ++column) {
      const world = [-3.6 + 1.2 * column, 3.6 - 1.2 * row, 0];
      const depth = camera[2] - world[2];
      const x = Math.round(((1 + (focal / aspect) * (world[0] - camera[0]) / depth) * Width) / 2);
      const y = Math.round(((1 - focal * (world[1] - camera[1]) / depth) * Height) / 2);
      const [a, b] = images.map((image) => labOf(meanAround(image, x, y)));
      const deltaE = Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
      total += deltaE;
      if (deltaE > worst.deltaE) worst = { deltaE, row, column, a, b };
      cells.push(deltaE.toFixed(1).padStart(5));
    }
    rows.push(cells.join(" "));
  }
  console.log("ΔE76 par sphère (colonnes : rugosité 0 → 1 ; lignes : métal 1 → 0)");
  for (const line of rows) console.log(line);
  const metallic = (1 - worst.row / 6).toFixed(2);
  const roughness = (worst.column / 6).toFixed(2);
  console.log(`moyenne : ${(total / 49).toFixed(2)} ; pire : ${worst.deltaE.toFixed(2)}` +
    ` (métal ${metallic}, rugosité ${roughness} ; L* ${worst.a[0].toFixed(1)} contre ${worst.b[0].toFixed(1)})`);
}
