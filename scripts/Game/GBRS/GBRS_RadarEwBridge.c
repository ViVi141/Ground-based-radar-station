// Bridges live RDF emitters into the station EwStack as noise jammers.
// Deception / RGPO / scintillation stay as separate static effects on the stack.
//
// Inherits RDF_RadarNoiseJammerEffect (not the bare RDF_RadarEwEffect) so the
// RDF 1.0.0 ECCM decision layer can observe this jammer:
//   - GetMainlobeFraction() drives SLB vs frequency-agility selection.
//   - EnableSlb() / m_EnableSlb implements sidelobe blanking on the stack.
// The bridge still aggregates live emitters in range via CollectInSphere
// (multi-source), unlike the single-position stock RDF jammer.
class GBRS_RadarEmitterNoiseBridge : RDF_RadarNoiseJammerEffect
{
	float m_MaxRangeM = 15000.0;
	// When true, use rotating-search average coupling (soft); else mainlobe+sidelobe beam.
	bool m_UseSearchAvg = true;
	// Reused CollectInSphere buffer — 20–50 ms dwells must not new the table.
	protected ref array<ref RDF_RadarScatterer> m_ScratchEntries;

	protected array<ref RDF_RadarScatterer> CollectNearby(vector radarOrigin)
	{
		if (!m_ScratchEntries)
			m_ScratchEntries = new array<ref RDF_RadarScatterer>();

		float maxRange = m_MaxRangeM;
		if (maxRange < 1.0)
			maxRange = 1.0;

		RDF_RadarScattererRegistry.CollectInSphere(radarOrigin, maxRange, m_ScratchEntries);
		return m_ScratchEntries;
	}

	override float GetAdditionalNoisePowerW(
		vector radarOrigin,
		vector scanForward,
		RDF_RadarHardware hardware)
	{
		if (!hardware)
			return 0.0;

		array<ref RDF_RadarScatterer> entries = CollectNearby(radarOrigin);
		if (!entries)
			return 0.0;

		float maxRange = m_MaxRangeM;
		if (maxRange < 1.0)
			maxRange = 1.0;
		float maxRangeSq = maxRange * maxRange;
		float sideLin = RDF_RadarClutterModel.DbToLin(m_SidelobeLevelDb);
		if (sideLin < 0.0)
			sideLin = 0.0;

		float receiverBandwidth = Math.Max(1.0, hardware.m_BandwidthHz);
		float wavelength = hardware.GetWavelengthM();
		float aperture = RDF_RadarClutterModel.DbToLin(hardware.m_AntennaGainDbi)
			* wavelength
			* wavelength
			/ RDF_RadarClutterModel.FOUR_PI;

		float total = 0.0;
		int i = 0;
		while (i < entries.Count())
		{
			RDF_RadarScatterer e = entries.Get(i);
			i = i + 1;
			if (!e || !e.m_Emitting || !e.m_Entity)
				continue;

			vector delta = e.m_Position - radarOrigin;
			float rangeSq = delta.LengthSq();
			if (rangeSq > maxRangeSq)
				continue;
			float rangeM = Math.Sqrt(rangeSq);
			// Skip co-located / own-site emissions (avoid self-jamming the PPI).
			if (rangeM < 50.0)
				continue;
			if (rangeM < 1.0)
				rangeM = 1.0;

			// Require explicit radio ERP; bare m_EmitStrength is a unitless flag.
			float erpW = e.m_EmitPeakPowerW;
			if (erpW <= 0.0)
				continue;

			if (e.m_EmitAntennaGainDbi != 0.0)
				erpW = erpW * RDF_RadarClutterModel.DbToLin(e.m_EmitAntennaGainDbi);

			float coupling = ResolveBridgeCoupling(scanForward, delta, rangeM, hardware, sideLin);
			if (coupling <= 0.0)
				continue;

			float jammerBandwidth = receiverBandwidth;
			float overlap = Math.Min(jammerBandwidth, receiverBandwidth);
			float fluxDensity = erpW / (
				RDF_RadarClutterModel.FOUR_PI
				* rangeM
				* rangeM
				* jammerBandwidth);
			total = total + fluxDensity * aperture * overlap * coupling * m_CouplingGain;
		}

		return total;
	}

