import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import {
  PRESETS,
  rminM,
  classifyTarget,
  codeColor,
  codeLabel,
  sampleHeight,
  elevationDeg,
  inElevationBeam,
  slantRangeM,
  CODE_VISIBLE,
} from "./radar.js";

const viewEl = document.getElementById("view");
const busyEl = document.getElementById("busy");
const rhiCanvas = document.getElementById("rhi");
const rhiCtx = rhiCanvas.getContext("2d");

const state = {
  faction: "US",
  mode: "search",
  clickMode: "target",
  agl: 20,
  exag: 6,
  overlay: true,
  meta: null,
  grid: null,
  radar: { x: 0, y: 0, z: 0 },
  target: null,
  codes: null,
  worker: null,
};

function currentPreset() {
  if (state.faction === "US" && state.mode === "search") {
    return PRESETS.us_search;
  }
  if (state.faction === "USSR" && state.mode === "search") {
    return PRESETS.ussr_search;
  }
  if (state.faction === "US" && state.mode === "wlr") {
    return PRESETS.us_wlr;
  }
  return PRESETS.ussr_wlr;
}

function presetId() {
  return currentPreset().id;
}

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.setSize(viewEl.clientWidth, viewEl.clientHeight);
renderer.setClearColor(0x0b1016, 1);
viewEl.appendChild(renderer.domElement);

const scene = new THREE.Scene();
scene.fog = new THREE.Fog(0x0b1016, 8000, 22000);

const camera = new THREE.PerspectiveCamera(
  50,
  viewEl.clientWidth / Math.max(1, viewEl.clientHeight),
  1,
  80000,
);
const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;

scene.add(new THREE.AmbientLight(0xb8c4d0, 0.45));
const sun = new THREE.DirectionalLight(0xfff2d8, 0.9);
sun.position.set(-4000, 6000, 2500);
scene.add(sun);

const terrainMesh = new THREE.Mesh();
const radarGroup = new THREE.Group();
const targetGroup = new THREE.Group();
const losLine = new THREE.Line(
  new THREE.BufferGeometry(),
  new THREE.LineBasicMaterial({ color: 0x2ecc71 }),
);
const rangeRing = new THREE.Line(
  new THREE.BufferGeometry(),
  new THREE.LineBasicMaterial({ color: 0xf4f1de }),
);
const rminRing = new THREE.Line(
  new THREE.BufferGeometry(),
  new THREE.LineDashedMaterial({ color: 0xff3355, dashSize: 40, gapSize: 24 }),
);
scene.add(terrainMesh);
scene.add(radarGroup);
scene.add(targetGroup);
scene.add(losLine);
scene.add(rangeRing);
scene.add(rminRing);

function worldY(asl) {
  return asl * state.exag;
}

function makeRingGeometry(cx, cz, radius, y, segments) {
  const pts = [];
  let i = 0;
  while (i <= segments) {
    const a = (i / segments) * Math.PI * 2.0;
    pts.push(new THREE.Vector3(cx + Math.cos(a) * radius, y, cz + Math.sin(a) * radius));
    i = i + 1;
  }
  return new THREE.BufferGeometry().setFromPoints(pts);
}

