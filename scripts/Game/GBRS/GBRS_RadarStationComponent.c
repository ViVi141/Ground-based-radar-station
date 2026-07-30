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
    // Layers RDF LOS (Projectile preset) and camera/view rays typically hit.
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

    [Attribute("4", UIWidgets.Slider, desc: "Radar elevation boresight in degrees (detection + visual tilt)", params: "-5 85 0.5")]
    protected float m_fElevationBoresightDeg;

    [Attribute("8", UIWidgets.Slider, desc: "Elevation beamwidth in degrees", params: "1 60 0.5")]
    protected float m_fElevationBeamwidthDeg;

    protected bool m_bPowered;
    protected RDF_RadarComponent m_Radar;
    protected IEntity m_AntennaEntity;
    protected ProcAnimComponent m_AntennaProcAnim;
    protected bool m_bUseEntityYawSpin;
    protected bool m_bConfigured;
    protected bool m_bSupplyTickScheduled;
    protected bool m_bTraceIgnoreActive;
    protected int m_iAntennaResolveAttempts;
    protected float m_fScanRpm;
    protected float m_fAntennaBasePitch;
    protected float m_fAntennaBaseRoll;
    protected ref array<ref GBRS_GeomLayerBackup> m_aGeomLayerBackups;

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

        SyncEntityYawAntenna(owner);
    }

    override void OnDelete(IEntity owner)
    {
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
        if (!m_bConfigured)
            ApplyConfiguration(GetOwner());

        if (!m_Radar)
            return;

        if (powered == m_bPowered)
            return;

        if (powered)
        {
            if (!CanAffordPowerOn())
                return;

            m_bPowered = true;
            m_Radar.SetEnabled(true);
            EnsureAntennaResolved();
            SetAntennaSpinning(true);
            SetTraceIgnoreActive(true);

            if (IsAuthority())
                StartSupplyDrain();
        }
        else
        {
            m_bPowered = false;
            m_Radar.SetEnabled(false);
            SetAntennaSpinning(false);
            SetTraceIgnoreActive(false);
            StopSupplyDrain();
        }
    }

    protected void ApplyConfiguration(IEntity owner)
    {
        if (!owner)
            return;

        m_Radar = RDF_RadarComponent.Cast(owner.FindComponent(RDF_RadarComponent));
        if (!m_Radar)
            return;

        RDF_RadarSettings settings;
        if (m_eFactionPreset == EGBRS_RadarFactionPreset.USSR)
        {
            settings = GBRS_RadarStationConfig.CreateUssrSearch();
        }
        else
        {
            settings = GBRS_RadarStationConfig.CreateUsSearch();
        }

        ApplyElevationToSettings(settings);

        m_Radar.GetSensor().Configure(settings);
        m_Radar.SetEventMask(owner, EntityEvent.FRAME);

        m_Radar.SetEnabled(false);
        m_bPowered = false;

        m_fScanRpm = 0.0;
        if (settings.m_Hardware)
            m_fScanRpm = settings.m_Hardware.m_ScanRpm;

        m_bConfigured = true;

        m_iAntennaResolveAttempts = 0;
        EnsureAntennaResolved();
        SetAntennaSpinning(false);
        ApplyAntennaElevationVisual();
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

        settings.m_Hardware.ClearElevationBeams();
        settings.m_Hardware.AddElevationBeam(
            "main",
            m_fElevationBoresightDeg,
            m_fElevationBeamwidthDeg,
            0.0);
        settings.m_Hardware.Validate();
        settings.Validate();
    }

    protected void EnsureAntennaResolved()
    {
        ResolveAntennaEntity(GetOwner());
        if (m_AntennaEntity)
        {
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

    // US: child with ProcAnim (RPL-5 antenna_rotation).
    // USSR: first mesh child that is not the generator (TPN-19 has no spin bone).
    protected void ResolveAntennaEntity(IEntity owner)
    {
        m_AntennaEntity = null;
        m_AntennaProcAnim = null;
        m_bUseEntityYawSpin = false;
        m_fAntennaBasePitch = 0.0;
        m_fAntennaBaseRoll = 0.0;
        if (!owner)
            return;

        IEntity child = owner.GetChildren();
        while (child)
        {
            ProcAnimComponent procAnim = ProcAnimComponent.Cast(child.FindComponent(ProcAnimComponent));
            if (procAnim)
            {
                m_AntennaEntity = child;
                m_AntennaProcAnim = procAnim;
                m_bUseEntityYawSpin = false;
                return;
            }

            child = child.GetSibling();
        }

        child = owner.GetChildren();
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

        if (m_AntennaProcAnim)
        {
            if (spinning)
            {
                m_AntennaProcAnim.Activate(m_AntennaEntity);
            }
            else
            {
                // Always deactivate; stock prefab may auto-start ProcAnim on spawn.
                m_AntennaProcAnim.Deactivate(m_AntennaEntity);
            }

            ApplyAntennaElevationVisual();
            return;
        }

        if (!spinning && m_bUseEntityYawSpin)
        {
            m_AntennaEntity.SetYawPitchRoll(
                Vector(0.0, m_fElevationBoresightDeg, m_fAntennaBaseRoll));
        }
    }

    // RPL-5 has no elevation bone; tilt the antenna entity while ProcAnim spins yaw.
    protected void ApplyAntennaElevationVisual()
    {
        if (!m_AntennaEntity)
            return;

        if (m_bUseEntityYawSpin)
            return;

        m_AntennaEntity.SetYawPitchRoll(
            Vector(0.0, m_fElevationBoresightDeg, 0.0));
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

        if (m_fScanRpm <= 0.0)
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return;

        float worldTimeS = world.GetWorldTime() * 0.001;
        float angleRad = worldTimeS * m_fScanRpm * Math.PI * 2.0 / 60.0;
        vector scanForward = Vector(Math.Cos(angleRad), 0.0, Math.Sin(angleRad));

        float worldYawDeg = Math.Atan2(scanForward[0], scanForward[2]) * Math.RAD2DEG;
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
        m_bUseEntityYawSpin = false;
        m_bConfigured = false;
        m_bPowered = false;
        m_bTraceIgnoreActive = false;
        m_fScanRpm = 0.0;
        if (m_aGeomLayerBackups)
            m_aGeomLayerBackups.Clear();
    }
}