	// ECCM observable: fraction of the scan where jamming couples through the
	// mainlobe. SEARCH_AVG has no instantaneous boresight → return the beam
	// duty (same contract as the stock RDF jammer). BEAM mode: 1.0 when any
	// live emitter sits inside the main beam, else 0.0.
	override float GetMainlobeFraction(
		vector radarOrigin,
		vector scanForward,
		RDF_RadarHardware hardware)
	{
		if (!hardware)
			return 0.0;

		if (m_UseSearchAvg)
		{
			float duty = m_SearchDutyOverride;
			if (duty < 0.0)
			{
				float beam = hardware.m_AzimuthBeamwidthDeg;
				if (beam < 0.1)
					beam = 0.1;
				duty = beam / 360.0;
			}
			if (duty < 0.0)
				duty = 0.0;
			if (duty > 1.0)
				duty = 1.0;
			return duty;
		}

		array<ref RDF_RadarScatterer> entries = CollectNearby(radarOrigin);
		if (!entries)
			return 0.0;

		float maxRange = m_MaxRangeM;
		if (maxRange < 1.0)
			maxRange = 1.0;
		float maxRangeSq = maxRange * maxRange;
		float halfBeamRad = hardware.m_AzimuthBeamwidthDeg * 0.5 * 0.01745329;
		float mainThreshold = Math.Cos(halfBeamRad);

		int i = 0;
		while (i < entries.Count())
		{
			RDF_RadarScatterer e = entries.Get(i);
			i = i + 1;
			if (!e || !e.m_Emitting || !e.m_Entity)
				continue;

			vector delta = e.m_Position - radarOrigin;
			float rangeSq = delta.LengthSq();
			if (rangeSq > maxRangeSq || rangeSq < 1.0)
				continue;
			if (e.m_EmitPeakPowerW <= 0.0)
				continue;

			float rangeM = Math.Sqrt(rangeSq);
			vector direction = delta * (1.0 / rangeM);
			float dot = scanForward[0] * direction[0]
				+ scanForward[1] * direction[1]
				+ scanForward[2] * direction[2];
			if (dot >= mainThreshold)
				return 1.0;
		}

		return 0.0;
	}

	// Same coupling math as before; renamed so it does not collide with the
	// stock RDF_RadarNoiseJammerEffect.ResolveCoupling(3-arg) overload.
	protected float ResolveBridgeCoupling(
		vector scanForward,
		vector delta,
		float rangeM,
		RDF_RadarHardware hardware,
		float sideLin)
	{
		if (m_UseSearchAvg)
		{
			float beam = hardware.m_AzimuthBeamwidthDeg;
			if (beam < 0.1)
				beam = 0.1;
			float duty = beam / 360.0;
			if (duty < 0.0)
				duty = 0.0;
			if (duty > 1.0)
				duty = 1.0;
			// SLB: blank the sidelobe contribution; keep only mainlobe duty.
			if (m_EnableSlb)
				return duty;
			return duty * 1.0 + (1.0 - duty) * sideLin;
		}

		vector direction = delta * (1.0 / rangeM);
		float dot = scanForward[0] * direction[0]
			+ scanForward[1] * direction[1]
			+ scanForward[2] * direction[2];
		float halfBeamRad = hardware.m_AzimuthBeamwidthDeg * 0.5 * 0.01745329;
		float mainThreshold = Math.Cos(halfBeamRad);
		if (dot >= mainThreshold)
			return 1.0;
		if (m_EnableSlb)
			return 0.0;
		return sideLin;
	}
}