function buildTerrainGeometry(meta, terrain, surface) {
  const n = meta.width;
  const positions = new Float32Array(n * n * 3);
  const colors = new Float32Array(n * n * 3);
  const indices = [];
  let iz = 0;
  while (iz < n) {
    let ix = 0;
    while (ix < n) {
      const idx = iz * n + ix;
      const h = terrain[idx];
      let y = 0.0;
      if (Number.isFinite(h)) {
        y = worldY(h);
      }
      const p = idx * 3;
      positions[p] = meta.origin_x + ix * meta.cell_m;
      positions[p + 1] = y;
      positions[p + 2] = meta.origin_z + iz * meta.cell_m;
      let shade = 0.35;
      if (ix > 0 && iz > 0 && ix < n - 1 && iz < n - 1) {
        const hx1 = terrain[iz * n + ix + 1];
        const hx0 = terrain[iz * n + ix - 1];
        const hz1 = terrain[(iz + 1) * n + ix];
        const hz0 = terrain[(iz - 1) * n + ix];
        if (Number.isFinite(hx1) && Number.isFinite(hx0) && Number.isFinite(hz1) && Number.isFinite(hz0)) {
          const dzdx = (hx1 - hx0) / (2.0 * meta.cell_m);
          const dzdz = (hz1 - hz0) / (2.0 * meta.cell_m);
          const nx = -dzdx;
          const ny = 1.0;
          const nz = -dzdz;
          const inv = 1.0 / Math.sqrt(nx * nx + ny * ny + nz * nz);
          const ndot = (-0.5 * nx + ny + 0.3 * nz) * inv;
          shade = 0.28 + 0.55 * Math.max(0.0, ndot);
        }
      }
      if (surface[idx] === 1) {
        colors[p] = 0.12 * shade * 2.2;
        colors[p + 1] = 0.28 * shade * 1.6;
        colors[p + 2] = 0.48 * shade * 1.3;
      } else {
        colors[p] = 0.22 * shade + 0.18;
        colors[p + 1] = 0.32 * shade + 0.16;
        colors[p + 2] = 0.14 * shade + 0.08;
      }
      if (ix < n - 1 && iz < n - 1) {
        const a = idx;
        const b = idx + 1;
        const c = idx + n;
        const d = idx + n + 1;
        indices.push(a, c, b);
        indices.push(b, c, d);
      }
      ix = ix + 1;
    }
    iz = iz + 1;
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute("position", new THREE.BufferAttribute(positions, 3));
  geo.setAttribute("color", new THREE.BufferAttribute(colors, 3));
  geo.setIndex(indices);
  geo.computeVertexNormals();
  return geo;
}

function applyOverlayColors() {
  const geo = terrainMesh.geometry;
  if (!geo || !state.meta) {
    return;
  }
  const colors = geo.getAttribute("color");
  const n = state.meta.width;
  const base = geo.userData.baseColors;
  let i = 0;
  while (i < n * n) {
    const p = i * 3;
    if (state.overlay && state.codes) {
      const cc = codeColor(state.codes[i]);
      colors.setXYZ(
        i,
        base[p] * 0.35 + cc[0] * 0.65,
        base[p + 1] * 0.35 + cc[1] * 0.65,
        base[p + 2] * 0.35 + cc[2] * 0.65,
      );
    } else {
      colors.setXYZ(i, base[p], base[p + 1], base[p + 2]);
    }
    i = i + 1;
  }
  colors.needsUpdate = true;
}

function applyExaggeration() {
  if (!state.meta || !state.grid) {
    return;
  }
  const geo = terrainMesh.geometry;
  const pos = geo.getAttribute("position");
  const n = state.meta.width;
  let i = 0;
  while (i < n * n) {
    const h = state.grid.terrain[i];
    let y = 0.0;
    if (Number.isFinite(h)) {
      y = worldY(h);
    }
    pos.setY(i, y);
    i = i + 1;
  }
  pos.needsUpdate = true;
  geo.computeVertexNormals();
  refreshMarkers();
}

function setActiveButtons(rowId, attr, value) {
  const buttons = document.querySelectorAll("#" + rowId + " button");
  let i = 0;
  while (i < buttons.length) {
    const btn = buttons[i];
    if (btn.getAttribute(attr) === value) {
      btn.classList.add("active");
    } else {
      btn.classList.remove("active");
    }
    i = i + 1;
  }
}

function refreshRings() {
  const preset = currentPreset();
  const y = worldY(state.radar.y);
  rangeRing.geometry.dispose();
  rangeRing.geometry = makeRingGeometry(state.radar.x, state.radar.z, preset.range_m, y, 128);
  rminRing.geometry.dispose();
  rminRing.geometry = makeRingGeometry(state.radar.x, state.radar.z, rminM(preset), y, 96);
  rminRing.computeLineDistances();
}

function refreshMarkers() {
  radarGroup.clear();
  const radarMat = new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.4 });
  const pole = new THREE.Mesh(new THREE.CylinderGeometry(4, 4, 40, 8), radarMat);
  pole.position.set(state.radar.x, worldY(state.radar.y) + 20, state.radar.z);
  const dish = new THREE.Mesh(new THREE.SphereGeometry(18, 16, 12), radarMat);
  dish.position.set(state.radar.x, worldY(state.radar.y) + 48, state.radar.z);
  radarGroup.add(pole);
  radarGroup.add(dish);

  targetGroup.clear();
  if (state.target) {
    const t = state.target;
    const mat = new THREE.MeshStandardMaterial({ color: 0xffcc33, roughness: 0.35 });
    const marker = new THREE.Mesh(new THREE.ConeGeometry(14, 36, 8), mat);
    marker.rotation.x = Math.PI;
    marker.position.set(t.x, worldY(t.asl) + 20, t.z);
    targetGroup.add(marker);
  }
  refreshRings();
  refreshLos();
}

