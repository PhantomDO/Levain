// Le calque des mesures de la page web (#294) : ce que le moteur mesure, et la machine sur laquelle
// il tourne. Partagé par la page du sandbox (shell.html) et par l'Artifact qui la publie.
//
// Les chiffres d'images viennent du moteur, une fois par seconde (engine/app/src/app.cpp, `reportFrame`,
// qui appelle `Module.onFrameReport`) : pas de seconde mesure ici. La machine se lit une fois, dans
// les API du navigateur. Aucun navigateur ne donne l'occupation du CPU ni du GPU (docs/QA.md,
// 06/10/2026) : la part de l'image passée dans le moteur est ce qui s'en approche.
//
// Le piège des horloges arrondies : pour la vie privée, le navigateur arrondit l'horloge que lit le
// moteur (1 ms sous Firefox, 0,1 ms sous Chromium, faute d'isolation cross-origin). Une image
// isolée se lit donc mal ; la moyenne sur une seconde lisse l'arrondi, sans que l'écart restant
// ait été mesuré.
"use strict";

// Un nombre à la française, avec `digits` chiffres après la virgule.
const levainNumber = (value, digits) =>
  value.toLocaleString("fr-FR", { minimumFractionDigits: digits, maximumFractionDigits: digits });

// Le navigateur et sa version : par User-Agent Client Hints quand il les a (Chromium), sinon par la
// chaîne User-Agent. Les marques factices (« Not A Brand ») que Chromium y mêle sont écartées.
function levainBrowser() {
  const brands = navigator.userAgentData?.brands?.filter((brand) => !/not.?a.?brand/i.test(brand.brand));
  if (brands?.length) {
    const brand = brands.find((b) => b.brand !== "Chromium") ?? brands[0];
    return `${brand.brand} ${brand.version} · ${navigator.userAgentData.platform || "?"}`;
  }
  const match = navigator.userAgent.match(/(Firefox|Edg|OPR|Chrome|Version)\/(\d+)/);
  const name = { Edg: "Edge", OPR: "Opera", Version: "Safari" }[match?.[1]] ?? match?.[1] ?? "navigateur inconnu";
  return match ? `${name} ${match[2]}` : name;
}

// Le GPU tel que WebGPU le décrit. Le navigateur choisit ce qu'il en dit : Chromium donne le vendeur
// et l'architecture, Firefox peut ne rien donner. Le même que demande le moteur, le plus puissant
// (engine/gpu/src/webgpu/create.cpp) : sur une machine à deux GPU, l'autre serait décrit.
async function levainGpu() {
  if (!navigator.gpu) return "pas de WebGPU";
  const adapter = await navigator.gpu.requestAdapter({ powerPreference: "high-performance" });
  if (!adapter) return "aucun adaptateur WebGPU";
  const info = adapter?.info ?? (await adapter?.requestAdapterInfo?.());
  const text = [info?.vendor, info?.architecture, info?.device, info?.description].filter(Boolean).join(" ");
  return text || "non communiqué par le navigateur";
}

// La mémoire, telle que Chromium la donne : arrondie à la puissance de deux la plus proche, de 0,25
// à 8 Go (une tablette de 3 Go en annonce 4). D'où le « ≈ », et « ou plus » au plafond seulement.
function levainMemory() {
  const memory = navigator.deviceMemory;
  if (!memory) return "mémoire non communiquée";
  if (memory >= 8) return "8 Go ou plus";
  return `≈ ${levainNumber(memory, memory < 1 ? 2 : 0)} Go`;
}

// Branche le calque sur `element` et rend la fonction à donner à `Module.onFrameReport`.
function levainStatsOverlay(element) {
  let machine = [`navigateur : ${levainBrowser()}`];
  const cores = navigator.hardwareConcurrency;
  machine.push(`${cores ? `${cores} cœurs logiques` : "cœurs non communiqués"} · ${levainMemory()}`);
  let frame = ["en attente de la première seconde…"];
  const draw = () => { element.textContent = [...frame, ...machine].join("\n"); };
  levainGpu()
    .then((gpu) => { machine = [`GPU : ${gpu}`, ...machine]; })
    .catch((error) => { machine = [`GPU : illisible (${error.message})`, ...machine]; })
    .finally(draw);
  draw();
  return (report) => {
    // Lu par tools/web-smoke.mjs : le moteur a bien rendu compte de ses images.
    window.levainLastReport = report;
    frame = [
      `${levainNumber(report.imagesPerSecond, 0)} images/s · ${levainNumber(report.averageMs, 1)} ms par image ` +
        `(max ${levainNumber(report.maxMs, 1)})`,
      `moteur : ${levainNumber(report.engineMs, 1)} ms, ${levainNumber(report.enginePercent, 0)} % de l'image`,
      `rendu : ${report.width} × ${report.height} px · écran ×${levainNumber(devicePixelRatio, 2)}`,
    ];
    draw();
  };
}
