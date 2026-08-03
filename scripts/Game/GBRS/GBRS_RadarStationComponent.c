// Faction preset for ground-based radar station RDF configuration.
enum EGBRS_RadarFactionPreset
{
    US = 0,
    USSR = 1
}

// Stores one collider interaction layer so powered-on ray ignore can be restored.
class GBRS_GeomLayerBackup
{
    IEntity m_Entity;
    int m_iGeomIndex;
    int m_iLayerMask;
}

// Configures RDF radar.
// US RPL-5: stock ProcAnim on antenna_rotation bone.
// USSR TPN-19: no spin bone; yaw the radar mesh entity to match ScanRpm.
[ComponentEditorProps(category: "GameScripted/GBRS", description: "Faction radar preset, antenna spin, and powered supply drain")]
class GBRS_RadarStationComponentClass : ScriptComponentClass
{
}

class GBRS_RadarStationComponent : ScriptComponent
{
    protected static const int SUPPLY_TICK_MS_MIN = 1000;
    protected static const int ANTENNA_RESOLVE_RETRY_MS = 250;
    protected static const int ANTENNA_RESOLVE_MAX_ATTEMPTS = 20;
    // Layers RDF LOS (Projectile preset) typically hits on structure meshes.
    protected static const int TRACE_IGNORE_LAYER_MASK =
        EPhysicsLayerDefs.FireGeometry | EPhysicsLayerDefs.ViewGeometry;

    [Attribute("0", UIWidgets.ComboBox, desc: "Faction radar hardware/search preset", enums: ParamEnumArray.FromEnum(EGBRS_RadarFactionPreset))]
    protected EGBRS_RadarFactionPreset m_eFactionPreset;

    [Attribute("5", UIWidgets.EditBox, desc: "Supplies consumed from the owning base each drain tick while powered")]
    protected float m_fSupplyCostPerTick;

    [Attribute("30", UIWidgets.EditBox, desc: "Seconds between supply drain ticks while powered")]
    protected float m_fSupplyTickIntervalS;

    [Attribute("1", UIWidgets.CheckBox, desc: "If true, power-on requires enough supplies for one drain tick when a base provider is linked")]
    protected bool m_bRequireSuppliesToPowerOn;

    [Attribute("5", UIWidgets.Slider, desc: "Antenna visual pitch (deg). Detection elevation beams come from the faction preset.", params: "-5 85 0.5")]
    protected float m_fElevationBoresightDeg;

    [Attribute("12", UIWidgets.Slider, desc: "Legacy single-beam width; unused when faction dual-beam preset is active", params: "1 60 0.5")]
    protected float m_fElevationBeamwidthDeg;

    [Attribute("0", UIWidgets.CheckBox, desc: "If true, replace faction dual elevation beams with the single Attribute beam above")]
    protected bool m_bOverrideElevationBeams;

    [Attribute("0", UIWidgets.CheckBox, desc: "Draw custom scan frustum while powered")]
    protected bool m_bScanVisualEnabled;

    [Attribute("1.0", UIWidgets.Slider, desc: "Frustum length as fraction of radar range", params: "0.1 1 0.05")]
    protected float m_fVisualRangeScale;

    [Attribute("0.55", UIWidgets.Slider, desc: "Frustum edge alpha", params: "0.1 1 0.05")]
    protected float m_fVisualEdgeAlpha;

    [Attribute("0.9", UIWidgets.Slider, desc: "Frustum boresight core alpha", params: "0.1 1 0.05")]
    protected float m_fVisualCoreAlpha;

    [Attribute("0", UIWidgets.CheckBox, desc: "Print [GBRS-DEBUG] radar detection diagnostics to console")]
    protected bool m_bDebugLog;

    protected bool m_bPowered;
    protected RDF_RadarComponent m_Radar;
    protected IEntity m_AntennaEntity;
    protected ProcAnimComponent m_AntennaProcAnim;
    protected int m_iAntennaSpinBone = -1;
    protected bool m_bUseEntityYawSpin;
    protected bool m_bUseBoneSpin;
    protected bool m_bConfigured;
    protected bool m_bSupplyTickScheduled;
    protected bool m_bTraceIgnoreActive;
    protected int m_iAntennaResolveAttempts;
    protected float m_fScanRpm;
    protected string m_WorkstationMode;
    protected float m_fAntennaBasePitch;
    protected float m_fAntennaBaseRoll;
    protected float m_fVisualAzHalfDeg;
    protected float m_fVisualElHalfDeg;
    protected ref array<ref GBRS_GeomLayerBackup> m_aGeomLayerBackups;
    protected ref GBRS_RadarDetectVisual m_DetectVisual;
    protected ref GBRS_RadarDebugProbe m_DebugProbe;
    protected int m_iDebugLastScanSerial;
    protected float m_fDebugNextHeartbeatS;

    override void OnPostInit(IEntity owner)
    {
        SetEventMask(owner, EntityEvent.INIT | EntityEvent.FRAME);
    }

    override void EOnInit(IEntity owner)
    {
        super.EOnInit(owner);
        ApplyConfiguration(owner);
    }

    override void EOnFrame(IEntity owner, float timeSlice)
    {
        if (!m_bConfigured)
        {
            ApplyConfiguration(owner);
            return;
        }

        if (!m_bPowered)
            return;

        if (!m_AntennaEntity)
            EnsureAntennaResolved();

        SyncRadarBindToAntenna(owner);
        SyncAntennaBoneSpin(owner);
        SyncEntityYawAntenna(owner);
        RenderScanVisuals(owner);
        DebugScanTick(owner);
    }

    override void OnDelete(IEntity owner)
    {
        GBRS_RadarStationMenu.CloseIfBound(this);
        StopSupplyDrain();
        StopAntennaResolveRetry();
        SetAntennaSpinning(false);
        SetTraceIgnoreActive(false);
        ShutdownRadar();
        super.OnDelete(owner);
    }

    bool IsConfigured()
    {
        return m_bConfigured;
    }

    bool IsPowered()
    {
        return m_bPowered;
    }

    bool IsScanVisualEnabled()
    {
        return m_bScanVisualEnabled;
    }

    void SetScanVisualEnabled(bool enabled)
    {
        if (enabled == m_bScanVisualEnabled)
            return;

        m_bScanVisualEnabled = enabled;
        if (!enabled && m_DetectVisual)
            m_DetectVisual.Clear();
    }

    void RequestToggleScanVisual()
    {
        SetScanVisualEnabled(!m_bScanVisualEnabled);
    }

    RDF_RadarComponent GetRadarComponent()
    {
        return m_Radar;
    }

    EGBRS_RadarFactionPreset GetFactionPreset()
    {
        return m_eFactionPreset;
    }

    string GetWorkstationMode()
    {
        return m_WorkstationMode;
    }