function refreshLos() {
  const preset = currentPreset();
  if (!state.target) {
    losLine.visible = false;
    return;
  }
  const t = state.target;
  const result = classifyTarget(
    state.grid,
    state.meta,
    state.radar,
    preset,
    t.x,
    t.asl,
    t.z,
  );
  const pts = [new THREE.Vector3(state.radar.x, worldY(state.radar.y), state.radar.z)];
  if (result.los && !result.los.clear) {
    pts.push(new THREE.Vector3(result.los.hitX, worldY(result.los.hitY), result.los.hitZ));
    losLine.material.color.set(0xc0392b);
  } else {
    pts.push(new THREE.Vector3(t.x, worldY(t.asl), t.z));
    if (result.code === CODE_VISIBLE) {
      losLine.material.color.set(0x2ecc71);
    } else {
      losLine.material.color.set(0xf39c12);
    }
  }
  losLine.geometry.dispose();
  losLine.geometry = new THREE.BufferGeometry().setFromPoints(pts);
  losLine.visible = true;
  updateStats(result, t);
  drawRhi(preset, t);
}

function fmt(n, digits) {
  return n.toFixed(digits);
}

function updateStats(result, target) {
  const preset = currentPreset();
  document.getElementById("s-preset").textContent = preset.label;
  document.getElementById("s-rmin").textContent = fmt(result.rmin, 0) + " m";
  document.getElementById("s-range").textContent = fmt(preset.range_m, 0) + " m";
  document.getElementById("s-ground").textContent = fmt(result.groundM, 0) + " m";
  document.getElementById("s-slant").textContent = fmt(result.slantM, 0) + " m";
  document.getElementById("s-el").textContent = fmt(result.elDeg, 1) + "°";
  let beam = result.beamName;
  if (!beam) {
    beam = "—";
  }
  document.getElementById("s-beam").textContent = beam;
  document.getElementById("s-xyz").textContent =
    fmt(target.x, 0) + " / " + fmt(target.asl, 1) + " / " + fmt(target.z, 0);
  const verdict = document.getElementById("verdict");
  verdict.textContent = codeLabel(result.code) + " · " + result.reason;
  if (result.code === CODE_VISIBLE) {
    verdict.className = "ok";
  } else {
    verdict.className = "bad";
  }
}

