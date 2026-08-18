/** GBRS pulse / beam / LOS helpers. Mirrors tools/simulate_gbrs_coverage_3d.py */

export const C_LIGHT = 299792458.0;
export const LOS_SLACK_M = 2.0;
export const TWO_WAY_BEAM_MIN = 0.02;

export const CODE_INVALID = 0;
export const CODE_BLIND = 2;
export const CODE_SHADOW = 3;
export const CODE_BEAM = 4;
export const CODE_VISIBLE = 5;
export const CODE_RANGE = 6;

export const PRESETS = {
  us_search: {
    id: "us_search",
    faction: "US",
    mode: "search",
    label: "美军 · PD 搜索",
    range_m: 7000.0,
    pulse_width_s: 5.0e-7,
    receiver_recovery_s: 2.0e-7,
    beams: [
      { name: "low", boresight_deg: 2.0, beamwidth_deg: 16.0, relative_gain_db: 0.0 },
      { name: "mid", boresight_deg: 18.0, beamwidth_deg: 24.0, relative_gain_db: 0.0 },
      { name: "high", boresight_deg: 40.0, beamwidth_deg: 30.0, relative_gain_db: -1.0 },
    ],
  },
  ussr_search: {
    id: "ussr_search",
    faction: "USSR",
    mode: "search",
    label: "苏军 · P-18 搜索",
    range_m: 10000.0,
    pulse_width_s: 6.0e-6,
    receiver_recovery_s: 1.0e-6,
    beams: [
      { name: "low", boresight_deg: 2.0, beamwidth_deg: 16.0, relative_gain_db: 0.0 },
      { name: "mid", boresight_deg: 18.0, beamwidth_deg: 24.0, relative_gain_db: 0.0 },
      { name: "high", boresight_deg: 42.0, beamwidth_deg: 30.0, relative_gain_db: -1.0 },
    ],
  },
  us_wlr: {
    id: "us_wlr",
    faction: "US",
    mode: "wlr",
    label: "美军 · WLR 反炮兵",
    range_m: 8000.0,
    pulse_width_s: 1.0e-6,
    receiver_recovery_s: 5.0e-7,
    beams: [
      { name: "mortar_low", boresight_deg: 15.0, beamwidth_deg: 28.0, relative_gain_db: 0.0 },
      { name: "mortar_mid", boresight_deg: 35.0, beamwidth_deg: 30.0, relative_gain_db: 0.0 },
      { name: "mortar_high", boresight_deg: 55.0, beamwidth_deg: 28.0, relative_gain_db: -0.5 },
    ],
  },
  ussr_wlr: {
    id: "ussr_wlr",
    faction: "USSR",
    mode: "wlr",
    label: "苏军 · WLR 反炮兵",
    range_m: 10000.0,
    pulse_width_s: 1.0e-6,
    receiver_recovery_s: 5.0e-7,
    beams: [
      { name: "mortar_low", boresight_deg: 15.0, beamwidth_deg: 28.0, relative_gain_db: 0.0 },
      { name: "mortar_mid", boresight_deg: 35.0, beamwidth_deg: 30.0, relative_gain_db: 0.0 },
      { name: "mortar_high", boresight_deg: 55.0, beamwidth_deg: 28.0, relative_gain_db: -0.5 },
    ],
  },
};

export function rminM(preset) {
  let blank = preset.pulse_width_s;
  if (blank < 0.0) {
    blank = 0.0;
  }
  if (preset.receiver_recovery_s > 0.0) {
    blank = blank + preset.receiver_recovery_s;
  }
  return C_LIGHT * blank * 0.5;
}

export function dbToLin(db) {
  return Math.pow(10.0, db / 10.0);
}

export function gaussianBeamGain(offsetDeg, beamwidthDeg) {
  if (beamwidthDeg <= 1.0e-6) {
    return 1.0;
  }
  const halfWidth = beamwidthDeg * 0.5;
  const ratio = offsetDeg / halfWidth;
  return Math.pow(0.5, ratio * ratio);
}

export function strongestTwoWayGain(preset, elevationDeg) {
  let strongest = 0.0;
  let beamName = "";
  let i = 0;
  while (i < preset.beams.length) {
    const beam = preset.beams[i];
    const elGain = gaussianBeamGain(elevationDeg - beam.boresight_deg, beam.beamwidth_deg);
    const relative = dbToLin(beam.relative_gain_db);
    const oneWay = elGain * relative;
    const twoWay = oneWay * oneWay;
    if (twoWay > strongest) {
      strongest = twoWay;
      beamName = beam.name;
    }
    i = i + 1;
  }
  return { gain: strongest, beamName: beamName };
}

export function inElevationBeam(preset, elevationDeg) {
  const result = strongestTwoWayGain(preset, elevationDeg);
  if (result.gain >= TWO_WAY_BEAM_MIN) {
    return true;
  }
  return false;
}

export function slantRangeM(groundM, radarAsl, targetAsl) {
  const dy = targetAsl - radarAsl;
  return Math.hypot(groundM, dy);
}

export function elevationDeg(groundM, radarAsl, targetAsl) {
  let g = groundM;
  if (g < 1.0) {
    g = 1.0;
  }
  return (Math.atan2(targetAsl - radarAsl, g) * 180.0) / Math.PI;
}