    // Reconfigure sensor for PD SEARCH / WLR / LOCK. Returns false if unknown.
    bool ApplyWorkstationMode(string mode)
    {
        IEntity owner = GetOwner();
        if (!owner)
            return false;

        if (!m_Radar)
            m_Radar = RDF_RadarComponent.Cast(owner.FindComponent(RDF_RadarComponent));
        if (!m_Radar)
            return false;

        if (mode == "WLR")
        {
            ApplyWlrSettings(owner);
            m_WorkstationMode = "WLR";
            return true;
        }

        if (mode == "LOCK")
        {
            ApplyLockSettings(owner);
            m_WorkstationMode = "LOCK";
            return true;
        }

        if (mode == "PD SEARCH")
        {
            ApplySearchSettings(owner);
            ConfigureLockLayer(false);
            m_WorkstationMode = "PD SEARCH";
            return true;
        }

        return false;
    }

    float GetScanRpm()
    {
        return GetLiveScanRpm();
    }

    // Continuous scan boresight (same formula as RDF mechanical scan).
    vector GetScanForwardWorld()
    {
        return ComputeScanForward(GetOwner());
    }

    vector GetScanOriginWorld()
    {
        return GetAntennaBindOrigin(GetOwner());
    }

    // True when the station root or any descendant damage manager is destroyed.
    bool IsDestroyedForPpi()
    {
        IEntity owner = GetOwner();
        if (!owner)
            return true;

        return IsEntityTreeDestroyed(owner);
    }

    protected bool IsEntityTreeDestroyed(IEntity entity)
    {
        if (!entity)
            return true;

        if (IsEntityDestroyed(entity))
            return true;

        IEntity child = entity.GetChildren();
        while (child)
        {
            if (IsEntityTreeDestroyed(child))
                return true;

            child = child.GetSibling();
        }

        return false;
    }

    protected bool IsEntityDestroyed(IEntity entity)
    {
        if (!entity)
            return true;

        DamageManagerComponent damageManager =
            DamageManagerComponent.Cast(entity.FindComponent(DamageManagerComponent));
        if (!damageManager)
            return false;

        return damageManager.IsDestroyed();
    }

    bool CanAffordPowerOn()
    {
        if (!m_bRequireSuppliesToPowerOn)
            return true;

        if (!SCR_ResourceSystemHelper.IsGlobalResourceTypeEnabled(EResourceType.SUPPLIES))
            return true;

        SCR_ResourceComponent resourceComponent = ResolveSupplyResourceComponent();
        if (!resourceComponent)
            return true;

        float available;
        if (!SCR_ResourceSystemHelper.GetAvailableResources(resourceComponent, available))
            return true;

        if (available >= m_fSupplyCostPerTick)
            return true;

        return false;
    }

    void RequestTogglePower()
    {
        SetPowered(!m_bPowered);
    }

    void SetPowered(bool powered)
    {
        ApplyPoweredState(powered, false);
    }

    // Auto-test only: bypass supply affordability so Script Debugger runs work
    // without a linked base stockpile.
    void SetPoweredForAutoTest(bool powered)
    {
        ApplyPoweredState(powered, true);
    }

    protected void ApplyPoweredState(bool powered, bool ignoreSupplyGate)
    {
        if (!m_bConfigured)
            ApplyConfiguration(GetOwner());

        if (!m_Radar)
            return;

        if (powered == m_bPowered)
            return;

        if (powered)
        {
            if (!ignoreSupplyGate)
            {
                if (!CanAffordPowerOn())
                    return;
            }

            // Re-push latest GBRS preset on every power-on so script reloads
            // take effect without respawning the composition.
            ApplySearchSettings(GetOwner());
            ConfigureLockLayer(false);

            m_bPowered = true;
            RDF_RadarSensor poweredSensor = m_Radar.GetSensor();
            if (poweredSensor)
                poweredSensor.SetForceLocalScan(true);
            m_Radar.SetEnabled(true);
            EnsureAntennaResolved();
            SetAntennaSpinning(true);
            SetTraceIgnoreActive(true);
            DebugLogPowerOn();

            if (IsAuthority())
                StartSupplyDrain();
        }
        else
        {
            if (m_bDebugLog)
                Print("[GBRS-DEBUG] Power OFF", LogLevel.WARNING);

            m_bPowered = false;
            m_Radar.SetEnabled(false);
            SetAntennaSpinning(false);
            SetTraceIgnoreActive(false);
            StopSupplyDrain();
            GBRS_RadarStationMenu.CloseIfBound(this);
        }
    }

    protected void ApplyConfiguration(IEntity owner)
    {
        if (!owner)
            return;

        m_Radar = RDF_RadarComponent.Cast(owner.FindComponent(RDF_RadarComponent));
        if (!m_Radar)
            return;

        ApplySearchSettings(owner);

        m_Radar.SetEnabled(false);
        m_bPowered = false;

        m_iAntennaResolveAttempts = 0;
        EnsureAntennaResolved();
        SetAntennaSpinning(false);
        ApplyAntennaElevationVisual();

        m_bConfigured = true;

        if (m_bDebugLog)
        {
            Print("[GBRS-DEBUG] Configured faction=" + ((int)m_eFactionPreset).ToString()
                + " rpm=" + m_fScanRpm.ToString()
                + " forceLocal=1 powered=0", LogLevel.WARNING);
        }
    }

    // Push faction pulse-Doppler preset onto RDF sensor/network without changing power.
    protected void ApplySearchSettings(IEntity owner)
    {
        if (!owner)
            return;

        if (!m_Radar)
            m_Radar = RDF_RadarComponent.Cast(owner.FindComponent(RDF_RadarComponent));
        if (!m_Radar)
            return;

        RDF_RadarSettings settings;
        if (m_eFactionPreset == EGBRS_RadarFactionPreset.USSR)
            settings = GBRS_RadarStationConfig.CreateUssrSearch();
        else
            settings = GBRS_RadarStationConfig.CreateUsSearch();

        ApplyElevationToSettings(settings);

        RDF_RadarSensor sensor = m_Radar.GetSensor();
        if (!sensor)
            return;

        // Stamp product mode, then overlay station geometry/range preset.
        // Configure replaces the stock ConfigureMode settings object.
        sensor.SetForceLocalScan(true);
        m_Radar.SetMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_PULSE_DOPPLER);
        sensor.Configure(settings);
        sensor.ResetSession();
        m_Radar.SetEventMask(owner, EntityEvent.FRAME);

        RDF_RadarNetworkAPI networkApi =
            RDF_RadarNetworkAPI.Cast(owner.FindComponent(RDF_RadarNetworkAPI));
        if (networkApi)
            networkApi.SetConfig(settings);

        m_fScanRpm = 0.0;
        if (settings.m_Hardware)
            m_fScanRpm = settings.m_Hardware.m_ScanRpm;

        m_WorkstationMode = "PD SEARCH";