function drawRhi(preset, target) {
  const w = rhiCanvas.width;
  const h = rhiCanvas.height;
  rhiCtx.fillStyle = "#0c1014";
  rhiCtx.fillRect(0, 0, w, h);
  const groundMax = preset.range_m;
  const aslMin = 0.0;
  const aslMax = 700.0;
  const rmin = rminM(preset);
  const bearing = Math.atan2(target.z - state.radar.z, target.x - state.radar.x);
  const samples = 220;
  const xs = [];
  const terrain = [];
  let i = 0;
  while (i < samples) {
    const r = (i / (samples - 1)) * groundMax;
    const x = state.radar.x + Math.cos(bearing) * r;
    const z = state.radar.z + Math.sin(bearing) * r;
    const samp = sampleHeight(state.grid, state.meta, x, z);
    xs.push(r);
    if (samp.ok) {
      terrain.push(samp.y);
    } else {
      terrain.push(Number.NaN);
    }
    i = i + 1;
  }

  function toX(rangeM) {
    return 36 + (rangeM / groundMax) * (w - 48);
  }
  function toY(asl) {
    return h - 22 - ((asl - aslMin) / (aslMax - aslMin)) * (h - 36);
  }

  const nx = 160;
  const ny = 80;
  const img = rhiCtx.createImageData(w, h);
  let iy = 0;
  while (iy < ny) {
    const asl = aslMin + ((iy + 0.5) / ny) * (aslMax - aslMin);
    let ix = 0;
    while (ix < nx) {
      const r = ((ix + 0.5) / nx) * groundMax;
      const ti = Math.min(samples - 1, Math.floor((r / groundMax) * (samples - 1)));
      const tAsl = terrain[ti];
      let rch = 12;
      let gch = 16;
      let bch = 20;
      if (!Number.isFinite(tAsl) || asl <= tAsl) {
        rch = 92;
        gch = 64;
        bch = 48;
      } else {
        const slant = slantRangeM(r, state.radar.y, asl);
        const el = elevationDeg(r, state.radar.y, asl);
        if (slant <= rmin) {
          rch = 192;
          gch = 48;
          bch = 42;
        } else {
          if (!inElevationBeam(preset, el)) {
            rch = 140;
            gch = 144;
            bch = 148;
          } else {
            let blocked = false;
            if (r > 1.0) {
              let j = 1;
              while (j < ti) {
                const tj = terrain[j];
                if (Number.isFinite(tj)) {
                  const u = xs[j] / r;
                  const yLos = state.radar.y + (asl - state.radar.y) * u;
                  if (tj > yLos + 2.0) {
                    blocked = true;
                    break;
                  }
                }
                j = j + 1;
              }
            }
            if (blocked) {
              rch = 44;
              gch = 62;
              bch = 80;
            } else {
              rch = 46;
              gch = 204;
              bch = 113;
            }
          }
        }
      }
      const px0 = Math.floor((ix / nx) * w);
      const px1 = Math.floor(((ix + 1) / nx) * w);
      const pyA = Math.floor(toY(asl + (aslMax - aslMin) / ny));
      const pyB = Math.floor(toY(asl));
      let py = pyA;
      let pyEnd = pyB;
      if (pyA > pyB) {
        py = pyB;
        pyEnd = pyA;
      }
      while (py < pyEnd) {
        if (py >= 0 && py < h) {
          let px = px0;
          while (px < px1) {
            if (px >= 0 && px < w) {
              const off = (py * w + px) * 4;
              img.data[off] = rch;
              img.data[off + 1] = gch;
              img.data[off + 2] = bch;
              img.data[off + 3] = 255;
            }
            px = px + 1;
          }
        }
        py = py + 1;
      }
      ix = ix + 1;
    }
    iy = iy + 1;
  }
  rhiCtx.putImageData(img, 0, 0);

  rhiCtx.strokeStyle = "#3d2914";
  rhiCtx.lineWidth = 1.5;
  rhiCtx.beginPath();
  let started = false;
  i = 0;
  while (i < samples) {
    if (Number.isFinite(terrain[i])) {
      const px = toX(xs[i]);
      const py = toY(terrain[i]);
      if (!started) {
        rhiCtx.moveTo(px, py);
        started = true;
      } else {
        rhiCtx.lineTo(px, py);
      }
    }
    i = i + 1;
  }
  rhiCtx.stroke();

  const tgtR = Math.hypot(target.x - state.radar.x, target.z - state.radar.z);
  rhiCtx.fillStyle = "#ffcc33";
  rhiCtx.beginPath();
  rhiCtx.arc(toX(tgtR), toY(target.asl), 4, 0, Math.PI * 2);
  rhiCtx.fill();
  rhiCtx.fillStyle = "#ffffff";
  rhiCtx.beginPath();
  rhiCtx.arc(toX(0), toY(state.radar.y), 3, 0, Math.PI * 2);
  rhiCtx.fill();
  rhiCtx.fillStyle = "#8b98a8";
  rhiCtx.font = "11px Segoe UI";
  rhiCtx.fillText("0", 8, h - 6);
  rhiCtx.fillText(fmt(groundMax / 1000, 0) + " km", w - 40, h - 6);
  rhiCtx.fillText("700 m ASL", 8, 12);
}

