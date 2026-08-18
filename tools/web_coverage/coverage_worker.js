import {
  CODE_INVALID,
  classifyTarget,
} from "./radar.js";

self.onmessage = (event) => {
  const msg = event.data;
  const grid = {
    terrain: new Float32Array(msg.terrain),
    surface: new Uint8Array(msg.surface),
  };
  const meta = msg.meta;
  const radar = msg.radar;
  const preset = msg.preset;
  const agl = msg.agl;
  const n = meta.width;
  const stride = msg.stride;
  let step = 1;
  if (stride && stride > 1) {
    step = stride;
  }
  const codes = new Uint8Array(n * n);
  let iz = 0;
  while (iz < n) {
    let ix = 0;
    while (ix < n) {
      const idx = iz * n + ix;
      const terrainY = grid.terrain[idx];
      let code = CODE_INVALID;
      if (Number.isFinite(terrainY)) {
        const tx = meta.origin_x + ix * meta.cell_m;
        const tz = meta.origin_z + iz * meta.cell_m;
        const result = classifyTarget(
          grid,
          meta,
          radar,
          preset,
          tx,
          terrainY + agl,
          tz,
        );
        code = result.code;
      }
      let jz = iz;
      const jzEnd = Math.min(n, iz + step);
      while (jz < jzEnd) {
        let jx = ix;
        const jxEnd = Math.min(n, ix + step);
        while (jx < jxEnd) {
          codes[jz * n + jx] = code;
          jx = jx + 1;
        }
        jz = jz + 1;
      }
      ix = ix + step;
    }
    iz = iz + step;
    if (iz % 16 === 0) {
      self.postMessage({ type: "progress", frac: iz / n });
    }
  }
  self.postMessage({ type: "done", codes: codes.buffer }, [codes.buffer]);
};