export function sampleHeight(grid, meta, worldX, worldZ) {
  const lx = (worldX - meta.origin_x) / meta.cell_m;
  const lz = (worldZ - meta.origin_z) / meta.cell_m;
  const x0 = Math.floor(lx);
  const z0 = Math.floor(lz);
  if (x0 < 0 || z0 < 0 || x0 >= meta.width - 1 || z0 >= meta.height - 1) {
    return { ok: false, y: 0.0, surf: 0 };
  }
  const tx = lx - x0;
  const tz = lz - z0;
  const i00 = z0 * meta.width + x0;
  const i10 = i00 + 1;
  const i01 = i00 + meta.width;
  const i11 = i01 + 1;
  const h00 = grid.terrain[i00];
  const h10 = grid.terrain[i10];
  const h01 = grid.terrain[i01];
  const h11 = grid.terrain[i11];
  if (!Number.isFinite(h00) || !Number.isFinite(h10) || !Number.isFinite(h01) || !Number.isFinite(h11)) {
    return { ok: false, y: 0.0, surf: 0 };
  }
  const y0 = h00 * (1.0 - tx) + h10 * tx;
  const y1 = h01 * (1.0 - tx) + h11 * tx;
  const y = y0 * (1.0 - tz) + y1 * tz;
  return { ok: true, y: y, surf: grid.surface[i00] };
}

export function losProbe(grid, meta, ox, oy, oz, tx, ty, tz, stepM) {
  const dx = tx - ox;
  const dy = ty - oy;
  const dz = tz - oz;
  const dist = Math.sqrt(dx * dx + dy * dy + dz * dz);
  if (dist < 1.0) {
    return { clear: true, hitU: 1.0, hitX: tx, hitY: ty, hitZ: tz };
  }
  let step = stepM;
  if (step < 1.0) {
    step = 1.0;
  }
  const steps = Math.max(2, Math.floor(dist / step));
  let firstU = -1.0;
  let i = 1;
  while (i < steps) {
    const u = i / steps;
    const x = ox + dx * u;
    const yLos = oy + dy * u;
    const z = oz + dz * u;
    const samp = sampleHeight(grid, meta, x, z);
    if (samp.ok) {
      const hObs = samp.y - LOS_SLACK_M - yLos;
      if (hObs > 0.0 && firstU < 0.0) {
        firstU = u;
        return {
          clear: false,
          hitU: u,
          hitX: x,
          hitY: samp.y,
          hitZ: z,
        };
      }
    }
    i = i + 1;
  }
  return { clear: true, hitU: 1.0, hitX: tx, hitY: ty, hitZ: tz };
}

export function classifyTarget(grid, meta, radar, preset, tx, targetAsl, tz) {
  const rmin = rminM(preset);
  const samp = sampleHeight(grid, meta, tx, tz);
  if (!samp.ok) {
    return {
      code: CODE_INVALID,
      reason: "目标在 DEM 外",
      rmin: rmin,
      groundM: 0,
      slantM: 0,
      elDeg: 0,
      beamName: "",
      los: null,
    };
  }
  const groundM = Math.hypot(tx - radar.x, tz - radar.z);
  const slantM = slantRangeM(groundM, radar.y, targetAsl);
  const elDeg = elevationDeg(groundM, radar.y, targetAsl);
  const beam = strongestTwoWayGain(preset, elDeg);
  if (slantM > preset.range_m + 0.5) {
    return {
      code: CODE_RANGE,
      reason: "超出仪器量程",
      rmin: rmin,
      groundM: groundM,
      slantM: slantM,
      elDeg: elDeg,
      beamName: beam.beamName,
      los: null,
    };
  }
  if (slantM <= rmin) {
    return {
      code: CODE_BLIND,
      reason: "脉冲近程盲区（斜距 ≤ Rmin）",
      rmin: rmin,
      groundM: groundM,
      slantM: slantM,
      elDeg: elDeg,
      beamName: beam.beamName,
      los: null,
    };
  }
  if (beam.gain < TWO_WAY_BEAM_MIN) {
    return {
      code: CODE_BEAM,
      reason: "仰角不在波束内",
      rmin: rmin,
      groundM: groundM,
      slantM: slantM,
      elDeg: elDeg,
      beamName: beam.beamName,
      los: null,
    };
  }
  const los = losProbe(grid, meta, radar.x, radar.y, radar.z, tx, targetAsl, tz, meta.cell_m);
  if (!los.clear) {
    return {
      code: CODE_SHADOW,
      reason: "DEM 山体遮挡",
      rmin: rmin,
      groundM: groundM,
      slantM: slantM,
      elDeg: elDeg,
      beamName: beam.beamName,
      los: los,
    };
  }
  return {
    code: CODE_VISIBLE,
    reason: "可探测（通视 + 波束 + 斜距）",
    rmin: rmin,
    groundM: groundM,
    slantM: slantM,
    elDeg: elDeg,
    beamName: beam.beamName,
    los: los,
  };
}

export function codeLabel(code) {
  if (code === CODE_VISIBLE) {
    return "可探测";
  }
  if (code === CODE_BLIND) {
    return "脉冲盲区";
  }
  if (code === CODE_SHADOW) {
    return "山影";
  }
  if (code === CODE_BEAM) {
    return "波束外";
  }
  if (code === CODE_RANGE) {
    return "超距";
  }
  return "无效";
}

export function codeColor(code) {
  if (code === CODE_VISIBLE) {
    return [0.18, 0.82, 0.38];
  }
  if (code === CODE_BLIND) {
    return [0.78, 0.18, 0.18];
  }
  if (code === CODE_SHADOW) {
    return [0.18, 0.28, 0.42];
  }
  if (code === CODE_BEAM) {
    return [0.62, 0.64, 0.66];
  }
  if (code === CODE_RANGE) {
    return [0.12, 0.12, 0.14];
  }
  return [0.08, 0.08, 0.1];
}