        if (m_bDebugLog)
        {
            string projFlag = "0";
            if (settings.m_IncludeProjectiles)
                projFlag = "1";
            string mtiMode = "off";
            if (settings.m_Hardware && settings.m_Hardware.m_EnableMti)
            {
                if (settings.m_Hardware.m_MtiMode == ERDF_MtiMode.RDF_MTI_MTD_BANK)
                    mtiMode = "MTD";
                else if (settings.m_Hardware.m_MtiMode == ERDF_MtiMode.RDF_MTI_THREE_PULSE)
                    mtiMode = "3P";
                else
                    mtiMode = "2P";
            }
            Print("[GBRS-DEBUG] PdSettings mode=" + ((int)sensor.GetMode()).ToString()
                + " range=" + settings.m_Range.ToString()
                + " interval=" + settings.m_UpdateInterval.ToString()
                + " minDist=" + settings.m_MinDistance.ToString()
                + " projectiles=" + projFlag
                + " mti=" + mtiMode
                + " coherent=" + BoolDebugFlag(settings.m_Hardware && settings.m_Hardware.m_CoherentIntegration)
                + " demClutter=" + BoolDebugFlag(settings.m_EnableDemClutter)
                + " clutterMap=" + BoolDebugFlag(settings.m_EnableClutterMap)
                + " coast=" + BoolDebugFlag(settings.m_TrackCoastOnMiss)
                + " cfar=" + BoolDebugFlag(settings.m_EnableCfarGate)
                + " nlos=" + BoolDebugFlag(settings.m_EnableNlosMultipath)
                + " snrGate=" + settings.m_DetectionSnrDb.ToString(), LogLevel.WARNING);
        }
    }

    // Counter-battery WLR: projectile-only stare with launch/impact solves.
    protected void ApplyWlrSettings(IEntity owner)
    {
        if (!owner)
            return;

        if (!m_Radar)
            m_Radar = RDF_RadarComponent.Cast(owner.FindComponent(RDF_RadarComponent));
        if (!m_Radar)
            return;

        RDF_RadarSettings settings;
        if (m_eFactionPreset == EGBRS_RadarFactionPreset.USSR)
            settings = GBRS_RadarStationConfig.CreateUssrWlr();
        else
            settings = GBRS_RadarStationConfig.CreateUsWlr();

        // Keep mortar elevation layout; do not overlay air-search beam overrides.
        PushSensorSettings(owner, settings, ERDF_RadarSensorMode.RDF_RADAR_MODE_WLR);
        ConfigureLockLayer(false);

        if (m_bDebugLog)
        {
            Print("[GBRS-DEBUG] WlrSettings range=" + settings.m_Range.ToString()
                + " projectiles=1 wlr=1", LogLevel.WARNING);
        }
    }

    // Air search PD plus lock-manager auto-acquire for vehicles.
    protected void ApplyLockSettings(IEntity owner)
    {
        ApplySearchSettings(owner);
        ConfigureLockLayer(true);
    }

    protected void PushSensorSettings(
        IEntity owner,
        RDF_RadarSettings settings,
        ERDF_RadarSensorMode mode)
    {
        if (!owner || !settings || !m_Radar)
            return;

        RDF_RadarSensor sensor = m_Radar.GetSensor();
        if (!sensor)
            return;

        sensor.SetForceLocalScan(true);
        m_Radar.SetMode(mode);
        sensor.Configure(settings);
        sensor.ResetSession();
        m_Radar.SetEventMask(owner, EntityEvent.FRAME);

        RDF_RadarNetworkAPI networkApi =
            RDF_RadarNetworkAPI.Cast(owner.FindComponent(RDF_RadarNetworkAPI));
        if (networkApi)
            networkApi.SetConfig(settings);

        m_fScanRpm = 0.0;
        if (settings.m_Hardware)
            m_fScanRpm = settings.m_Hardware.m_ScanRpm;
    }

    // When enabled: auto-acquire vehicles. When disabled: clear any held lock.
    protected void ConfigureLockLayer(bool enableAutoLock)
    {
        if (!m_Radar)
            return;

        RDF_RadarSensor sensor = m_Radar.GetSensor();
        if (!sensor)
            return;

        RDF_RadarLockManager lockMgr = sensor.GetLockManager();
        if (!lockMgr)
            return;

        if (!enableAutoLock)
        {
            lockMgr.SetAutoAcquire(false);
            lockMgr.Unlock();
            return;
        }

        RDF_RadarSettings settings = sensor.GetSettings();
        float maxRange = 3000.0;
        if (settings && settings.m_Range > 0.0)
            maxRange = settings.m_Range;

        lockMgr.Unlock();
        lockMgr.SetAutoAcquire(true);
        lockMgr.SetTypeFilter(true, false, false);
        lockMgr.SetMaxLockRange(maxRange);
        lockMgr.SetLockSector(0.0);
        lockMgr.SetAcquireHits(2);
        lockMgr.SetCoastMaxSec(3.0);
    }

    protected void ApplyElevationToSettings(RDF_RadarSettings settings)
    {
        if (!settings)
            return;

        if (m_fElevationBoresightDeg < -5.0)
            m_fElevationBoresightDeg = -5.0;
        if (m_fElevationBoresightDeg > 85.0)
            m_fElevationBoresightDeg = 85.0;

        if (m_fElevationBeamwidthDeg < 1.0)
            m_fElevationBeamwidthDeg = 1.0;
        if (m_fElevationBeamwidthDeg > 60.0)
            m_fElevationBeamwidthDeg = 60.0;

        if (!settings.m_Hardware)
            return;

        if (m_bOverrideElevationBeams)
        {
            settings.m_Hardware.ClearElevationBeams();
            settings.m_Hardware.AddElevationBeam(
                "main",
                m_fElevationBoresightDeg,
                m_fElevationBeamwidthDeg,
                0.0);
        }
        else
        {
            // Keep faction dual-beam layout from GBRS_RadarStationConfig.
            // Antenna visual pitch follows the primary (first) beam.
            if (settings.m_Hardware.m_ElevationBeams)
            {
                if (settings.m_Hardware.m_ElevationBeams.Count() > 0)
                {
                    RDF_RadarElevationBeam primaryBeam =
                        settings.m_Hardware.m_ElevationBeams.Get(0);
                    if (primaryBeam)
                    {
                        m_fElevationBoresightDeg = primaryBeam.m_BoresightDeg;
                        m_fVisualElHalfDeg = primaryBeam.m_BeamwidthDeg * 0.5;
                    }
                }
            }
        }

        if (settings.m_Hardware)
        {
            m_fVisualAzHalfDeg = settings.m_Hardware.m_AzimuthBeamwidthDeg * 0.5;
            if (m_bOverrideElevationBeams)
                m_fVisualElHalfDeg = m_fElevationBeamwidthDeg * 0.5;
        }

        if (m_fVisualAzHalfDeg < 0.5)
            m_fVisualAzHalfDeg = 0.5;
        if (m_fVisualElHalfDeg < 0.5)
            m_fVisualElHalfDeg = 0.5;

        settings.m_Hardware.Validate();
        settings.Validate();
    }

    protected void EnsureAntennaResolved()
    {
        ResolveAntennaEntity(GetOwner());
        if (m_AntennaEntity)
        {
            SyncRadarBindToAntenna(GetOwner());
            SetAntennaSpinning(m_bPowered);
            ApplyAntennaElevationVisual();
            return;
        }

        if (m_iAntennaResolveAttempts >= ANTENNA_RESOLVE_MAX_ATTEMPTS)
            return;

        if (m_iAntennaResolveAttempts == 0)
            GetGame().GetCallqueue().CallLater(RetryResolveAntenna, ANTENNA_RESOLVE_RETRY_MS, true);

        m_iAntennaResolveAttempts = m_iAntennaResolveAttempts + 1;
    }

    protected void RetryResolveAntenna()
    {
        ResolveAntennaEntity(GetOwner());
        if (m_AntennaEntity)
        {
            // Always force the powered spin state; stock RPL-5_on can start ProcAnim on spawn.
            SyncRadarBindToAntenna(GetOwner());
            SetAntennaSpinning(m_bPowered);
            ApplyAntennaElevationVisual();
            if (m_bPowered)
                SetTraceIgnoreActive(true);

            if (!m_bPowered)
            {
                // Keep suppressing auto-spin for a few more ticks after late EditorLink spawn.
                if (m_iAntennaResolveAttempts < ANTENNA_RESOLVE_MAX_ATTEMPTS)
                {
                    m_iAntennaResolveAttempts = m_iAntennaResolveAttempts + 1;
                    return;
                }
            }

            StopAntennaResolveRetry();
            return;
        }

        m_iAntennaResolveAttempts = m_iAntennaResolveAttempts + 1;
        if (m_iAntennaResolveAttempts >= ANTENNA_RESOLVE_MAX_ATTEMPTS)
            StopAntennaResolveRetry();
    }

    protected void StopAntennaResolveRetry()
    {
        GetGame().GetCallqueue().Remove(RetryResolveAntenna);
        m_iAntennaResolveAttempts = ANTENNA_RESOLVE_MAX_ATTEMPTS;
    }

    // US: RPL-5 antenna entity — spin antenna_rotation bone via Animation.SetBone.
    // USSR: first mesh child that is not the generator (TPN-19 has no spin bone).
    protected void ResolveAntennaEntity(IEntity owner)
    {
        m_AntennaEntity = null;
        m_AntennaProcAnim = null;
        m_iAntennaSpinBone = -1;
        m_bUseEntityYawSpin = false;
        m_bUseBoneSpin = false;
        m_fAntennaBasePitch = 0.0;
        m_fAntennaBaseRoll = 0.0;
        if (!owner)
            return;

        if (FindUsAntenna(owner))
            return;

        // US must stay on bone spin only — never yaw the whole pedestal mesh.
        if (m_eFactionPreset == EGBRS_RadarFactionPreset.US)
            return;

        IEntity child = owner.GetChildren();
        while (child)
        {
            if (child.GetVObject() && !IsGeneratorChild(child))
            {
                m_AntennaEntity = child;
                m_bUseEntityYawSpin = true;
                vector ypr = child.GetYawPitchRoll();
                m_fAntennaBasePitch = ypr[1];
                m_fAntennaBaseRoll = ypr[2];
                return;
            }

            child = child.GetSibling();
        }
    }

    // Depth-first: composition root, EditorLink children, and nested parts.
    protected bool FindUsAntenna(IEntity root)
    {
        if (!root)
            return false;

        IEntity child = root.GetChildren();
        while (child)
        {
            if (TryBindUsAntenna(child))
                return true;

            if (FindUsAntenna(child))
                return true;

            child = child.GetSibling();
        }

        return false;
    }

    protected bool TryBindUsAntenna(IEntity ent)
    {
        if (!ent)
            return false;

        Animation anim = ent.GetAnimation();
        if (!anim)
            return false;

        int bone = anim.GetBoneIndex("antenna_rotation");
        if (bone < 0)
            return false;

        m_AntennaEntity = ent;
        m_iAntennaSpinBone = bone;
        m_bUseBoneSpin = true;
        m_bUseEntityYawSpin = false;

        // Prefer entity that also has ProcAnim (stock RPL-5), then keep it off.
        m_AntennaProcAnim = ProcAnimComponent.Cast(ent.FindComponent(ProcAnimComponent));
        if (m_AntennaProcAnim && m_AntennaProcAnim.IsActive())
            m_AntennaProcAnim.Deactivate(ent);

        return true;
    }

    protected bool IsGeneratorChild(IEntity child)
    {
        if (!child)
            return false;

        EntityPrefabData prefabData = child.GetPrefabData();
        if (!prefabData)
            return false;

        ResourceName prefabName = prefabData.GetPrefabName();
        string path = prefabName;
        path.ToLower();
        if (path.Contains("generator"))
            return true;

        return false;
    }

    protected void SetAntennaSpinning(bool spinning)
    {
        if (!m_AntennaEntity)
            return;

        // Stock ProcAnim fights script bone spin and is Enabled 0 in our prefab.
        if (m_AntennaProcAnim)
        {
            if (m_AntennaProcAnim.IsActive())
                m_AntennaProcAnim.Deactivate(m_AntennaEntity);
        }

        if (!spinning)
        {
            if (m_bUseBoneSpin)
                ResetAntennaBone();
            else if (m_bUseEntityYawSpin)
            {
                m_AntennaEntity.SetYawPitchRoll(
                    Vector(0.0, m_fElevationBoresightDeg, m_fAntennaBaseRoll));
            }
        }
    }

    protected void ResetAntennaBone()
    {
        if (!m_AntennaEntity || m_iAntennaSpinBone < 0)
            return;

        Animation anim = m_AntennaEntity.GetAnimation();
        if (!anim)
            return;

        vector rot[3];
        Math3D.MatrixIdentity3(rot);
        vector boneMat[4];
        boneMat[0] = rot[0];
        boneMat[1] = rot[1];
        boneMat[2] = rot[2];
        boneMat[3] = "0 0 0";
        anim.SetBoneMatrix(m_AntennaEntity, m_iAntennaSpinBone, boneMat);
    }

    // RPL-5: drive antenna_rotation bone (pedestal stays fixed).
    // Use SetBoneMatrix + AnglesToMatrix (degrees) so rate matches RDF ScanRpm.
    // ProcAnim is forced off every frame — stock pap would otherwise spin faster.
    protected void SyncAntennaBoneSpin(IEntity owner)
    {
        if (!m_bUseBoneSpin || !m_AntennaEntity)
            return;

        if (m_iAntennaSpinBone < 0)
            return;

        float rpm = GetLiveScanRpm();
        if (rpm <= 0.0)
            return;

        if (m_AntennaProcAnim && m_AntennaProcAnim.IsActive())
            m_AntennaProcAnim.Deactivate(m_AntennaEntity);

        Animation anim = m_AntennaEntity.GetAnimation();
        if (!anim)
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return;

        // RDF angle a: forward=(cos a, 0, sin a). Enfusion yaw 0 = +Z, so yaw = 90 - a_deg.
        float worldTimeS = world.GetWorldTime() * 0.001;
        float angleDeg = worldTimeS * rpm * 6.0;
        float worldYawDeg = 90.0 - angleDeg;
        vector antYpr = m_AntennaEntity.GetYawPitchRoll();
        float localYawDeg = worldYawDeg - antYpr[0];

        vector rot[3];
        Math3D.AnglesToMatrix(Vector(localYawDeg, 0.0, 0.0), rot);

        vector boneMat[4];
        boneMat[0] = rot[0];
        boneMat[1] = rot[1];
        boneMat[2] = rot[2];
        boneMat[3] = "0 0 0";
        anim.SetBoneMatrix(m_AntennaEntity, m_iAntennaSpinBone, boneMat);
    }

    protected float GetLiveScanRpm()
    {
        if (m_Radar)
        {
            RDF_RadarSettings settings = m_Radar.GetSettings();
            if (settings && settings.m_Hardware && settings.m_Hardware.m_ScanRpm > 0.0)
                return settings.m_Hardware.m_ScanRpm;
        }

        return m_fScanRpm;
    }

    // RPL-5: never yaw/pitch the whole ProcAnim entity — that rotates the pedestal.
    protected void ApplyAntennaElevationVisual()
    {
    }

    // Bind RDF scan origin to antenna mesh center (composition root owns RDF_RadarComponent).
    protected void SyncRadarBindToAntenna(IEntity owner)
    {
        if (!m_Radar || !owner)
            return;

        RDF_RadarSettings settings = m_Radar.GetSettings();
        if (!settings)
            return;

        settings.m_UseBoundsCenter = false;
        settings.m_UseLocalOffset = false;
        settings.m_OriginOffset = GetAntennaBindOrigin(owner) - owner.GetOrigin();
    }

    protected vector GetAntennaBindOrigin(IEntity owner)
    {
        if (!m_AntennaEntity)
        {
            if (owner)
                return owner.GetOrigin() + Vector(0.0, 3.0, 0.0);
            return "0 0 0";
        }

        vector mins;
        vector maxs;
        m_AntennaEntity.GetBounds(mins, maxs);
        vector mat[4];
        m_AntennaEntity.GetWorldTransform(mat);
        vector centerLocal = (mins + maxs) * 0.5;
        return mat[3]
            + (mat[0] * centerLocal[0])
            + (mat[1] * centerLocal[1])
            + (mat[2] * centerLocal[2]);
    }

    protected vector ComputeScanForward(IEntity owner)
    {
        float rpm = GetLiveScanRpm();
        if (rpm > 0.0)
        {
            BaseWorld world = GetGame().GetWorld();
            if (world)
            {
                // Must match RDF_RadarScanner.GetScanForward exactly.
                float worldTimeS = world.GetWorldTime() * 0.001;
                float angleRad = worldTimeS * rpm * Math.PI * 2.0 / 60.0;
                return Vector(Math.Cos(angleRad), 0.0, Math.Sin(angleRad));
            }
        }

        if (m_AntennaEntity)
        {
            vector mat[4];
            m_AntennaEntity.GetWorldTransform(mat);
            vector fwd = mat[2];
            float len = fwd.Length();
            if (len > 0.001)
                return fwd * (1.0 / len);
        }

        if (owner)
        {
            vector matOwner[4];
            owner.GetWorldTransform(matOwner);
            vector ownerFwd = matOwner[2];
            float ownerLen = ownerFwd.Length();
            if (ownerLen > 0.001)
                return ownerFwd * (1.0 / ownerLen);
        }

        return Vector(1.0, 0.0, 0.0);
    }

    protected void RenderScanVisuals(IEntity owner)
    {
        if (!m_bScanVisualEnabled)
            return;

        if (!m_Radar)
            return;

        RDF_RadarSensor sensor = m_Radar.GetSensor();
        if (!sensor || !sensor.IsEnabled())
            return;

        vector origin = GetAntennaBindOrigin(owner);
        // Prefer continuous ScanRpm forward — sensor context only updates on dwell ticks.
        vector forward = ComputeScanForward(owner);

        RDF_RadarScanContext ctx = sensor.GetScanContext();
        if (ctx)
        {
            if (ctx.m_Origin.LengthSq() > 0.0001)
                origin = ctx.m_Origin;
        }

        float rangeM = 2000.0;
        RDF_RadarSettings cfg = sensor.GetSettings();
        if (cfg && cfg.m_Range > 0.0)
            rangeM = cfg.m_Range;

        float scale = m_fVisualRangeScale;
        if (scale < 0.1)
            scale = 0.1;
        if (scale > 1.0)
            scale = 1.0;
        rangeM = rangeM * scale;

        int edgeA = Math.Round(m_fVisualEdgeAlpha * 255.0);
        if (edgeA < 20)
            edgeA = 20;
        if (edgeA > 255)
            edgeA = 255;

        int coreA = Math.Round(m_fVisualCoreAlpha * 255.0);
        if (coreA < 20)
            coreA = 20;
        if (coreA > 255)
            coreA = 255;

        int colourEdge = ARGB(edgeA, 80, 255, 140);
        int colourCore = ARGB(coreA, 180, 255, 80);

        GBRS_RadarScanFrustumVisual.Draw(
            origin,
            forward,
            rangeM,
            m_fVisualAzHalfDeg,
            m_fElevationBoresightDeg,
            m_fVisualElHalfDeg,
            colourEdge,
            colourCore);

        if (!m_DetectVisual)
            m_DetectVisual = new GBRS_RadarDetectVisual();

        float nowS = System.GetTickCount() * 0.001;
        m_DetectVisual.Ingest(sensor.GetPlots(), origin, nowS);
        m_DetectVisual.Draw(origin, GetLiveScanRpm(), nowS);
    }

    protected void DebugLogPowerOn()
    {
        if (!m_bDebugLog)
            return;

        RDF_RadarSensor sensor = null;
        if (m_Radar)
            sensor = m_Radar.GetSensor();

        RDF_RadarSettings settings = null;
        if (sensor)
            settings = sensor.GetSettings();

        Print("[GBRS-DEBUG] ========== POWER ON ==========", LogLevel.WARNING);
        Print("[GBRS-DEBUG] faction=" + ((int)m_eFactionPreset).ToString()
            + " hasAntenna=" + HasAntennaDebugFlag()
            + " boneSpin=" + BoolDebugFlag(m_bUseBoneSpin)
            + " entityYaw=" + BoolDebugFlag(m_bUseEntityYawSpin), LogLevel.WARNING);

        if (sensor)
        {
            Print("[GBRS-DEBUG] sensor.enabled=" + BoolDebugFlag(sensor.IsEnabled())
                + " forceLocal=" + BoolDebugFlag(sensor.IsForceLocalScan())
                + " scanSerial=" + sensor.GetScanSerial().ToString(), LogLevel.WARNING);
        }
        else
        {
            Print("[GBRS-DEBUG] ERROR: sensor is null", LogLevel.ERROR);
        }

        if (settings)
        {
            float azBw = 0.0;
            float rpm = 0.0;
            bool mti = false;
            int dopplerBins = 0;
            int elBeams = 0;
            string mtiMode = "off";
            if (settings.m_Hardware)
            {
                azBw = settings.m_Hardware.m_AzimuthBeamwidthDeg;
                rpm = settings.m_Hardware.m_ScanRpm;
                mti = settings.m_Hardware.m_EnableMti;
                dopplerBins = settings.m_Hardware.m_DopplerBinCount;
                if (settings.m_Hardware.m_ElevationBeams)
                    elBeams = settings.m_Hardware.m_ElevationBeams.Count();
                if (mti)
                {
                    if (settings.m_Hardware.m_MtiMode == ERDF_MtiMode.RDF_MTI_MTD_BANK)
                        mtiMode = "MTD";
                    else if (settings.m_Hardware.m_MtiMode == ERDF_MtiMode.RDF_MTI_THREE_PULSE)
                        mtiMode = "3P";
                    else
                        mtiMode = "2P";
                }
            }

            Print("[GBRS-DEBUG] range=" + settings.m_Range.ToString()
                + " interval=" + settings.m_UpdateInterval.ToString()
                + " mechScan=" + BoolDebugFlag(settings.m_EnableMechanicalScan)
                + " discInt=" + settings.m_ScattererDiscoveryIntervalS.ToString()
                + " maxEnt=" + settings.m_ScattererMaxEntries.ToString()
                + " cfar=" + BoolDebugFlag(settings.m_EnableCfarGate)
                + " snrDb=" + settings.m_DetectionSnrDb.ToString(), LogLevel.WARNING);
            Print("[GBRS-DEBUG] rpm=" + rpm.ToString()
                + " azBeamW=" + azBw.ToString()
                + " coneHalf=" + (azBw * 0.5).ToString()
                + " mti=" + BoolDebugFlag(mti)
                + " mtiMode=" + mtiMode
                + " dopBins=" + dopplerBins.ToString()
                + " clutterMap=" + BoolDebugFlag(settings.m_EnableClutterMap)
                + " coast=" + BoolDebugFlag(settings.m_TrackCoastOnMiss)
                + " elBeams=" + elBeams.ToString()
                + " maxLos=" + settings.m_MaxLosTracesPerScan.ToString(), LogLevel.WARNING);
            Print("[GBRS-DEBUG] originOffset=" + settings.m_OriginOffset.ToString()
                + " useLocalOffset=" + BoolDebugFlag(settings.m_UseLocalOffset)
                + " useBoundsCenter=" + BoolDebugFlag(settings.m_UseBoundsCenter), LogLevel.WARNING);
        }

        m_iDebugLastScanSerial = -1;
        m_fDebugNextHeartbeatS = 0.0;
    }

    protected string BoolDebugFlag(bool value)
    {
        if (value)
            return "1";
        return "0";
    }

    protected string HasAntennaDebugFlag()
    {
        if (m_AntennaEntity)
            return "1";
        return "0";
    }

    protected void DebugScanTick(IEntity owner)
    {
        if (!m_bDebugLog)
            return;

        if (!m_Radar)
            return;

        RDF_RadarSensor sensor = m_Radar.GetSensor();
        if (!sensor)
            return;

        float nowS = System.GetTickCount() * 0.001;
        int serial = sensor.GetScanSerial();
        bool newScan = serial != m_iDebugLastScanSerial;
        bool heartbeat = nowS >= m_fDebugNextHeartbeatS;
        if (!newScan && !heartbeat)
            return;

        if (heartbeat)
            m_fDebugNextHeartbeatS = nowS + 2.0;

        if (newScan)
            m_iDebugLastScanSerial = serial;

        RDF_RadarSettings settings = sensor.GetSettings();
        RDF_RadarScanContext ctx = sensor.GetScanContext();
        array<ref RDF_RadarTarget> plots = sensor.GetPlots();

        vector origin = GetAntennaBindOrigin(owner);
        vector forward = ComputeScanForward(owner);
        float rangeM = 2000.0;
        float coneHalf = m_fVisualAzHalfDeg;
        float minDist = 5.0;
        if (settings)
        {
            if (settings.m_Range > 0.0)
                rangeM = settings.m_Range;
            minDist = settings.m_MinDistance;
            if (settings.m_Hardware)
                coneHalf = settings.m_Hardware.m_AzimuthBeamwidthDeg * 0.5;
        }

        if (ctx)
        {
            if (ctx.m_Origin.LengthSq() > 0.0001)
                origin = ctx.m_Origin;
            if (ctx.m_Forward.LengthSq() > 0.0001)
                forward = ctx.m_Forward;
            if (ctx.m_RangeM > 0.0)
                rangeM = ctx.m_RangeM;
        }

        int plotCount = 0;
        int detectedCount = 0;
        int losBlockedCount = 0;
        float bestSnr = -999.0;
        float bestDist = -1.0;
        string bestBeam = "-";
        if (plots)
        {
            plotCount = plots.Count();
            int i = 0;
            while (i < plots.Count())
            {
                RDF_RadarTarget t = plots.Get(i);
                i = i + 1;
                if (!t)
                    continue;
                if (t.m_Detected)
                    detectedCount = detectedCount + 1;
                if (t.m_LosBlocked)
                    losBlockedCount = losBlockedCount + 1;
                if (t.m_SnrDb > bestSnr)
                {
                    bestSnr = t.m_SnrDb;
                    bestDist = t.m_Distance;
                    bestBeam = t.m_BeamName;
                }
            }
        }

        int trackCount = 0;
        array<ref RDF_RadarTrack> tracks = sensor.GetTracks();
        if (tracks)
            trackCount = tracks.Count();

        if (!m_DebugProbe)
            m_DebugProbe = new GBRS_RadarDebugProbe();

        m_DebugProbe.Reset(origin, forward, coneHalf, rangeM, minDist, owner);

        // Probe the same source the scanner uses (scatterer registry), not
        // GetActiveEntities — streamed/editor aircraft often miss that list and
        // falsely print "no Vehicle in range" while scat>0.
        int registryScat = RDF_RadarScattererRegistry.GetEntryCount();
        array<ref RDF_RadarScatterer> nearby = new array<ref RDF_RadarScatterer>();
        RDF_RadarScattererRegistry.CollectInSphere(origin, rangeM, nearby);
        int ni = 0;
        while (ni < nearby.Count())
        {
            RDF_RadarScatterer entry = nearby.Get(ni);
            ni = ni + 1;
            if (!entry)
                continue;
            if (!entry.m_Alive)
                continue;
            if (!entry.m_Entity)
                continue;
            m_DebugProbe.CollectEntity(entry.m_Entity);
        }

        string tag = "heartbeat";
        if (newScan)
            tag = "scan";

        Print("[GBRS-DEBUG] --- " + tag
            + " serial=" + serial.ToString()
            + " enabled=" + BoolDebugFlag(sensor.IsEnabled())
            + " forceLocal=" + BoolDebugFlag(sensor.IsForceLocalScan())
            + " durMs=" + sensor.GetLastScanDurationMs().ToString()
            + " ---", LogLevel.WARNING);
        Print("[GBRS-DEBUG] status=" + sensor.GetStatusShort(), LogLevel.WARNING);

        Print("[GBRS-DEBUG] origin=" + origin.ToString()
            + " forward=" + forward.ToString()
            + " coneHalfDeg=" + coneHalf.ToString()
            + " range=" + rangeM.ToString(), LogLevel.WARNING);

        Print("[GBRS-DEBUG] plots=" + plotCount.ToString()
            + " detected=" + detectedCount.ToString()
            + " losBlocked=" + losBlockedCount.ToString()
            + " tracks=" + trackCount.ToString()
            + " bestSnrDb=" + bestSnr.ToString()
            + " bestDist=" + bestDist.ToString()
            + " bestBeam=" + bestBeam, LogLevel.WARNING);

        // Live RDF physics samples (parity check vs offline calib_pd_full.py).
        if (plots)
        {
            int shown = 0;
            int piPhys = 0;
            while (piPhys < plots.Count() && shown < 3)
            {
                RDF_RadarTarget pt = plots.Get(piPhys);
                piPhys = piPhys + 1;
                if (!pt || !pt.m_Detected)
                    continue;
                Print("[GBRS-DEBUG] plotPhys dist=" + pt.m_Distance.ToString()
                    + " snrDb=" + pt.m_SnrDb.ToString()
                    + " mtiG=" + pt.m_MtiGain.ToString()
                    + " dopBin=" + pt.m_DopplerBin.ToString()
                    + " clutterW=" + pt.m_ClutterPowerW.ToString()
                    + " cnrDb=" + pt.m_ClutterToNoiseDb.ToString()
                    + " losBlk=" + BoolDebugFlag(pt.m_LosBlocked)
                    + " mp=" + pt.m_MultipathFactor.ToString()
                    + " beam=" + pt.m_BeamName, LogLevel.WARNING);
                shown = shown + 1;
            }
        }

        Print("[GBRS-DEBUG] registry=" + RDF_RadarScattererRegistry.GetStatsLine(), LogLevel.WARNING);

        string nearDistStr = "none";
        if (m_DebugProbe.m_CandidateCount > 0)
            nearDistStr = m_DebugProbe.m_NearestDist.ToString();

        Print("[GBRS-DEBUG] regCandidates=" + m_DebugProbe.m_CandidateCount.ToString()
            + " inCone=" + m_DebugProbe.m_InConeCount.ToString()
            + " nearestDist=" + nearDistStr
            + " nearestDot=" + m_DebugProbe.m_NearestDot.ToString()
            + " nearestElDeg=" + m_DebugProbe.m_NearestElDeg.ToString()
            + " type=" + m_DebugProbe.m_NearestType
            + " name=" + m_DebugProbe.m_NearestName, LogLevel.WARNING);

        string nearVehDistStr = "none";
        if (m_DebugProbe.m_VehicleCount > 0)
            nearVehDistStr = m_DebugProbe.m_NearestVehicleDist.ToString();

        Print("[GBRS-DEBUG] vehicles=" + m_DebugProbe.m_VehicleCount.ToString()
            + " vehInCone=" + m_DebugProbe.m_VehicleInConeCount.ToString()
            + " nearestVehDist=" + nearVehDistStr
            + " nearestVehDot=" + m_DebugProbe.m_NearestVehicleDot.ToString()
            + " nearestVehElDeg=" + m_DebugProbe.m_NearestVehicleElDeg.ToString()
            + " nearestVeh=" + m_DebugProbe.m_NearestVehicleName, LogLevel.WARNING);

        if (m_DebugProbe.m_CandidateCount > 0)
        {
            float needDot = Math.Cos(coneHalf * 0.017453292519943295);
            if (m_DebugProbe.m_VehicleCount <= 0)
            {
                Print("[GBRS-DEBUG] HINT: no Vehicle airframe in range (mounted rockets"
                    + " / characters are NOT the aircraft). Fly a heli 100m+ out.",
                    LogLevel.WARNING);
            }
            else if (m_DebugProbe.m_VehicleInConeCount <= 0)
            {
                Print("[GBRS-DEBUG] HINT: vehicle in range but outside azimuth beam this dwell"
                    + " (vehDot=" + m_DebugProbe.m_NearestVehicleDot.ToString()
                    + " need>=" + needDot.ToString()
                    + " elDeg=" + m_DebugProbe.m_NearestVehicleElDeg.ToString()
                    + " name=" + m_DebugProbe.m_NearestVehicleName + ").",
                    LogLevel.WARNING);
            }
            else if (plotCount == 0)
            {
                Print("[GBRS-DEBUG] HINT: vehInCone>0 but plots=0"
                    + " (hard LOS blocked / SNR / minDist)."
                    + " nearestVeh=" + m_DebugProbe.m_NearestVehicleName, LogLevel.WARNING);
            }
            else if (detectedCount == 0)
            {
                Print("[GBRS-DEBUG] HINT: plots exist but detected=0 (SNR/CFAR)."
                    + " bestSnrDb=" + bestSnr.ToString(), LogLevel.WARNING);
            }
        }
        else if (registryScat <= 0)
        {
            Print("[GBRS-DEBUG] HINT: registry empty (scat=0). Wait for discovery"
                + " or use a lasting Vehicle. GM free-camera is NOT a radar target.",
                LogLevel.WARNING);
        }
        else
        {
            Print("[GBRS-DEBUG] HINT: registry scat=" + registryScat.ToString()
                + " but none within this station range sphere ("
                + rangeM.ToString() + " m).", LogLevel.WARNING);
        }
    }

    // While powered, strip Fire/View geometry layers so RDF Projectile LOS and
    // similar traces ignore this station and all linked parts.
    protected void SetTraceIgnoreActive(bool active)
    {
        if (active)
        {
            if (m_bTraceIgnoreActive)
                RestoreTraceLayers();

            BackupAndClearTraceLayers();
            m_bTraceIgnoreActive = true;
            return;
        }

        if (!m_bTraceIgnoreActive)
            return;

        RestoreTraceLayers();
        m_bTraceIgnoreActive = false;
    }

    protected void BackupAndClearTraceLayers()
    {
        if (!m_aGeomLayerBackups)
            m_aGeomLayerBackups = new array<ref GBRS_GeomLayerBackup>();
        else
            m_aGeomLayerBackups.Clear();

        IEntity owner = GetOwner();
        if (!owner)
            return;

        array<IEntity> entities = {};
        CollectHierarchyEntities(owner, entities);

        int entityCount = entities.Count();
        int entityIndex = 0;
        while (entityIndex < entityCount)
        {
            IEntity ent = entities[entityIndex];
            entityIndex = entityIndex + 1;
            if (!ent)
                continue;

            Physics physics = ent.GetPhysics();
            if (!physics)
                continue;

            int geomCount = physics.GetNumGeoms();
            int geomIndex = 0;
            while (geomIndex < geomCount)
            {
                int layerMask = physics.GetGeomInteractionLayer(geomIndex);
                if ((layerMask & TRACE_IGNORE_LAYER_MASK) != 0)
                {
                    GBRS_GeomLayerBackup backup = new GBRS_GeomLayerBackup();
                    backup.m_Entity = ent;
                    backup.m_iGeomIndex = geomIndex;
                    backup.m_iLayerMask = layerMask;
                    m_aGeomLayerBackups.Insert(backup);

                    int cleared = layerMask & (~TRACE_IGNORE_LAYER_MASK);
                    physics.SetGeomInteractionLayer(geomIndex, cleared);
                }

                geomIndex = geomIndex + 1;
            }
        }
    }

    protected void RestoreTraceLayers()
    {
        if (!m_aGeomLayerBackups)
            return;

        int count = m_aGeomLayerBackups.Count();
        int index = 0;
        while (index < count)
        {
            GBRS_GeomLayerBackup backup = m_aGeomLayerBackups[index];
            index = index + 1;
            if (!backup || !backup.m_Entity)
                continue;

            Physics physics = backup.m_Entity.GetPhysics();
            if (!physics)
                continue;

            if (backup.m_iGeomIndex < 0)
                continue;

            if (backup.m_iGeomIndex >= physics.GetNumGeoms())
                continue;

            physics.SetGeomInteractionLayer(backup.m_iGeomIndex, backup.m_iLayerMask);
        }

        m_aGeomLayerBackups.Clear();
    }

    protected void CollectHierarchyEntities(IEntity ent, notnull array<IEntity> outEntities)
    {
        if (!ent)
            return;

        outEntities.Insert(ent);

        IEntity child = ent.GetChildren();
        while (child)
        {
            CollectHierarchyEntities(child, outEntities);
            child = child.GetSibling();
        }
    }

    // Matches RDF_RadarScanner.GetScanForward for TPN-19 entity yaw.
    protected void SyncEntityYawAntenna(IEntity owner)
    {
        if (!m_bUseEntityYawSpin || !m_AntennaEntity)
            return;

        float rpm = GetLiveScanRpm();
        if (rpm <= 0.0)
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return;

        float worldTimeS = world.GetWorldTime() * 0.001;
        float angleDeg = worldTimeS * rpm * 6.0;
        float worldYawDeg = 90.0 - angleDeg;
        vector ownerYpr = owner.GetYawPitchRoll();
        float localYawDeg = worldYawDeg - ownerYpr[0];

        m_AntennaEntity.SetYawPitchRoll(
            Vector(localYawDeg, m_fElevationBoresightDeg, m_fAntennaBaseRoll));
    }

    protected void StartSupplyDrain()
    {
        if (!IsAuthority())
            return;

        if (m_bSupplyTickScheduled)
            return;

        if (m_fSupplyCostPerTick <= 0.0)
            return;

        if (!SCR_ResourceSystemHelper.IsGlobalResourceTypeEnabled(EResourceType.SUPPLIES))
            return;

        int intervalMs = SUPPLY_TICK_MS_MIN;
        if (m_fSupplyTickIntervalS > 0.0)
            intervalMs = m_fSupplyTickIntervalS * 1000.0;

        if (intervalMs < SUPPLY_TICK_MS_MIN)
            intervalMs = SUPPLY_TICK_MS_MIN;

        GetGame().GetCallqueue().CallLater(ConsumeSupplyTick, intervalMs, true);
        m_bSupplyTickScheduled = true;
    }

    protected void StopSupplyDrain()
    {
        if (!m_bSupplyTickScheduled)
            return;

        GetGame().GetCallqueue().Remove(ConsumeSupplyTick);
        m_bSupplyTickScheduled = false;
    }

    protected void ConsumeSupplyTick()
    {
        if (!IsAuthority())
            return;

        if (!m_bPowered)
        {
            StopSupplyDrain();
            return;
        }

        if (!SCR_ResourceSystemHelper.IsGlobalResourceTypeEnabled(EResourceType.SUPPLIES))
            return;

        SCR_ResourceComponent resourceComponent = ResolveSupplyResourceComponent();
        if (!resourceComponent)
            return;

        EResourceReason reason = SCR_ResourceSystemHelper.ConsumeResources(
            resourceComponent,
            m_fSupplyCostPerTick,
            true,
            EResourceType.SUPPLIES);

        if (reason == EResourceReason.SUFFICIENT)
            return;

        SetPowered(false);
    }

    protected SCR_ResourceComponent ResolveSupplyResourceComponent()
    {
        IEntity owner = GetOwner();
        if (!owner)
            return null;

        SCR_CampaignBuildingCompositionComponent composition =
            SCR_CampaignBuildingCompositionComponent.Cast(owner.FindComponent(SCR_CampaignBuildingCompositionComponent));
        if (!composition)
            return null;

        IEntity providerEntity = composition.GetProviderEntity();
        if (!providerEntity)
            return null;

        SCR_CampaignBuildingProviderComponent provider =
            SCR_CampaignBuildingProviderComponent.Cast(providerEntity.FindComponent(SCR_CampaignBuildingProviderComponent));
        if (!provider)
            return SCR_ResourceComponent.FindResourceComponent(providerEntity);

        SCR_CampaignBuildingProviderComponent masterProvider;
        if (provider.UseMasterProviderBudget(EEditableEntityBudget.CAMPAIGN, masterProvider))
        {
            if (masterProvider)
                return masterProvider.GetResourceComponent();
        }

        IEntity masterEntity = provider.GetMasterProviderEntity();
        if (masterEntity && masterEntity != providerEntity)
        {
            SCR_CampaignBuildingProviderComponent masterFromEntity =
                SCR_CampaignBuildingProviderComponent.Cast(masterEntity.FindComponent(SCR_CampaignBuildingProviderComponent));
            if (masterFromEntity)
                return masterFromEntity.GetResourceComponent();
        }

        return provider.GetResourceComponent();
    }

    protected bool IsAuthority()
    {
        SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
        if (gameMode)
            return gameMode.IsMaster();

        return Replication.IsServer();
    }

    protected void ShutdownRadar()
    {
        if (m_Radar)
            m_Radar.SetEnabled(false);

        m_Radar = null;
        m_AntennaEntity = null;
        m_AntennaProcAnim = null;
        m_iAntennaSpinBone = -1;
        m_bUseEntityYawSpin = false;
        m_bUseBoneSpin = false;
        m_bConfigured = false;
        m_bPowered = false;
        m_bTraceIgnoreActive = false;
        m_fScanRpm = 0.0;
        if (m_DetectVisual)
            m_DetectVisual.Clear();
        if (m_aGeomLayerBackups)
            m_aGeomLayerBackups.Clear();
    }
}