function scheduleCoverage() {
  if (!state.overlay) {
    state.codes = null;
    applyOverlayColors();
    busyEl.style.display = "none";
    return;
  }
  if (state.worker) {
    state.worker.terminate();
  }
  busyEl.style.display = "block";
  state.worker = new Worker("./coverage_worker.js", { type: "module" });
  const preset = currentPreset();
  state.worker.onmessage = function (ev) {
    if (ev.data.type === "progress") {
      busyEl.textContent = "正在计算覆盖… " + Math.round(ev.data.frac * 100) + "%";
      return;
    }
    state.codes = new Uint8Array(ev.data.codes);
    applyOverlayColors();
    busyEl.style.display = "none";
    busyEl.textContent = "正在计算覆盖…";
  };
  state.worker.postMessage({
    terrain: state.grid.terrain.buffer.slice(0),
    surface: state.grid.surface.buffer.slice(0),
    meta: state.meta,
    radar: state.radar,
    preset: preset,
    agl: state.agl,
    stride: 2,
  });
}

function placeAt(worldX, worldZ, asRadar) {
  const samp = sampleHeight(state.grid, state.meta, worldX, worldZ);
  if (!samp.ok) {
    return;
  }
  if (asRadar) {
    state.radar = { x: worldX, y: samp.y + state.meta.mast_agl_m, z: worldZ };
    refreshMarkers();
    scheduleCoverage();
    if (state.target) {
      refreshLos();
    }
    return;
  }
  state.target = { x: worldX, z: worldZ, asl: samp.y + state.agl, terrain: samp.y };
  refreshMarkers();
}

const pick = { x: 0, y: 0, active: false };

function onPointerDown(event) {
  pick.x = event.clientX;
  pick.y = event.clientY;
  pick.active = true;
}

function onPointerUp(event) {
  if (!pick.active) {
    return;
  }
  pick.active = false;
  const dx = event.clientX - pick.x;
  const dy = event.clientY - pick.y;
  if (Math.hypot(dx, dy) > 6) {
    return;
  }
  const rect = renderer.domElement.getBoundingClientRect();
  const mouse = new THREE.Vector2(
    ((event.clientX - rect.left) / rect.width) * 2 - 1,
    -((event.clientY - rect.top) / rect.height) * 2 + 1,
  );
  const raycaster = new THREE.Raycaster();
  raycaster.setFromCamera(mouse, camera);
  const hits = raycaster.intersectObject(terrainMesh);
  if (hits.length === 0) {
    return;
  }
  const p = hits[0].point;
  let asRadar = false;
  if (state.clickMode === "radar") {
    asRadar = true;
  }
  placeAt(p.x, p.z, asRadar);
}

