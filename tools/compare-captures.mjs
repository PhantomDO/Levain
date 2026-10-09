// Compare deux captures du moteur (`--capture`), pixel par pixel : combien de pixels ont un canal qui
// s'écarte de plus de la tolérance, le plus grand écart, et la première et la dernière ligne où l'un
// d'eux s'écarte. Ces deux lignes situent l'écart dans l'image (le ciel en haut, le sol en bas) par
// une commande, et non à l'œil (règle n°6). Pour comparer deux backends sur la même image
// (build/GOTCHA.md, « Direct3D 12 sur la 4070 ») :
//   node tools/compare-captures.mjs <a.png> <b.png> [tolérance, 2 par défaut]
// Sort en 0 une fois la mesure écrite, en 1 si les images n'ont pas la même taille, en 2 sur un mauvais
// usage : c'est une mesure, pas un test ; à qui la lit de juger l'écart.
import { readFileSync } from "node:fs";
import { decodePng } from "./png.mjs";

const [first, second, toleranceText = "2"] = process.argv.slice(2);
const tolerance = Number(toleranceText);
if (!first || !second || !Number.isInteger(tolerance) || tolerance < 0) {
  console.error("usage : compare-captures.mjs <a.png> <b.png> [tolérance]");
  process.exit(2);
}

const a = decodePng(readFileSync(first));
const b = decodePng(readFileSync(second));
if (a.width !== b.width || a.height !== b.height) {
  console.error(`tailles différentes : ${a.width} × ${a.height} et ${b.width} × ${b.height}`);
  process.exit(1);
}

// Les trois canaux de couleur seulement : l'alpha d'une capture ne se voit pas à l'écran.
let differing = 0;
let largest = 0;
let firstRow = -1;
let lastRow = -1;
for (let pixel = 0; pixel < a.width * a.height; ++pixel) {
  let worst = 0;
  for (let channel = 0; channel < 3; ++channel) {
    const gap = Math.abs(a.pixels[pixel * a.channels + channel] - b.pixels[pixel * b.channels + channel]);
    worst = Math.max(worst, gap);
  }
  if (worst > tolerance) {
    ++differing;
    const row = Math.floor(pixel / a.width);
    if (firstRow < 0) firstRow = row;
    lastRow = row;
  }
  largest = Math.max(largest, worst);
}
const total = a.width * a.height;
const percent = ((100 * differing) / total).toFixed(2);
console.log(`${differing} pixels sur ${total} (${percent} %) à plus de ±${tolerance}, écart maximal ${largest}`);
// Les lignes se comptent depuis le haut de l'image, la première à 0, comme dans un éditeur d'images.
if (differing > 0) {
  console.log(`lignes qui s'écartent : de ${firstRow} à ${lastRow}, sur ${a.height} (0 en haut)`);
}
