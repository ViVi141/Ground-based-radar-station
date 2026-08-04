// Bridges live RDF emitters into the station EwStack as noise jammers.
// Deception / RGPO / scintillation stay as separate static effects on the stack.
class GBRS_RadarEmitterNoiseBridge : RDF_RadarEwEffect
{
	float m_MaxRangeM = 15000.0;
	float m_SidelobeLevelDb = -40.0;
	float m_CouplingGain = 1.0;
	// When true, use rotating-search average coupling (soft); else mainlobe+sidelobe beam.
	bool m_UseSearchAvg = true;

	override float GetAdditionalNoisePowerW(
		vector radarOrigin,
		vector scanForward,
		RDF_RadarHardware hardware)
	{
		if (!hardware)
			return 0.0;

		array<ref RDF_RadarScatterer> entries = RDF_RadarScattererRegistry.GetEntries();
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

			float coupling = ResolveCoupling(scanForward, delta, rangeM, hardware, sideLin);
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

	protected float ResolveCoupling(
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
		return sideLin;
	}
}
