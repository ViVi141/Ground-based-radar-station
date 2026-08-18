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
// US AN/TPN-19: no spin bone; yaw the radar mesh entity to match ScanRpm.
// USSR Tesla RPL-5: stock ProcAnim on antenna_rotation bone.
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

    // Power drain: pull from nearby faction base storage, then the local bunker.
    [Attribute("15", UIWidgets.EditBox, desc: "Supplies consumed each drain tick while powered (base stockpile, then local bunker)")]
    protected float m_fSupplyCostPerTick;

    [Attribute("25", UIWidgets.EditBox, desc: "Seconds between supply drain ticks while powered")]
    protected float m_fSupplyTickIntervalS;

    [Attribute("1", UIWidgets.CheckBox, desc: "If true, power-on requires enough supplies for one drain tick in range (base or local bunker)")]
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

    [Attribute("{4D5CD8B2B5DE8916}Particles/Vehicle/Vehicle_fire_engine_medium.ptc", UIWidgets.ResourceNamePicker, "Persistent fire particle after destroy", "ptc")]
    protected ResourceName m_sDestroyedFireParticle;

    [Attribute("{8986B887CCF7BFA9}Particles/Vehicle/Vehicle_smoke_UAZ_damaged_black_02.ptc", UIWidgets.ResourceNamePicker, "Persistent smoke particle after destroy", "ptc")]
    protected ResourceName m_sDestroyedSmokeParticle;

    [Attribute("0 2.5 0", UIWidgets.EditBox, "Local offset for fire/smoke emitters")]
    protected vector m_vDestroyedFireOffset;

    // Runtime state — synced like SCR_BaseInteractiveLightComponent:
    // UserAction broadcast toggles peers live; RplSave/RplLoad covers JIP.
    protected bool m_bPowered;
    protected string m_WorkstationMode;

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
    protected float m_fAntennaBasePitch;
    protected float m_fAntennaBaseRoll;
    // Mechanical-scan phase continuity: while powered off the antenna stays
    // frozen at its last bearing; on re-power we offset the RDF scan angle so
    // the logical scan resumes from that bearing instead of snapping to the
    // world-clock angle. Radians, applied to RDF_RadarSettings.m_ScanPhaseOffsetRad.
    protected float m_fScanPhaseOffsetRad;
    protected float m_fAntennaFrozenAngleRad;
    protected bool m_bAntennaFrozen;
    protected float m_fVisualAzHalfDeg;
    protected float m_fVisualElHalfDeg;
    protected ref array<ref GBRS_GeomLayerBackup> m_aGeomLayerBackups;
    protected ref GBRS_RadarDetectVisual m_DetectVisual;
    protected ref GBRS_RadarDebugProbe m_DebugProbe;
    protected int m_iDebugLastScanSerial;
    protected float m_fDebugNextHeartbeatS;
    protected bool m_bDestroyed;
    protected GBRS_RadarStationDamageManagerComponent m_DamageManager;
    protected ParticleEffectEntity m_DestroyedFireParticle;
    protected ParticleEffectEntity m_DestroyedSmokeParticle;
    protected SCR_CampaignBuildingCompositionComponent m_BuildingComposition;
    protected bool m_bCompositionBuildGateBound;
    protected bool m_bBaseFactionListenerBound;
    // RDF 1.0.0 fire-control bridge: exposes LOCK-mode lock as a fire solution
    // for external weapons (SAM vehicles, AAA) that poll this station.
    protected ref RDF_RadarWeaponBridge m_WeaponBridge;
    // MANUAL workstation mode: live-tunable radar parameters (server-authoritative).
    protected ref GBRS_RadarManualConfig m_ManualConfig;
    // Antenna stare: park the antenna at a fixed RDF azimuth (0 = east,
    // 90 = north) and lock RDF GetScanForward to that absolute angle.
    protected bool m_bAntennaStare;
    protected float m_fAntennaStareAzDeg;
    // Product dwell while the dish is rotating. Stare overwrites UpdateInterval
    // on the live settings object; this value restores it when stare ends.
    protected float m_fProductUpdateIntervalS = 0.04;
    // One-frame guard so ClearStarePhase runs exactly once after stare off.
    protected bool m_bStarePhaseCleared = true;
    protected bool m_bStarePoseCached;
    protected float m_fStareLocalYawDeg;

    // Lightweight contact tracking for public events / Conflict early warning.
    // Keyed by RDF scatterer id because plots lose m_Entity after workstation
    // readout strips identity.
    protected static const float CONTACT_LOST_TIMEOUT_S = 5.0;
    protected static const float CONTACT_UPDATE_INTERVAL_S = 0.5;
    protected ref map<int, ref RDF_RadarTarget> m_Contacts;
    protected ref map<int, float> m_ContactLastSeen;
    protected ref map<int, bool> m_WlrFiredTrackIds;
    protected float m_fNextContactUpdateS;
    protected float m_fLastForceIntelS;
    protected bool m_bLockLayerEnabled;
    protected bool m_bIntelBriefingSent;

    override void OnPostInit(IEntity owner)
    {
        SetEventMask(owner, EntityEvent.INIT | EntityEvent.FRAME);

        // RDF EOnInit can race GBRS and try to set ACTIVE before the entity is
        // registered. Keep the sensor off until composition/build gate says so.
        RDF_RadarComponent radar = RDF_RadarComponent.Cast(owner.FindComponent(RDF_RadarComponent));
        if (radar)
            radar.SetEnabled(false);
    }

    override void EOnInit(IEntity owner)
    {
        super.EOnInit(owner);
        if (m_WorkstationMode == "")
            m_WorkstationMode = GBRS_RadarStationConstants.MODE_PD_SEARCH;
        if (!m_ManualConfig)
            m_ManualConfig = new GBRS_RadarManualConfig();
        if (!m_Contacts)
            m_Contacts = new map<int, ref RDF_RadarTarget>();
        if (!m_ContactLastSeen)
            m_ContactLastSeen = new map<int, float>();
        if (!m_WlrFiredTrackIds)
            m_WlrFiredTrackIds = new map<int, bool>();
        BindDamageManager(owner);
        BindBuildingCompositionGate(owner);
        if (IsCompositionReady())
            ApplyConfiguration(owner);
        ApplyUnderConstructionLock();
        BindCoveringBaseFactionListener();
        GBRS_CampaignRadarWarning.EnsureBound();
        if (IsCompositionReady())
            QueueIntelBriefing();
    }

    override void EOnFrame(IEntity owner, float timeSlice)
    {
        if (!m_bConfigured)
        {
            if (!IsCompositionReady())
                return;

            ApplyConfiguration(owner);
            return;
        }

        if (!m_bPowered)
            return;

        if (!m_AntennaEntity)
            EnsureAntennaResolved();

        // Antenna stare: keep the locked absolute scan angle on the live
        // RDF settings object (Scan may have replaced settings this frame).
        if (m_bAntennaStare)
            ApplyAntennaStarePhase(owner);
        else if (!m_bStarePhaseCleared)
            ClearStarePhase();

        SyncRadarBindToAntenna(owner);
        SyncAntennaBoneSpin(owner);
        SyncEntityYawAntenna(owner);
        TickScattererRegistry(owner);
        RenderScanVisuals(owner);
        UpdateContactEvents(owner);
        UpdateWlrEvents(owner);
        DebugScanTick(owner);
    }

    //------------------------------------------------------------------------------------------------
    // Antenna stare: park the antenna (and RDF scan) at m_fAntennaStareAzDeg.
    // Write the bearing as an absolute RDF azimuth and set m_bScanAngleLocked
    // so GetScanForward does not depend on worldTime*rpm. Compensating the
    // world clock every frame is one tick late when RDF_RadarComponent is
    // listed before this component (US 2.5 deg beam then misses the target).
    protected void ApplyAntennaStarePhase(IEntity owner)
    {
        float targetRad = m_fAntennaStareAzDeg * 0.017453292519943295;
        m_fScanPhaseOffsetRad = targetRad;
        m_bStarePhaseCleared = false;

        RDF_RadarSettings settings = GetLiveRadarSettings();
        StampScanPhaseOnSettings(settings);
    }

    protected RDF_RadarSettings GetLiveRadarSettings()
    {
        if (!m_Radar)
            return null;

        RDF_RadarSensor sensor = m_Radar.GetSensor();
        if (!sensor)
            return null;

        return sensor.GetSettings();
    }

    protected void StampScanPhaseOnSettings(RDF_RadarSettings settings)
    {
        if (!settings)
            return;

        settings.m_ScanPhaseOffsetRad = m_fScanPhaseOffsetRad;
        settings.m_bScanAngleLocked = m_bAntennaStare;
        if (m_bAntennaStare)
            settings.m_UpdateInterval = GBRS_RadarStationConstants.STARE_UPDATE_INTERVAL_S;
        else if (m_fProductUpdateIntervalS > 0.0)
            settings.m_UpdateInterval = m_fProductUpdateIntervalS;
    }

    //------------------------------------------------------------------------------------------------
    // Restore free rotation after stare: phase offset back to 0 so the scan
    // continues from the world-clock angle.
    protected void ClearStarePhase()
    {
        m_fScanPhaseOffsetRad = 0.0;
        RDF_RadarSettings settings = GetLiveRadarSettings();
        if (settings)
        {
            settings.m_ScanPhaseOffsetRad = 0.0;
            settings.m_bScanAngleLocked = false;
            if (m_fProductUpdateIntervalS > 0.0)
                settings.m_UpdateInterval = m_fProductUpdateIntervalS;
        }
        m_bStarePhaseCleared = true;
        m_bStarePoseCached = false;
    }

    //------------------------------------------------------------------------------------------------
    // RDF only Ticks the scatterer registry from ScanOnce. Stare / 40 ms
    // dwells then classify Eden's ~15k DYNAMIC dump at scan rate, and LIFO
    // ProcessPending keeps the in-beam helicopter behind infantry rejects.
    // Drive Configure+Tick every powered frame so classification stays at
    // frame rate. Same-frame ScanOnce Tick coalesces into one flush.
    protected void TickScattererRegistry(IEntity owner)
    {
        if (!m_Radar)
            return;

        RDF_RadarSensor sensor = m_Radar.GetSensor();
        if (!sensor)
            return;

        if (!sensor.IsEnabled())
            return;

        RDF_RadarSettings settings = sensor.GetSettings();
        if (!settings)
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return;

        vector origin = GetAntennaBindOrigin(owner);
        float rangeM = settings.m_Range;
        if (rangeM <= 0.0)
            rangeM = 2000.0;

        float scale = settings.m_ScattererDiscoveryRangeScale;
        if (scale <= 0.0)
            scale = GBRS_RadarStationConstants.SCATTERER_DISCOVERY_RANGE_SCALE;

        RDF_RadarScattererRegistry.Configure(
            settings.m_ScattererDiscoveryIntervalS,
            settings.m_ScattererClassifyPerTick,
            settings.m_ScattererRefreshPerTick,
            settings.m_ScattererMaxEntries,
            true);
        RDF_RadarScattererRegistry.Tick(
            world,
            world.GetWorldTime() * 0.001,
            origin,
            rangeM * scale);
    }

    override void OnDelete(IEntity owner)
    {
        GBRS_RadarStationMenu.CloseIfBound(this);
        StopSupplyDrain();
        StopAntennaResolveRetry();
        SetAntennaSpinning(false);
        SetTraceIgnoreActive(false);
        StopDestroyedFireEffects();
        UnbindCoveringBaseFactionListener();
        if (m_bCompositionBuildGateBound && m_BuildingComposition)
        {
            m_BuildingComposition.GetOnCompositionSpawned().Remove(OnCompositionFullyBuilt);
            m_bCompositionBuildGateBound = false;
        }
        ClearContactEvents();
        ShutdownRadar();
        if (GetGame())
        {
            ScriptCallQueue queue = GetGame().GetCallqueue();
            if (queue)
                queue.Remove(TryNotifyIntelBriefing);
        }
        super.OnDelete(owner);
    }

    //------------------------------------------------------------------------------------------------
    // JIP / stream-in — same contract as SCR_BaseInteractiveLightComponent.RplSave/RplLoad.
    //------------------------------------------------------------------------------------------------
    override bool RplSave(ScriptBitWriter writer)
    {
        super.RplSave(writer);
        writer.WriteBool(m_bPowered);
        writer.WriteBool(m_bScanVisualEnabled);
        writer.WriteInt(WorkstationModeToIndex(m_WorkstationMode));
        writer.WriteBool(m_bDestroyed);
        if (!m_ManualConfig)
            m_ManualConfig = new GBRS_RadarManualConfig();
        m_ManualConfig.WriteRpl(writer);
        writer.WriteBool(m_bAntennaStare);
        writer.WriteFloat(m_fAntennaStareAzDeg);
        writer.WriteString(GetAffiliatedFactionKey());
        return true;
    }

    //------------------------------------------------------------------------------------------------
    override bool RplLoad(ScriptBitReader reader)
    {
        super.RplLoad(reader);

        bool powered;
        bool scanVisual;
        int modeIndex;
        bool destroyed;
        reader.ReadBool(powered);
        reader.ReadBool(scanVisual);
        reader.ReadInt(modeIndex);
        reader.ReadBool(destroyed);

        if (!m_ManualConfig)
            m_ManualConfig = new GBRS_RadarManualConfig();
        m_ManualConfig.ReadRpl(reader);

        bool stare;
        float stareAz;
        reader.ReadBool(stare);
        reader.ReadFloat(stareAz);
        m_bAntennaStare = stare;
        m_fAntennaStareAzDeg = stareAz;

        string occupyingFactionKey;
        reader.ReadString(occupyingFactionKey);
        ApplyAffiliatedFactionKey(occupyingFactionKey);

        if (!m_bConfigured)
            ApplyConfiguration(GetOwner());

        if (destroyed)
        {
            ApplyDestroyedStateLocal(false);
        }
        else
        {
            TogglePower(powered, true);
            ToggleScanVisual(scanVisual);
            ApplyWorkstationModeLocal(IndexToWorkstationMode(modeIndex));
        }
        return true;
    }

    bool IsConfigured()
    {
        return m_bConfigured;
    }

    bool IsPowered()
    {
        return m_bPowered;
    }

    bool IsDestroyed()
    {
        if (m_bDestroyed)
            return true;

        if (m_DamageManager && m_DamageManager.IsStationDestroyed())
            return true;

        return false;
    }

    // Same-faction operators only. After a covering Conflict base is captured,
    // affiliation follows the occupying faction so captors can take the station.
    bool IsFriendlyUser(IEntity user)
    {
        if (!user)
            return false;

        IEntity owner = GetOwner();
        if (!owner)
            return false;

        Faction stationFaction = SCR_Faction.GetEntityFaction(owner);
        if (!stationFaction)
            return false;

        Faction userFaction = SCR_Faction.GetEntityFaction(user);
        if (!userFaction)
            return false;

        if (stationFaction != userFaction)
            return false;

        return true;
    }

    float GetHealth()
    {
        if (!m_DamageManager)
            return 0.0;
        return m_DamageManager.GetStationHealth();
    }

    float GetMaxHealth()
    {
        if (!m_DamageManager)
            return 0.0;
        return m_DamageManager.GetStationMaxHealth();
    }

    float GetHealthScaled()
    {
        if (!m_DamageManager)
            return 0.0;
        return m_DamageManager.GetStationHealthScaled();
    }

    // Called by GBRS_RadarStationChildDamageComponent when an engine explosion
    // (C4 etc.) hits a child part (antenna / generator). Relays the raw damage
    // into the station root's own 8000 HP pool so any hit part hurts the whole
    // station. HandleDamage applies the root HitZone multipliers itself — pass
    // the raw damageValue, do NOT pre-compute effective damage (that would
    // double-apply the multipliers).
    // multiplier scales the child's received damage before relay (realism:
    // antenna = 0.2, generator = 1.0).
    void RelayDamageToStation(notnull BaseDamageContext damageContext, float multiplier = 1.0)
    {
        if (!m_DamageManager)
            return;

        if (m_DamageManager.IsStationDestroyed())
            return;

        HitZone defaultZone = m_DamageManager.GetDefaultHitZone();
        if (!defaultZone)
            return;

        float relayedDamage = damageContext.damageValue * multiplier;
        if (relayedDamage <= 0.0)
            return;

        IEntity owner = GetOwner();
        vector dirNorm[3];
        dirNorm[0] = damageContext.hitPosition;
        dirNorm[1] = damageContext.hitDirection;
        dirNorm[2] = damageContext.hitNormal;

        SCR_DamageContext relayed = new SCR_DamageContext(
            damageContext.damageType,
            relayedDamage,
            dirNorm,
            owner,
            defaultZone,
            damageContext.instigator,
            damageContext.material,
            -1,
            -1);
        m_DamageManager.HandleDamage(relayed);

        if (m_DamageManager.IsDebugDraw())
        {
            float effective = defaultZone.ComputeEffectiveDamage(relayed, false);
            Print("[GBRS-DAMAGE] relayed child damage " + relayedDamage.ToString()
                + " (mult " + multiplier.ToString()
                + ") -> effective " + effective.ToString()
                + " (hp now " + m_DamageManager.GetStationHealth().ToString() + ")",
                LogLevel.WARNING);

            DrawRelayDebug(damageContext, relayedDamage, effective);
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Draws a marker at the child hit point, a line to the station root, and
    //! a label at the root showing raw->effective damage + remaining HP.
    //! All shapes/text use ONCE flags so the engine auto-clears them.
    protected void DrawRelayDebug(notnull BaseDamageContext damageContext, float relayedDamage, float effective)
    {
        IEntity owner = GetOwner();
        if (!owner)
            return;

        World world = owner.GetWorld();
        if (!world)
            return;

        vector hitPos = damageContext.hitPosition;
        vector rootPos = owner.GetOrigin();

        // Line from hit point to the station root pool.
        vector linePts[2];
        linePts[0] = hitPos;
        linePts[1] = rootPos + "0 1.5 0";
        Shape.CreateLines(
            Color.YELLOW,
            ShapeFlags.ONCE | ShapeFlags.NOOUTLINE | ShapeFlags.TRANSP | ShapeFlags.NOZBUFFER,
            linePts,
            2);

        // Root label: type, raw relayed, effective, hp left.
        string typeName = typename.EnumToString(EDamageType, damageContext.damageType);
        DebugTextWorldSpace.Create(
            world,
            "ROOT " + typeName + "\nraw " + relayedDamage.ToString() + " -> eff " + effective.ToString()
                + "\nhp " + m_DamageManager.GetStationHealth().ToString() + "/" + m_DamageManager.GetStationMaxHealth().ToString(),
            DebugTextFlags.ONCE | DebugTextFlags.CENTER | DebugTextFlags.FACE_CAMERA,
            rootPos[0], rootPos[1] + 2.2, rootPos[2],
            11,
            Color.CYAN,
            ARGB(200, 0, 0, 0));
    }

    // Called by GBRS_RadarStationDamageManagerComponent when HP reaches DESTROYED.
    void OnStationDestroyed()
    {
        ApplyDestroyedStateLocal(true);
    }

    // Shared destroy path for live damage and JIP RplLoad.
    protected void ApplyDestroyedStateLocal(bool fromDamageEvent)
    {
        if (m_bDestroyed)
        {
            EnsureDestroyedFireEffects();
            return;
        }

        m_bDestroyed = true;
        ClearContactEvents();

        if (GBRS_RadarStationEvents.OnRadarDestroyed)
            GBRS_RadarStationEvents.OnRadarDestroyed.Invoke(this);

        if (m_bDebugLog)
            Print("[GBRS-DEBUG] Station DESTROYED — forcing power off + fire", LogLevel.WARNING);

        if (m_bPowered)
            TogglePower(false, true);
        else if (m_Radar)
            m_Radar.SetEnabled(false);

        GBRS_RadarStationMenu.CloseIfBound(this);
        SetAntennaSpinning(false);
        SetTraceIgnoreActive(false);
        StopSupplyDrain();
        if (m_DetectVisual)
            m_DetectVisual.Clear();

        EnsureDestroyedFireEffects();
    }

    //------------------------------------------------------------------------------------------------
    void EnsureDestroyedFireEffects()
    {
        IEntity owner = GetOwner();
        if (!owner)
            return;

        if (!m_DestroyedFireParticle && !m_sDestroyedFireParticle.IsEmpty())
        {
            m_DestroyedFireParticle = SpawnAttachedParticle(
                m_sDestroyedFireParticle, owner, m_vDestroyedFireOffset);
        }

        if (!m_DestroyedSmokeParticle && !m_sDestroyedSmokeParticle.IsEmpty())
        {
            vector smokeOffset = m_vDestroyedFireOffset;
            smokeOffset[1] = smokeOffset[1] + 1.0;
            m_DestroyedSmokeParticle = SpawnAttachedParticle(
                m_sDestroyedSmokeParticle, owner, smokeOffset);
        }
    }

    //------------------------------------------------------------------------------------------------
    void StopDestroyedFireEffects()
    {
        if (m_DestroyedFireParticle)
        {
            SCR_ParticleHelper.StopParticleEmissionAndLights(m_DestroyedFireParticle);
            m_DestroyedFireParticle = null;
        }

        if (m_DestroyedSmokeParticle)
        {
            SCR_ParticleHelper.StopParticleEmissionAndLights(m_DestroyedSmokeParticle);
            m_DestroyedSmokeParticle = null;
        }
    }

    //------------------------------------------------------------------------------------------------
    protected ParticleEffectEntity SpawnAttachedParticle(
        ResourceName particle,
        IEntity parent,
        vector localOffset)
    {
        if (particle.IsEmpty() || !parent)
            return null;

        ParticleEffectEntitySpawnParams spawnParams = new ParticleEffectEntitySpawnParams();
        spawnParams.Transform[3] = localOffset;
        spawnParams.FollowParent = parent;
        spawnParams.PlayOnSpawn = true;
        spawnParams.UseFrameEvent = true;
        spawnParams.DeleteWhenStopped = true;
        return ParticleEffectEntity.SpawnParticleEffect(particle, spawnParams);
    }

    protected void BindDamageManager(IEntity owner)
    {
        if (!owner)
            return;

        m_DamageManager =
            GBRS_RadarStationDamageManagerComponent.Cast(
                owner.FindComponent(GBRS_RadarStationDamageManagerComponent));
        if (!m_DamageManager)
            return;

        if (m_DamageManager.IsStationDestroyed())
            ApplyDestroyedStateLocal(false);
    }

    // Conflict FreeRoam: keep radar/damage inert until shovel-build finishes.
    protected void BindBuildingCompositionGate(IEntity owner)
    {
        if (!owner)
            return;

        m_BuildingComposition =
            SCR_CampaignBuildingCompositionComponent.Cast(
                owner.FindComponent(SCR_CampaignBuildingCompositionComponent));
        if (!m_BuildingComposition)
            return;

        if (m_BuildingComposition.IsCompositionSpawned())
            return;

        if (!m_bCompositionBuildGateBound)
        {
            m_BuildingComposition.GetOnCompositionSpawned().Insert(OnCompositionFullyBuilt);
            m_bCompositionBuildGateBound = true;
        }
    }

    protected void ApplyUnderConstructionLock()
    {
        if (!m_BuildingComposition)
            return;

        if (m_BuildingComposition.IsCompositionSpawned())
            return;

        if (m_Radar)
            m_Radar.SetEnabled(false);

        if (m_DamageManager)
            m_DamageManager.EnableDamageHandling(false);

        m_bPowered = false;
        SetAntennaSpinning(false);
        SyncIntelRadio(false);
    }

    protected void OnCompositionFullyBuilt(bool spawned)
    {
        if (!spawned)
            return;

        if (m_DamageManager)
            m_DamageManager.EnableDamageHandling(true);

        if (!m_bConfigured)
            ApplyConfiguration(GetOwner());

        if (m_bCompositionBuildGateBound && m_BuildingComposition)
        {
            m_BuildingComposition.GetOnCompositionSpawned().Remove(OnCompositionFullyBuilt);
            m_bCompositionBuildGateBound = false;
        }

        QueueIntelBriefing();
    }

    //------------------------------------------------------------------------------------------------
    protected void QueueIntelBriefing()
    {
        if (m_bIntelBriefingSent)
            return;
        if (!GetGame())
            return;

        GetGame().GetCallqueue().CallLater(TryNotifyIntelBriefing, 1000, false);
    }

    //------------------------------------------------------------------------------------------------
    protected void TryNotifyIntelBriefing()
    {
        if (m_bIntelBriefingSent)
            return;
        if (!IsAuthority())
            return;
        if (IsDestroyed())
            return;
        if (!IsCompositionReady())
            return;

        IEntity owner = GetOwner();
        if (!owner)
            return;

        Faction faction = SCR_Faction.GetEntityFaction(owner);
        if (!faction)
            return;

        m_bIntelBriefingSent = true;
        GBRS_IntelRadioNet.NotifyIntelBriefing(this, faction);
    }

    bool IsCompositionReady()
    {
        if (!m_BuildingComposition)
            return true;

        return m_BuildingComposition.IsCompositionSpawned();
    }

    bool IsScanVisualEnabled()
    {
        return m_bScanVisualEnabled;
    }

    // Like SCR_BaseInteractiveLightComponent.ToggleLight — runs on every peer
    // that receives the broadcast UserAction (and on RplLoad).
    void ToggleScanVisual(bool enabled)
    {
        if (enabled == m_bScanVisualEnabled)
            return;

        m_bScanVisualEnabled = enabled;
        if (!enabled && m_DetectVisual)
            m_DetectVisual.Clear();
    }

    void SetScanVisualEnabled(bool enabled)
    {
        ToggleScanVisual(enabled);
    }

    void RequestToggleScanVisual()
    {
        ToggleScanVisual(!m_bScanVisualEnabled);
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

    // Unpowered path for auto-tests: TogglePower applies this mode instead of
    // always booting PD SEARCH then immediately reconfiguring. If the station
    // is already powered, this is a real mode switch.
    bool SetDesiredWorkstationMode(string mode)
    {
        if (!IsValidWorkstationMode(mode))
            return false;

        if (m_bPowered)
            return ApplyWorkstationMode(mode);

        m_WorkstationMode = mode;
        return true;
    }

    // Menu / API entry. Clients only submit a request; authority applies and
    // broadcasts so PPI / RDF stay synchronized. Rejected asks leave the mode
    // unchanged on the requester.
    bool ApplyWorkstationMode(string mode)
    {
        if (IsDestroyed())
            return false;

        if (!IsValidWorkstationMode(mode))
            return false;

        if (!m_bPowered)
            return false;

        if (mode == m_WorkstationMode)
            return true;

        if (IsAuthority())
        {
            ApplyWorkstationModeLocal(mode);
            Rpc(RpcDo_WorkstationMode, WorkstationModeToIndex(mode));
            return true;
        }

        return GBRS_PlayerControllerNet.RequestWorkstationMode(this, mode);
    }

    RplId GetStationRplId()
    {
        return Replication.FindItemId(this);
    }

    bool IsStationAuthority()
    {
        return IsAuthority();
    }

    //------------------------------------------------------------------------------------------------
    //! Operator TX NET. Authority-only. Interrupts the current intel VO.
    bool AuthorityForceIntelTx()
    {
        if (!IsAuthority())
            return false;
        if (IsDestroyed())
            return false;
        if (!m_bPowered)
            return false;

        float nowS = System.GetTickCount() * 0.001;
        if ((nowS - m_fLastForceIntelS) < 1.0)
            return false;

        bool sent = GBRS_CampaignRadarWarning.ForceBroadcastFromStation(this);
        if (sent)
            m_fLastForceIntelS = nowS;

        return sent;
    }

    SCR_CampaignMilitaryBaseComponent GetCoveringCampaignBase(bool sameFactionOnly)
    {
        return FindCoveringCampaignBase(sameFactionOnly);
    }

    // Server entry from GBRS_PlayerControllerNet (menu on a proxy).
    bool AuthoritySetWorkstationMode(string mode)
    {
        if (!IsAuthority())
            return false;

        if (IsDestroyed())
            return false;

        if (!m_bPowered)
            return false;

        if (!IsValidWorkstationMode(mode))
            return false;

        if (mode == m_WorkstationMode)
            return true;

        ApplyWorkstationModeLocal(mode);
        Rpc(RpcDo_WorkstationMode, WorkstationModeToIndex(mode));
        return true;
    }

    [RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
    protected void RpcDo_WorkstationMode(int modeIndex)
    {
        ApplyWorkstationModeLocal(IndexToWorkstationMode(modeIndex));
    }

    //------------------------------------------------------------------------------------------------
    // MANUAL workstation mode: operator parameter tuning.
    //------------------------------------------------------------------------------------------------

    // Client entry (menu). Only MANUAL mode accepts tuning; authority applies
    // the change and broadcasts so every peer sees the same radar settings.
    bool ApplyManualParam(int paramIndex, float value)
    {
        if (IsDestroyed())
            return false;

        if (!m_bPowered)
            return false;

        if (m_WorkstationMode != GBRS_RadarStationConstants.MODE_MANUAL)
            return false;

        if (paramIndex < 0 || paramIndex >= GBRS_RadarManualConfig.PARAM_COUNT)
            return false;

        if (!m_ManualConfig)
            m_ManualConfig = new GBRS_RadarManualConfig();

        float clamped = GBRS_RadarManualConfig.ClampParam(
            paramIndex, value, m_ManualConfig.m_RangeMaxM);

        if (IsAuthority())
        {
            AuthoritySetManualParam(paramIndex, clamped);
            return true;
        }

        return GBRS_PlayerControllerNet.RequestManualParam(this, paramIndex, clamped);
    }

    // Server entry from GBRS_PlayerControllerNet (menu on a proxy).
    bool AuthoritySetManualParam(int paramIndex, float value)
    {
        if (!IsAuthority())
            return false;

        if (IsDestroyed())
            return false;

        if (!m_bPowered)
            return false;

        if (m_WorkstationMode != GBRS_RadarStationConstants.MODE_MANUAL)
            return false;

        if (paramIndex < 0 || paramIndex >= GBRS_RadarManualConfig.PARAM_COUNT)
            return false;

        if (!m_ManualConfig)
            m_ManualConfig = new GBRS_RadarManualConfig();

        float clamped = GBRS_RadarManualConfig.ClampParam(
            paramIndex, value, m_ManualConfig.m_RangeMaxM);
        m_ManualConfig.SetParam(paramIndex, clamped);
        Rpc(RpcDo_ManualParam, paramIndex, clamped);

        // STARE AZ parks the antenna; do not ResetSession via ApplyManualSettings.
        if (paramIndex == GBRS_RadarManualConfig.PARAM_STARE)
        {
            if (clamped < 0.0)
                AuthoritySetAntennaStare(false, 0.0);
            else
                AuthoritySetAntennaStare(true, clamped);
            return true;
        }

        ApplyManualSettings(GetOwner());
        return true;
    }

    [RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
    protected void RpcDo_ManualParam(int paramIndex, float value)
    {
        if (!m_ManualConfig)
            m_ManualConfig = new GBRS_RadarManualConfig();

        if (paramIndex < 0 || paramIndex >= GBRS_RadarManualConfig.PARAM_COUNT)
            return;

        m_ManualConfig.SetParam(paramIndex, value);
        if (paramIndex == GBRS_RadarManualConfig.PARAM_STARE)
            return;

        ApplyManualSettings(GetOwner());
    }

    // Read the operator's manual config (menu display). Null-safe.
    GBRS_RadarManualConfig GetManualConfig()
    {
        if (!m_ManualConfig)
            m_ManualConfig = new GBRS_RadarManualConfig();
        return m_ManualConfig;
    }

    // True when the operator is actively tuning MANUAL mode (menu uses it to
    // decide whether to show the parameter list instead of the contact list).
    bool IsManualMode()
    {
        return m_WorkstationMode == GBRS_RadarStationConstants.MODE_MANUAL;
    }

    //------------------------------------------------------------------------------------------------
    // Antenna stare control. enabled=true parks the antenna at azDeg (0-360,
    // RDF convention: 0 = east / +X, 90 = north / +Z) in any workstation
    // mode; enabled=false resumes free rotation.
    bool IsAntennaStare()
    {
        return m_bAntennaStare;
    }

    float GetAntennaStareAzDeg()
    {
        return m_fAntennaStareAzDeg;
    }

    // Live RDF scan angle in degrees (0 = east, 90 = north).
    float GetLiveScanAngleDeg()
    {
        if (m_bAntennaStare)
            return m_fAntennaStareAzDeg;

        float rpm = GetLiveScanRpm();
        BaseWorld world = GetGame().GetWorld();
        float worldTimeS = 0.0;
        if (world)
            worldTimeS = world.GetWorldTime() * 0.001;

        float angleRad = m_fScanPhaseOffsetRad;
        if (rpm > 0.0)
            angleRad = worldTimeS * rpm * Math.PI * 2.0 / 60.0 + m_fScanPhaseOffsetRad;

        float deg = angleRad * Math.RAD2DEG;
        while (deg < 0.0)
            deg = deg + 360.0;
        while (deg >= 360.0)
            deg = deg - 360.0;
        return deg;
    }

    // Client entry (menu). Server-authoritative; broadcast so peers see the
    // same antenna bearing.
    bool SetAntennaStare(bool enabled, float azDeg)
    {
        if (IsDestroyed())
            return false;

        if (!m_bPowered)
            return false;

        float az = azDeg;
        while (az < 0.0)
            az = az + 360.0;
        while (az >= 360.0)
            az = az - 360.0;

        if (IsAuthority())
        {
            AuthoritySetAntennaStare(enabled, az);
            return true;
        }

        return GBRS_PlayerControllerNet.RequestAntennaStare(this, enabled, az);
    }

    // Server entry from GBRS_PlayerControllerNet.
    bool AuthoritySetAntennaStare(bool enabled, float azDeg)
    {
        if (!IsAuthority())
            return false;

        if (IsDestroyed())
            return false;

        if (!m_bPowered)
            return false;

        float az = azDeg;
        while (az < 0.0)
            az = az + 360.0;
        while (az >= 360.0)
            az = az - 360.0;

        m_bAntennaStare = enabled;
        m_fAntennaStareAzDeg = az;
        m_bStarePhaseCleared = !enabled;
        m_bStarePoseCached = false;

        if (enabled)
            ApplyAntennaStarePhase(GetOwner());
        else
            ClearStarePhase();

        Rpc(RpcDo_AntennaStare, enabled, az);
        return true;
    }

    [RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
    protected void RpcDo_AntennaStare(bool enabled, float azDeg)
    {
        m_bAntennaStare = enabled;
        m_fAntennaStareAzDeg = azDeg;
        m_bStarePhaseCleared = !enabled;
        m_bStarePoseCached = false;
        if (enabled)
            ApplyAntennaStarePhase(GetOwner());
        else
            ClearStarePhase();
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

    // RDF 1.0.0 fire-control bridge. External weapons (SAM vehicles, AAA,
    // guided launchers) call this to consume the station's LOCK-mode lock:
    //   RDF_RadarFireSolution solution = new RDF_RadarFireSolution();
    //   if (station.TryGetFireSolution(solution) && solution.m_CanAuthorizeFire)
    //       aim at solution.m_AimPos with solution.m_AimVel
    // Returns false when the station is not powered, not in LOCK mode, or the
    // lock manager holds no confirmed target.
    bool TryGetFireSolution(out RDF_RadarFireSolution solution)
    {
        solution = null;

        if (IsDestroyed())
            return false;

        if (!m_bPowered)
            return false;

        if (m_WorkstationMode != GBRS_RadarStationConstants.MODE_LOCK)
            return false;

        if (!m_Radar)
            return false;

        if (!m_WeaponBridge)
        {
            m_WeaponBridge = new RDF_RadarWeaponBridge();
            m_WeaponBridge.BindRadarComponent(m_Radar);
            // LOCK auto-acquire holds TRACKING before weapons may fire.
            m_WeaponBridge.SetRequireTrackingForFire(true);
            m_WeaponBridge.SetPreferArmAim(false);
        }

        return m_WeaponBridge.TryGetFireSolution(solution);
    }

    // Convenience for HUD / debug: true when the LOCK layer currently holds a
    // tracking lock that authorizes fire.
    bool IsFireAuthorized()
    {
        RDF_RadarFireSolution solution = new RDF_RadarFireSolution();
        if (!TryGetFireSolution(solution))
            return false;
        return solution.m_CanAuthorizeFire;
    }

    // True when the station root or any descendant damage manager is destroyed.
    bool IsDestroyedForPpi()
    {
        if (IsDestroyed())
            return true;

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

    // Client UserAction / menu entry: ask authority. Listen servers apply locally.
    void RequestTogglePower()
    {
        IEntity localUser = GetLocalControlledEntity();
        if (localUser && !IsFriendlyUser(localUser))
            return;

        if (IsAuthority())
        {
            SetPowered(!m_bPowered);
            return;
        }

        GBRS_PlayerControllerNet.RequestTogglePower(this);
    }

    protected IEntity GetLocalControlledEntity()
    {
        PlayerController controller = GetGame().GetPlayerController();
        if (!controller)
            return null;

        return controller.GetControlledEntity();
    }

    // Authority-driven power change. Supply affordability and stockpile drain are
    // decided only here; peers receive RpcDo_TogglePower after confirmation.
    void SetPowered(bool powered)
    {
        if (!IsAuthority())
            return;

        bool before = m_bPowered;
        TogglePower(powered, false);
        if (m_bPowered == before)
            return;

        Rpc(RpcDo_TogglePower, m_bPowered);
    }

    // Auto-test only: bypass supply affordability so Script Debugger runs work
    // without a linked base stockpile.
    void SetPoweredForAutoTest(bool powered)
    {
        TogglePower(powered, true);
    }

    [RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
    protected void RpcDo_TogglePower(bool powered)
    {
        TogglePower(powered, true);
    }

    [RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
    protected void RpcDo_AdoptOccupyingFaction(string factionKey)
    {
        ApplyAffiliatedFactionKey(factionKey);
    }

    // Like SCR_BaseInteractiveLightComponent.ToggleLight.
    // skipSupplyGate: auto-test / RplLoad / RpcDo must not re-check stockpile.
    void TogglePower(bool turnOn, bool skipSupplyGate = false)
    {
        if (m_bPowered == turnOn)
            return;

        if (!GetGame().InPlayMode())
            return;

        if (turnOn && IsDestroyed())
            return;

        if (turnOn && !IsCompositionReady())
            return;

        if (!m_bConfigured)
            ApplyConfiguration(GetOwner());

        if (!m_Radar)
            return;

        if (turnOn)
        {
            // Proxies trust broadcast UserAction / RpcDo; only authority gates stockpile.
            if (!skipSupplyGate)
            {
                if (IsAuthority())
                {
                    if (!CanAffordPowerOn())
                        return;
                }
            }

            // Resume from the frozen antenna bearing: offset = frozenAngle -
            // current world-clock angle. ApplySearchSettings / PushSensorSettings
            // stamp m_fScanPhaseOffsetRad into the new settings object, and the
            // antenna sync uses the same offset so mesh and scan stay in sync.
            if (m_bAntennaFrozen)
            {
                float rpm = GetLiveScanRpm();
                if (rpm > 0.0)
                {
                    BaseWorld world = GetGame().GetWorld();
                    float worldTimeS = 0.0;
                    if (world)
                        worldTimeS = world.GetWorldTime() * 0.001;
                    m_fScanPhaseOffsetRad = m_fAntennaFrozenAngleRad
                        - worldTimeS * rpm * Math.PI * 2.0 / 60.0;
                }
                else
                {
                    m_fScanPhaseOffsetRad = 0.0;
                }
                m_bAntennaFrozen = false;
            }

            // Resume the last / requested workstation mode. Forcing PD SEARCH
            // here made StartWlr() Configure PD then WLR in the same debugger
            // frame; the next UpdateEntities then native-crashed.
            if (!IsValidWorkstationMode(m_WorkstationMode))
                m_WorkstationMode = GBRS_RadarStationConstants.MODE_PD_SEARCH;
            m_bPowered = true;
            ApplyWorkstationModeLocal(m_WorkstationMode);

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
            SyncIntelRadio(true);
            return;
        }

        if (m_bDebugLog)
            Print("[GBRS-DEBUG] Power OFF", LogLevel.WARNING);

        // Freeze the antenna at its last driven bearing. Sync* stops running
        // below (EOnFrame early-outs on !m_bPowered) so the value recorded by
        // the last spin tick is the frozen angle; re-power offsets the RDF
        // scan phase to resume from here instead of the world-clock angle.
        m_bAntennaFrozen = true;
        m_bPowered = false;
        m_Radar.SetEnabled(false);
        SetAntennaSpinning(false);
        SetTraceIgnoreActive(false);
        StopSupplyDrain();
        ClearContactEvents();
        GBRS_RadarStationMenu.CloseIfBound(this);
        SyncIntelRadio(false);
    }

    protected bool IsValidWorkstationMode(string mode)
    {
        if (mode == GBRS_RadarStationConstants.MODE_PD_SEARCH)
            return true;
        if (mode == GBRS_RadarStationConstants.MODE_WLR)
            return true;
        if (mode == GBRS_RadarStationConstants.MODE_LOCK)
            return true;
        if (mode == GBRS_RadarStationConstants.MODE_MANUAL)
            return true;
        return false;
    }

    protected int WorkstationModeToIndex(string mode)
    {
        if (mode == GBRS_RadarStationConstants.MODE_WLR)
            return 1;
        if (mode == GBRS_RadarStationConstants.MODE_LOCK)
            return 2;
        if (mode == GBRS_RadarStationConstants.MODE_MANUAL)
            return 3;
        return 0;
    }

    protected string IndexToWorkstationMode(int modeIndex)
    {
        if (modeIndex == 1)
            return GBRS_RadarStationConstants.MODE_WLR;
        if (modeIndex == 2)
            return GBRS_RadarStationConstants.MODE_LOCK;
        if (modeIndex == 3)
            return GBRS_RadarStationConstants.MODE_MANUAL;
        return GBRS_RadarStationConstants.MODE_PD_SEARCH;
    }

    protected void ApplyWorkstationModeLocal(string mode)
    {
        if (!IsValidWorkstationMode(mode))
            return;

        if (!ApplyWorkstationModeEffects(mode))
            return;

        m_WorkstationMode = mode;
    }

    // Configures RDF for the mode without network traffic.
    protected bool ApplyWorkstationModeEffects(string mode)
    {
        IEntity owner = GetOwner();
        if (!owner)
            return false;

        if (!m_Radar)
            m_Radar = RDF_RadarComponent.Cast(owner.FindComponent(RDF_RadarComponent));
        if (!m_Radar)
            return false;

        if (mode == GBRS_RadarStationConstants.MODE_WLR)
        {
            ApplyWlrSettings(owner);
            return true;
        }

        if (mode == GBRS_RadarStationConstants.MODE_LOCK)
        {
            ApplyLockSettings(owner);
            return true;
        }

        if (mode == GBRS_RadarStationConstants.MODE_MANUAL)
        {
            if (!m_ManualConfig)
                m_ManualConfig = new GBRS_RadarManualConfig();
            m_ManualConfig.SeedFromFaction(m_eFactionPreset);
            ApplyManualSettings(owner);
            return true;
        }

        if (mode == GBRS_RadarStationConstants.MODE_PD_SEARCH)
        {
            ApplySearchSettings(owner);
            ConfigureLockLayer(false);
            return true;
        }

        return false;
    }

    protected void ApplyConfiguration(IEntity owner)
    {
        if (!owner)
            return;

        m_Radar = RDF_RadarComponent.Cast(owner.FindComponent(RDF_RadarComponent));
        if (!m_Radar)
            return;

        ApplySearchSettings(owner);

        // Leave m_bPowered alone — RplLoad / TogglePower own runtime power state.
        if (!m_bPowered)
            m_Radar.SetEnabled(false);

        m_iAntennaResolveAttempts = 0;
        EnsureAntennaResolved();
        if (!m_bPowered)
            SetAntennaSpinning(false);
        ApplyAntennaElevationVisual();

        m_bConfigured = true;

        if (m_bDebugLog)
        {
            Print("[GBRS-DEBUG] Configured faction=" + ((int)m_eFactionPreset).ToString()
                + " rpm=" + m_fScanRpm.ToString()
                + " forceLocal=1 powered=" + BoolDebugFlag(m_bPowered), LogLevel.WARNING);
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
        m_fProductUpdateIntervalS = settings.m_UpdateInterval;
        StampScanPhaseOnSettings(settings);
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

        if (m_bPowered)
            SetAntennaSpinning(true);

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
                + " esm=" + BoolDebugFlag(settings.m_EnableEsmReceive)
                + " ewFx=" + EwEffectCount(settings).ToString()
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

    // MANUAL workstation mode: pulse-Doppler search built from the operator's
    // live-tunable parameters (GBRS_RadarManualConfig). No lock layer.
    protected void ApplyManualSettings(IEntity owner)
    {
        if (!owner)
            return;

        if (!m_Radar)
            m_Radar = RDF_RadarComponent.Cast(owner.FindComponent(RDF_RadarComponent));
        if (!m_Radar)
            return;

        if (!m_ManualConfig)
            m_ManualConfig = new GBRS_RadarManualConfig();

        RDF_RadarSettings settings = RDF_RadarSensor.CreatePulseDopplerSettings(64);
        settings.m_Range = m_ManualConfig.m_RangeM;
        settings.m_UpdateInterval = m_ManualConfig.m_UpdateIntervalS;
        settings.m_SectorHalfAngleDeg = 180.0;
        settings.m_EnableMechanicalScan = true;
        settings.m_UseBoundsCenter = false;
        settings.m_UseLocalOffset = false;
        settings.m_OriginOffset = "0 0 0";
        settings.m_MaxLosTracesPerScan = 128;
        settings.m_FreshUpdateBudgetMin = 64;
        settings.m_FreshUpdateBudgetMax = 128;
        GBRS_RadarStationConfig.ApplyScattererDiscoveryBudget(settings);
        settings.m_IncludeVehicles = true;
        settings.m_IncludeProjectiles = false;
        settings.m_IncludeRadarEmitters = true;
        settings.m_MinDistance = 40.0;
        settings.m_EnablePhysicalDetection = true;
        settings.m_DetectionSnrDb = m_ManualConfig.m_DetectionSnrDb;
        settings.m_KeepUndetected = false;

        if (settings.m_Hardware)
        {
            // Start from the stock PD hardware block, then overlay operator tuning.
            GBRS_RadarStationConfig.ApplyPulseDopplerHardware(settings.m_Hardware);
            settings.m_Hardware.m_ScanRpm = m_ManualConfig.m_ScanRpm;
            settings.m_Hardware.m_AzimuthBeamwidthDeg = m_ManualConfig.m_AzimuthBeamwidthDeg;
            settings.m_Hardware.m_PeakPowerW = m_ManualConfig.m_PeakPowerW;
            settings.m_Hardware.ClearElevationBeams();
            settings.m_Hardware.AddElevationBeam(
                "manual",
                m_ManualConfig.m_ElevationBoresightDeg,
                m_ManualConfig.m_ElevationBeamwidthDeg,
                0.0);
            settings.m_Hardware.Validate();
        }

        m_fElevationBoresightDeg = m_ManualConfig.m_ElevationBoresightDeg;

        // Full fidelity + operator clutter / readout choices.
        GBRS_RadarStationConfig.ApplyFullFidelity(settings);
        settings.m_DemClutterScale = m_ManualConfig.m_DemClutterScale;
        settings.m_CfarMode = ERDF_CfarMode.RDF_CFAR_CA;
        settings.Validate();

        PushSensorSettings(owner, settings, ERDF_RadarSensorMode.RDF_RADAR_MODE_PULSE_DOPPLER);
        ConfigureLockLayer(false);

        if (m_bDebugLog)
        {
            Print("[GBRS-DEBUG] ManualSettings range=" + settings.m_Range.ToString()
                + " snr=" + m_ManualConfig.m_DetectionSnrDb.ToString()
                + " dem=" + m_ManualConfig.m_DemClutterScale.ToString()
                + " rpm=" + m_ManualConfig.m_ScanRpm.ToString()
                + " azBw=" + m_ManualConfig.m_AzimuthBeamwidthDeg.ToString()
                + " pw=" + m_ManualConfig.m_PeakPowerW.ToString(), LogLevel.WARNING);
        }
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

        m_fProductUpdateIntervalS = settings.m_UpdateInterval;
        StampScanPhaseOnSettings(settings);
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

        if (m_bPowered)
            SetAntennaSpinning(true);
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
            if (m_bLockLayerEnabled != false)
            {
                m_bLockLayerEnabled = false;
                if (GBRS_RadarStationEvents.OnLockChanged)
                    GBRS_RadarStationEvents.OnLockChanged.Invoke(this, false);
            }
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

        if (m_bLockLayerEnabled != enableAutoLock)
        {
            m_bLockLayerEnabled = enableAutoLock;
            if (GBRS_RadarStationEvents.OnLockChanged)
                GBRS_RadarStationEvents.OnLockChanged.Invoke(this, enableAutoLock);
        }
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

    // USSR Tesla RPL-5: spin antenna_rotation bone via Animation.SetBone.
    // US AN/TPN-19: first mesh child that is not the generator (no spin bone).
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

        if (FindBoneSpinAntenna(owner))
            return;

        // RPL-5 must stay on bone spin only — never yaw the whole pedestal mesh.
        // The bone may stream in a few ticks after EditorLink spawn.
        if (m_eFactionPreset == EGBRS_RadarFactionPreset.USSR)
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
    protected bool FindBoneSpinAntenna(IEntity root)
    {
        if (!root)
            return false;

        IEntity child = root.GetChildren();
        while (child)
        {
            if (TryBindBoneSpinAntenna(child))
                return true;

            if (FindBoneSpinAntenna(child))
                return true;

            child = child.GetSibling();
        }

        return false;
    }

    protected bool TryBindBoneSpinAntenna(IEntity ent)
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

        // On stop, leave the antenna at its current angle. EOnFrame early-outs
        // on !m_bPowered, so SyncAntennaBoneSpin / SyncEntityYawAntenna stop
        // driving and the last bone matrix / entity yaw persists — the antenna
        // freezes where it is instead of snapping back to the initial bearing.
        // (Re-power resumes from the world-time scan angle, matching RDF scan.)
    }

    // Tesla RPL-5: drive antenna_rotation bone (pedestal stays fixed).
    // Use SetBoneMatrix + AnglesToMatrix (degrees) so rate matches RDF ScanRpm.
    // ProcAnim is forced off every frame — stock pap would otherwise spin faster.
    protected void SyncAntennaBoneSpin(IEntity owner)
    {
        if (!m_bUseBoneSpin || !m_AntennaEntity)
            return;

        if (m_iAntennaSpinBone < 0)
            return;

        if (m_AntennaProcAnim && m_AntennaProcAnim.IsActive())
            m_AntennaProcAnim.Deactivate(m_AntennaEntity);

        Animation anim = m_AntennaEntity.GetAnimation();
        if (!anim)
            return;

        float localYawDeg = 0.0;
        if (m_bAntennaStare)
        {
            localYawDeg = GetFrozenAntennaLocalYaw(owner);
        }
        else
        {
            float rpm = GetLiveScanRpm();
            if (rpm <= 0.0)
                return;

            BaseWorld world = GetGame().GetWorld();
            if (!world)
                return;

            float worldTimeS = world.GetWorldTime() * 0.001;
            float angleRad = worldTimeS * rpm * Math.PI * 2.0 / 60.0 + m_fScanPhaseOffsetRad;
            m_fAntennaFrozenAngleRad = angleRad;
            float worldYawDeg = 90.0 - (angleRad * Math.RAD2DEG);
            vector antYpr = m_AntennaEntity.GetYawPitchRoll();
            localYawDeg = worldYawDeg - antYpr[0];
        }

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

    // Tesla RPL-5: never yaw/pitch the whole ProcAnim entity — that rotates the pedestal.
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
        if (m_bAntennaStare)
        {
            float stareRad = m_fAntennaStareAzDeg * 0.017453292519943295;
            return Vector(Math.Cos(stareRad), 0.0, Math.Sin(stareRad));
        }

        float rpm = GetLiveScanRpm();
        if (rpm > 0.0)
        {
            BaseWorld world = GetGame().GetWorld();
            if (world)
            {
                // Must match RDF_RadarScanner.GetScanForward exactly (plus the
                // pause/resume phase offset so HUD sweep tracks the antenna).
                float worldTimeS = world.GetWorldTime() * 0.001;
                float angleRad = worldTimeS * rpm * Math.PI * 2.0 / 60.0 + m_fScanPhaseOffsetRad;
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
        m_DetectVisual.Ingest(sensor.GetPlots(), cfg, origin, nowS);
        m_DetectVisual.Draw(origin, GetLiveScanRpm(), nowS);
    }

    //------------------------------------------------------------------------------------------------
    //! Lightweight public-event contact tracker.
    //! Fires OnRadarContact / OnRadarContactLost when detected plots appear or
    //! time out. Keyed by RDF scatterer id so identity-stripped plots still work.
    protected void UpdateContactEvents(IEntity owner)
    {
        if (!m_Radar || !m_Contacts || !m_ContactLastSeen)
            return;

        float nowS = System.GetTickCount() * 0.001;
        if (nowS < m_fNextContactUpdateS)
            return;

        m_fNextContactUpdateS = nowS + CONTACT_UPDATE_INTERVAL_S;

        RDF_RadarSensor sensor = m_Radar.GetSensor();
        if (!sensor)
            return;

        RDF_RadarSettings settings = sensor.GetSettings();
        array<ref RDF_RadarTarget> plots = sensor.GetPlots();
        array<int> seen = {};

        if (plots)
        {
            int i = 0;
            while (i < plots.Count())
            {
                RDF_RadarTarget t = plots.Get(i);
                i = i + 1;
                if (!t || !t.m_Detected)
                    continue;

                if (!GBRS_RadarStationConfig.ShouldDisplayPlot(t, settings))
                    continue;

                int id = t.m_ScattererId;
                if (id <= 0)
                    continue;

                seen.Insert(id);

                RDF_RadarTarget old = m_Contacts.Get(id);
                m_Contacts.Set(id, t);
                m_ContactLastSeen.Set(id, nowS);

                if (!old)
                {
                    if (GBRS_RadarStationEvents.OnRadarContact)
                        GBRS_RadarStationEvents.OnRadarContact.Invoke(this, t);

                }
            }
        }

        // Emit lost events for contacts that have not been seen for a while.
        array<int> staleKeys = {};
        foreach (int id, RDF_RadarTarget old : m_Contacts)
        {
            if (seen.Contains(id))
                continue;

            float last = m_ContactLastSeen.Get(id);
            if (nowS - last >= CONTACT_LOST_TIMEOUT_S)
                staleKeys.Insert(id);
        }

        foreach (int id : staleKeys)
        {
            RDF_RadarTarget old = m_Contacts.Get(id);
            m_Contacts.Remove(id);
            m_ContactLastSeen.Remove(id);
            if (old && GBRS_RadarStationEvents.OnRadarContactLost)
                GBRS_RadarStationEvents.OnRadarContactLost.Invoke(this, old);

        }
    }

    //------------------------------------------------------------------------------------------------
    //! WLR fire-solution event emitter. Fires once per confirmed WLR track
    //! after the ballistic solver returns a usable launch/impact fix.
    protected void UpdateWlrEvents(IEntity owner)
    {
        if (!m_Radar || !m_WlrFiredTrackIds)
            return;

        if (m_WorkstationMode != GBRS_RadarStationConstants.MODE_WLR)
            return;

        RDF_RadarSensor sensor = m_Radar.GetSensor();
        if (!sensor)
            return;

        RDF_RadarProjectileTracker tracker = sensor.GetTracker();
        if (!tracker)
            return;

        array<ref RDF_RadarTrack> tracks = tracker.GetAllTracks();
        if (!tracks)
            return;

        foreach (RDF_RadarTrack tr : tracks)
        {
            if (!tr || !tr.m_Confirmed)
                continue;

            int id = tr.m_TrackId;
            if (m_WlrFiredTrackIds.Contains(id))
                continue;

            GBRS_RadarWlrSolution sol = GBRS_RadarWlrBallisticSolver.Resolve(tr);
            if (!sol || !sol.m_Fix)
                continue;

            RDF_RadarWlrFix fix = sol.m_Fix;
            if (!fix.m_ImpactValid)
                continue;

            m_WlrFiredTrackIds.Set(id, true);
            if (GBRS_RadarStationEvents.OnWlrSolution)
                GBRS_RadarStationEvents.OnWlrSolution.Invoke(this, fix);
        }
    }

    //------------------------------------------------------------------------------------------------
    protected void ClearContactEvents()
    {
        if (m_Contacts)
            m_Contacts.Clear();
        if (m_ContactLastSeen)
            m_ContactLastSeen.Clear();
        if (m_WlrFiredTrackIds)
            m_WlrFiredTrackIds.Clear();
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

    protected int EwEffectCount(RDF_RadarSettings settings)
    {
        if (!settings || !settings.m_EwStack || !settings.m_EwStack.m_Effects)
            return 0;
        return settings.m_EwStack.m_Effects.Count();
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

        // Live RDF physics samples (parity check vs offline tools/calib_pd_full.py).
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

    // Matches RDF_RadarScanner.GetScanForward for AN/TPN-19 entity yaw.
    protected void SyncEntityYawAntenna(IEntity owner)
    {
        if (!m_bUseEntityYawSpin || !m_AntennaEntity)
            return;

        float localYawDeg = 0.0;
        if (m_bAntennaStare)
        {
            localYawDeg = GetFrozenAntennaLocalYaw(owner);
        }
        else
        {
            float rpm = GetLiveScanRpm();
            if (rpm <= 0.0)
                return;

            BaseWorld world = GetGame().GetWorld();
            if (!world)
                return;

            float worldTimeS = world.GetWorldTime() * 0.001;
            float angleRad = worldTimeS * rpm * Math.PI * 2.0 / 60.0 + m_fScanPhaseOffsetRad;
            m_fAntennaFrozenAngleRad = angleRad;
            float worldYawDeg = 90.0 - (angleRad * Math.RAD2DEG);
            vector ownerYpr = owner.GetYawPitchRoll();
            localYawDeg = worldYawDeg - ownerYpr[0];
        }

        m_AntennaEntity.SetYawPitchRoll(
            Vector(localYawDeg, m_fElevationBoresightDeg, m_fAntennaBaseRoll));
    }

    // Stare pose is a cached local yaw so the mesh does not fight the spinning
    // world-clock each frame (that cancellation leaves a visible twitch).
    protected float GetFrozenAntennaLocalYaw(IEntity owner)
    {
        if (m_bStarePoseCached)
            return m_fStareLocalYawDeg;

        float worldYawDeg = 90.0 - m_fAntennaStareAzDeg;
        if (m_bUseEntityYawSpin && owner)
        {
            vector ownerYpr = owner.GetYawPitchRoll();
            m_fStareLocalYawDeg = worldYawDeg - ownerYpr[0];
        }
        else if (m_AntennaEntity)
        {
            vector antYpr = m_AntennaEntity.GetYawPitchRoll();
            m_fStareLocalYawDeg = worldYawDeg - antYpr[0];
        }
        else
        {
            m_fStareLocalYawDeg = worldYawDeg;
        }

        m_bStarePoseCached = true;
        return m_fStareLocalYawDeg;
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

        BindCoveringBaseFactionListener();
        SyncWithCoveringCampaignBase();
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

    protected bool ResourceHasSupplyTick(notnull SCR_ResourceComponent resourceComponent)
    {
        float available;
        if (!SCR_ResourceSystemHelper.GetAvailableResources(resourceComponent, available))
            return false;

        if (available >= m_fSupplyCostPerTick)
            return true;

        return false;
    }

    // Nearby Campaign base whose radius covers this station.
    // sameFactionOnly: only the occupying faction that currently matches the
    // station, used when draining HQ supplies.
    protected SCR_CampaignMilitaryBaseComponent FindCoveringCampaignBase(bool sameFactionOnly)
    {
        IEntity owner = GetOwner();
        if (!owner)
            return null;

        SCR_MilitaryBaseSystem baseSystem = SCR_MilitaryBaseSystem.GetInstance();
        if (!baseSystem)
            return null;

        array<SCR_MilitaryBaseComponent> bases = {};
        baseSystem.GetBases(bases);
        if (bases.IsEmpty())
            return null;

        Faction stationFaction;
        if (sameFactionOnly)
            stationFaction = SCR_Faction.GetEntityFaction(owner);

        vector origin = owner.GetOrigin();
        SCR_CampaignMilitaryBaseComponent best = null;
        float bestDistSq = -1.0;

        foreach (SCR_MilitaryBaseComponent base : bases)
        {
            if (!base)
                continue;

            SCR_CampaignMilitaryBaseComponent campaignBase =
                SCR_CampaignMilitaryBaseComponent.Cast(base);
            if (!campaignBase)
                continue;

            if (sameFactionOnly)
            {
                Faction baseFaction = campaignBase.GetFaction();
                if (!baseFaction || !stationFaction)
                    continue;

                if (baseFaction != stationFaction)
                    continue;
            }

            IEntity baseOwner = campaignBase.GetOwner();
            if (!baseOwner)
                continue;

            float radius = campaignBase.GetRadius();
            if (radius <= 0.0)
                continue;

            float distSq = vector.DistanceSqXZ(origin, baseOwner.GetOrigin());
            if (distSq > (radius * radius))
                continue;

            if (bestDistSq < 0.0 || distSq < bestDistSq)
            {
                bestDistSq = distSq;
                best = campaignBase;
            }
        }

        return best;
    }

    protected SCR_ResourceComponent ResolveFriendlyBaseResourceComponent()
    {
        SCR_CampaignMilitaryBaseComponent covering = FindCoveringCampaignBase(true);
        if (!covering)
            return null;

        return covering.GetResourceComponent();
    }

    protected void BindCoveringBaseFactionListener()
    {
        if (m_bBaseFactionListenerBound)
            return;

        if (!GetGame().InPlayMode())
            return;

        if (!IsAuthority())
            return;

        SCR_MilitaryBaseSystem baseSystem = SCR_MilitaryBaseSystem.GetInstance();
        if (!baseSystem)
            return;

        baseSystem.GetOnBaseFactionChanged().Insert(OnCoveringBaseFactionChanged);
        m_bBaseFactionListenerBound = true;
        SyncWithCoveringCampaignBase();
    }

    protected void UnbindCoveringBaseFactionListener()
    {
        if (!m_bBaseFactionListenerBound)
            return;

        SCR_MilitaryBaseSystem baseSystem = SCR_MilitaryBaseSystem.GetInstance();
        if (baseSystem)
            baseSystem.GetOnBaseFactionChanged().Remove(OnCoveringBaseFactionChanged);

        m_bBaseFactionListenerBound = false;
    }

    protected void OnCoveringBaseFactionChanged(SCR_MilitaryBaseComponent base, Faction faction)
    {
        if (!base)
            return;

        SyncWithCoveringCampaignBase();
    }

    // Covering Conflict base captured or recaptured: drop power immediately and
    // hand the station to the occupying faction. Hardware preset stays as built.
    protected void SyncWithCoveringCampaignBase()
    {
        if (!IsAuthority())
            return;

        if (IsDestroyed())
            return;

        SCR_CampaignMilitaryBaseComponent covering = FindCoveringCampaignBase(false);
        if (!covering)
            return;

        Faction occupying = covering.GetFaction();
        IEntity owner = GetOwner();
        if (!owner)
            return;

        Faction stationFaction = SCR_Faction.GetEntityFaction(owner);
        if (occupying && occupying == stationFaction)
            return;

        if (m_bPowered)
            SetPowered(false);

        if (!occupying)
            return;

        AdoptOccupyingFaction(occupying);
    }

    protected void AdoptOccupyingFaction(notnull Faction occupying)
    {
        string factionKey = occupying.GetFactionKey();
        ApplyAffiliatedFactionKey(factionKey);

        if (!IsAuthority())
            return;

        Rpc(RpcDo_AdoptOccupyingFaction, factionKey);
    }

    protected string GetAffiliatedFactionKey()
    {
        IEntity owner = GetOwner();
        if (!owner)
            return "";

        FactionAffiliationComponent affiliation =
            FactionAffiliationComponent.Cast(owner.FindComponent(FactionAffiliationComponent));
        if (!affiliation)
            return "";

        return affiliation.GetAffiliatedFactionKey();
    }

    protected void ApplyAffiliatedFactionKey(string factionKey)
    {
        if (factionKey == "")
            return;

        IEntity owner = GetOwner();
        if (!owner)
            return;

        FactionAffiliationComponent affiliation =
            FactionAffiliationComponent.Cast(owner.FindComponent(FactionAffiliationComponent));
        if (!affiliation)
            return;

        affiliation.SetAffiliatedFactionByKey(factionKey);

        if (m_bPowered)
            SyncIntelRadio(true);
    }

    protected SCR_ResourceComponent ResolveLinkedProviderResourceComponent()
    {
        IEntity owner = GetOwner();
        if (!owner)
            return null;

        SCR_CampaignBuildingCompositionComponent composition =
            SCR_CampaignBuildingCompositionComponent.Cast(
                owner.FindComponent(SCR_CampaignBuildingCompositionComponent));
        if (!composition)
            return null;

        IEntity providerEntity = composition.GetProviderEntity();
        if (!providerEntity)
            return null;

        SCR_CampaignBuildingProviderComponent provider =
            SCR_CampaignBuildingProviderComponent.Cast(
                providerEntity.FindComponent(SCR_CampaignBuildingProviderComponent));
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
                SCR_CampaignBuildingProviderComponent.Cast(
                    masterEntity.FindComponent(SCR_CampaignBuildingProviderComponent));
            if (masterFromEntity)
                return masterFromEntity.GetResourceComponent();
        }

        return provider.GetResourceComponent();
    }

    protected SCR_ResourceComponent ResolveSupplyResourceComponent()
    {
        IEntity owner = GetOwner();
        if (!owner)
            return null;

        SCR_ResourceComponent baseResource = ResolveFriendlyBaseResourceComponent();
        if (baseResource && ResourceHasSupplyTick(baseResource))
            return baseResource;

        SCR_ResourceComponent localResource =
            SCR_ResourceComponent.FindResourceComponent(owner);
        if (localResource && ResourceHasSupplyTick(localResource))
            return localResource;

        SCR_ResourceComponent providerResource = ResolveLinkedProviderResourceComponent();
        if (providerResource && ResourceHasSupplyTick(providerResource))
            return providerResource;

        if (baseResource)
            return baseResource;

        if (localResource)
            return localResource;

        return providerResource;
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
        ClearContactEvents();
        m_bLockLayerEnabled = false;
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
        m_fScanPhaseOffsetRad = 0.0;
        m_fAntennaFrozenAngleRad = 0.0;
        m_bAntennaFrozen = false;
        m_bAntennaStare = false;
        m_fAntennaStareAzDeg = 0.0;
        m_bStarePhaseCleared = true;
        m_bStarePoseCached = false;
        if (m_DetectVisual)
            m_DetectVisual.Clear();
        if (m_aGeomLayerBackups)
            m_aGeomLayerBackups.Clear();
        SyncIntelRadio(false);
    }

    //------------------------------------------------------------------------------------------------
    protected void SyncIntelRadio(bool powered)
    {
        GBRS_IntelRadioNet.ConfigureStationRadio(this, powered);
    }
}
