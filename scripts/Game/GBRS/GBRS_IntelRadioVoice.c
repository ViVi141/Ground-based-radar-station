//------------------------------------------------------------------------------------------------
//! Local radio-processed VO for intel-net delivery. Uses vanilla HQ / radio
//! protocol banks (same path as Conflict HQ announcer), so GM still works.
[EntityEditorProps(category: "GameScripted/GBRS", description: "Intel-net radio voice bank")]
class GBRS_IntelRadioSoundEntityClass : GenericEntityClass
{
}

class GBRS_IntelRadioSoundEntity : GenericEntity
{
    protected SimpleSoundComponent m_SoundComp;
    protected AudioHandle m_PlayedRadio = AudioHandle.Invalid;
    protected static GBRS_IntelRadioSoundEntity s_Instance;

    //------------------------------------------------------------------------------------------------
    static GBRS_IntelRadioSoundEntity GetInstance()
    {
        if (s_Instance)
            return s_Instance;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return null;

        Resource resource = Resource.Load(GBRS_RadarStationConstants.PREFAB_INTEL_RADIO_SOUND);
        if (!resource)
            return null;
        if (!resource.IsValid())
            return null;

        s_Instance = GBRS_IntelRadioSoundEntity.Cast(GetGame().SpawnEntityPrefab(resource, world));
        return s_Instance;
    }

    //------------------------------------------------------------------------------------------------
    static void PlayIntelVoice(int voiceKind, int gridPacked, float quality)
    {
        GBRS_IntelRadioSoundEntity soundEntity = GetInstance();
        if (!soundEntity)
            return;

        soundEntity.Play(voiceKind, gridPacked, quality);
    }

    //------------------------------------------------------------------------------------------------
    void Play(int voiceKind, int gridPacked, float quality)
    {
        SimpleSoundComponent soundComp = GetSoundComponent();
        if (!soundComp)
            return;

        BaseContainer settings = GetGame().GetGameUserSettings().GetModule("SCR_AudioSettings");
        bool announcerEnabled = true;
        if (settings)
            settings.Get("m_bHQAnnouncer", announcerEnabled);
        if (!announcerEnabled)
            return;

        int signalQuality = soundComp.GetSignalIndex("TransmissionQuality");
        if (signalQuality >= 0)
            soundComp.SetSignalValue(signalQuality, quality);

        int signalSeed = soundComp.GetSignalIndex("Seed");
        if (signalSeed >= 0)
            soundComp.SetSignalValue(signalSeed, Math.RandomFloat01());

        if (gridPacked > 0)
        {
            int signalGrid = soundComp.GetSignalIndex("Grid");
            if (signalGrid >= 0)
                soundComp.SetSignalValue(signalGrid, gridPacked);
        }

        SCR_Faction faction = SCR_Faction.Cast(SCR_FactionManager.SGetLocalPlayerFaction());
        if (faction)
        {
            int signalIdentityVoice = soundComp.GetSignalIndex("IdentityVoice");
            if (signalIdentityVoice >= 0)
                soundComp.SetSignalValue(signalIdentityVoice, faction.GetIndentityVoiceSignal());
        }

        string eventName;
        if (voiceKind == GBRS_RadarStationConstants.INTEL_VOICE_WLR)
        {
            eventName = SCR_SoundEvent.SOUND_HQC_M_BASEUNDERATTACK_COMMANDER;
        }
        else
        {
            int signalObjectType = soundComp.GetSignalIndex("ObjectType");
            if (signalObjectType >= 0)
                soundComp.SetSignalValue(signalObjectType, 1.0);

            int signalVehicle = soundComp.GetSignalIndex("Vehicle");
            if (signalVehicle >= 0)
                soundComp.SetSignalValue(signalVehicle, ECP_VehicleTypes.HELICOPTER);

            int signalVehicleFaction = soundComp.GetSignalIndex("VehicleFaction");
            if (signalVehicleFaction >= 0)
                soundComp.SetSignalValue(signalVehicleFaction, 100.0 + ECP_VehicleTypes.HELICOPTER);

            eventName = SCR_SoundEvent.SOUND_CP_SPOTTED_LONG;
        }

        AudioSystem.TerminateSound(m_PlayedRadio);
        m_PlayedRadio = soundComp.PlayStr(eventName);
        if (m_PlayedRadio == AudioHandle.Invalid)
        {
            string fallback = SCR_SoundEvent.SOUND_HQ_BUA;
            if (faction)
                fallback = fallback + "_" + faction.GetFactionKey();
            m_PlayedRadio = soundComp.PlayStr(fallback);
        }
    }

    //------------------------------------------------------------------------------------------------
    SimpleSoundComponent GetSoundComponent()
    {
        if (!m_SoundComp)
            m_SoundComp = SimpleSoundComponent.Cast(FindComponent(SimpleSoundComponent));
        return m_SoundComp;
    }

    //------------------------------------------------------------------------------------------------
    void ~GBRS_IntelRadioSoundEntity()
    {
        s_Instance = null;
    }
}