function bindUi() {
  document.getElementById("faction-row").addEventListener("click", function (ev) {
    const btn = ev.target.closest("button");
    if (!btn) {
      return;
    }
    state.faction = btn.getAttribute("data-faction");
    setActiveButtons("faction-row", "data-faction", state.faction);
    document.getElementById("s-preset").textContent = currentPreset().label;
    refreshRings();
    refreshLos();
    scheduleCoverage();
  });
  document.getElementById("mode-row").addEventListener("click", function (ev) {
    const btn = ev.target.closest("button");
    if (!btn) {
      return;
    }
    state.mode = btn.getAttribute("data-mode");
    setActiveButtons("mode-row", "data-mode", state.mode);
    document.getElementById("s-preset").textContent = currentPreset().label;
    refreshRings();
    refreshLos();
    scheduleCoverage();
  });
  document.getElementById("click-row").addEventListener("click", function (ev) {
    const btn = ev.target.closest("button");
    if (!btn) {
      return;
    }
    state.clickMode = btn.getAttribute("data-click");
    setActiveButtons("click-row", "data-click", state.clickMode);
  });
  document.getElementById("agl").addEventListener("input", function (ev) {
    state.agl = Number(ev.target.value);
    document.getElementById("agl-val").textContent = String(state.agl);
    if (state.target) {
      state.target.asl = state.target.terrain + state.agl;
      refreshMarkers();
    }
    scheduleCoverage();
  });
  document.getElementById("exag").addEventListener("input", function (ev) {
    state.exag = Number(ev.target.value);
    document.getElementById("exag-val").textContent = String(state.exag);
    applyExaggeration();
  });
  document.getElementById("overlay").addEventListener("change", function (ev) {
    state.overlay = ev.target.checked;
    scheduleCoverage();
  });
  renderer.domElement.addEventListener("pointerdown", onPointerDown);
  renderer.domElement.addEventListener("pointerup", onPointerUp);
}

function onResize() {
  const w = viewEl.clientWidth;
  const h = Math.max(1, viewEl.clientHeight);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
  renderer.setSize(w, h);
}

function tick() {
  controls.update();
  renderer.render(scene, camera);
  requestAnimationFrame(tick);
}

async function loadData() {
  const meta = await (await fetch("./data/meta.json")).json();
  const tBuf = await (await fetch("./data/terrain.f32")).arrayBuffer();
  const sBuf = await (await fetch("./data/surface.u8")).arrayBuffer();
  state.meta = meta;
  state.grid = {
    terrain: new Float32Array(tBuf),
    surface: new Uint8Array(sBuf),
  };
  state.radar = { x: meta.radar[0], y: meta.radar[1], z: meta.radar[2] };

  const geo = buildTerrainGeometry(meta, state.grid.terrain, state.grid.surface);
  geo.userData.baseColors = new Float32Array(geo.getAttribute("color").array);
  terrainMesh.geometry = geo;
  terrainMesh.material = new THREE.MeshLambertMaterial({
    vertexColors: true,
    side: THREE.DoubleSide,
  });

  const midX = meta.origin_x + ((meta.width - 1) * meta.cell_m) / 2.0;
  const midZ = meta.origin_z + ((meta.height - 1) * meta.cell_m) / 2.0;
  camera.position.set(state.radar.x - 2800, worldY(state.radar.y) + 2200, state.radar.z + 3200);
  controls.target.set(state.radar.x, worldY(state.radar.y), state.radar.z);
  controls.update();
  refreshMarkers();
  document.getElementById("s-preset").textContent = currentPreset().label;
  document.getElementById("s-rmin").textContent = fmt(rminM(currentPreset()), 0) + " m";
  document.getElementById("s-range").textContent = fmt(currentPreset().range_m, 0) + " m";
  scheduleCoverage();
  return midX + midZ;
}

bindUi();
window.addEventListener("resize", onResize);
onResize();
tick();
loadData().catch(function (err) {
  document.getElementById("verdict").textContent =
    "无法加载 DEM。请先运行 python tools\\web_coverage\\serve.py";
  document.getElementById("verdict").className = "bad";
  console.error(err);
});
