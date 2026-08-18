//------------------------------------------------------------------------------------------------
//! Local intel-net VO. Custom TTS clips in GBRS_IntelRadio.acp.
//! Air: prefix + grid + heading + altitude + out.
//! WLR: prefix + launch grid + impact grid + ETA + out.
//! Grid packing is EEE*1000+NNN.
//! Playback follows the local handheld / backpack / editor radio.
[EntityEditorProps(category: "GameScripted/GBRS", description: "Intel-net radio voice bank")]
class GBRS_IntelRadioSoundEntityClass : GenericEntityClass
{
}

class GBRS_IntelRadioSoundEntity : GenericEntity
{
    protected SimpleSoundComponent m_SoundComp;
    protected AudioHandle m_PlayedRadio = AudioHandle.Invalid;
    protected AudioHandle m_PlayedHiss = AudioHandle.Invalid;
    protected static GBRS_IntelRadioSoundEntity s_Instance;
    protected int m_iPendingVoiceKind;
    protected int m_iPendingGridPacked;
    protected int m_iPendingParamA;
    protected int m_iPendingParamB;
    protected float m_fPendingQuality;
    protected string m_sPendingFactionKey;
    protected bool m_bRetryQueued;
    protected bool m_bDeferredPending;
    protected int m_iDeferredVoiceKind;
    protected int m_iDeferredGridPacked;
    protected int m_iDeferredParamA;
    protected int m_iDeferredParamB;
    protected float m_fDeferredQuality;
    protected string m_sDeferredFactionKey;
    protected ref array<string> m_aClipQueue = new array<string>();
    protected int m_iClipIndex;
    protected bool m_bQueueActive;

    //------------------------------------------------------------------------------------------------
    static GBRS_IntelRadioSoundEntity GetInstance()
    {
        if (s_Instance)
            return s_Instance;

        s_Instance = SpawnSoundEntity(GBRS_RadarStationConstants.PREFAB_INTEL_RADIO_SOUND);
        return s_Instance;
    }

    //------------------------------------------------------------------------------------------------
    protected static GBRS_IntelRadioSoundEntity SpawnSoundEntity(ResourceName prefab)
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return null;
        if (prefab.IsEmpty())
            return null;

        Resource resource = Resource.Load(prefab);
        if (!resource)
            return null;
        if (!resource.IsValid())
            return null;

        GenericEntity spawned = GenericEntity.Cast(GetGame().SpawnEntityPrefab(resource, world));
        if (!spawned)
            return null;

        return GBRS_IntelRadioSoundEntity.Cast(spawned);
    }

    //------------------------------------------------------------------------------------------------
    static void PlayIntelVoice(
        int voiceKind,
        int gridPacked,
        int paramA,
        int paramB,
        float quality,
        string factionKey,
        bool interrupt)
    {
        GBRS_IntelRadioSoundEntity soundEntity = GetInstance();
        if (soundEntity)
        {
            soundEntity.Play(voiceKind, gridPacked, paramA, paramB, quality, factionKey, interrupt);
            return;
        }

        PlayUiFallback(factionKey);
    }

    //------------------------------------------------------------------------------------------------
    protected static void PlayUiFallback(string factionKey)
    {
        string chatter = SCR_SoundEvent.SOUND_RADIO_CHATTER_US;
        if (factionKey == "USSR")
            chatter = SCR_SoundEvent.SOUND_RADIO_CHATTER_RU;

        SCR_UISoundEntity.SoundEvent(chatter);
    }

    //------------------------------------------------------------------------------------------------
    void Play(
        int voiceKind,
        int gridPacked,
        int paramA,
        int paramB,
        float quality,
        string factionKey,
        bool interrupt)
    {
        if (interrupt)
        {
            m_bDeferredPending = false;
            StopQueue();
        }
        else if (m_bQueueActive)
        {
            DeferOrIgnore(voiceKind, gridPacked, paramA, paramB, quality, factionKey);
            return;
        }

        m_iPendingVoiceKind = voiceKind;
        m_iPendingGridPacked = gridPacked;
        m_iPendingParamA = paramA;
        m_iPendingParamB = paramB;
        m_fPendingQuality = quality;
        m_sPendingFactionKey = factionKey;

        if (TryPlay())
            return;

        if (m_bRetryQueued)
            return;

        m_bRetryQueued = true;
        GetGame().GetCallqueue().CallLater(RetryPlay, 200, false);
    }

    //------------------------------------------------------------------------------------------------
    //! Keep the clip already on air. Same launch/impact (or same air grid)
    //! is dropped; a different report waits until Out.
    protected void DeferOrIgnore(
        int voiceKind,
        int gridPacked,
        int paramA,
        int paramB,
        float quality,
        string factionKey)
    {
        if (IsSameReport(
            m_iPendingVoiceKind,
            m_iPendingGridPacked,
            m_iPendingParamA,
            m_sPendingFactionKey,
            voiceKind,
            gridPacked,
            paramA,
            factionKey))
        {
            return;
        }

        if (m_bDeferredPending)
        {
            if (IsSameReport(
                m_iDeferredVoiceKind,
                m_iDeferredGridPacked,
                m_iDeferredParamA,
                m_sDeferredFactionKey,
                voiceKind,
                gridPacked,
                paramA,
                factionKey))
            {
                return;
            }
        }

        m_bDeferredPending = true;
        m_iDeferredVoiceKind = voiceKind;
        m_iDeferredGridPacked = gridPacked;
        m_iDeferredParamA = paramA;
        m_iDeferredParamB = paramB;
        m_fDeferredQuality = quality;
        m_sDeferredFactionKey = factionKey;
    }

    //------------------------------------------------------------------------------------------------
    protected bool IsSameReport(
        int kindA,
        int gridA,
        int paramAA,
        string factionA,
        int kindB,
        int gridB,
        int paramAB,
        string factionB)
    {
        if (kindA != kindB)
            return false;
        if (gridA != gridB)
            return false;
        if (paramAA != paramAB)
            return false;
        if (factionA != factionB)
            return false;
        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected bool TryStartDeferred()
    {
        if (!m_bDeferredPending)
            return false;

        m_bDeferredPending = false;
        m_iPendingVoiceKind = m_iDeferredVoiceKind;
        m_iPendingGridPacked = m_iDeferredGridPacked;
        m_iPendingParamA = m_iDeferredParamA;
        m_iPendingParamB = m_iDeferredParamB;
        m_fPendingQuality = m_fDeferredQuality;
        m_sPendingFactionKey = m_sDeferredFactionKey;
        return TryPlay();
    }

    //------------------------------------------------------------------------------------------------
    protected void RetryPlay()
    {
        m_bRetryQueued = false;
        if (TryPlay())
            return;

        PlayUiFallback(m_sPendingFactionKey);
    }

    //------------------------------------------------------------------------------------------------
    protected bool TryPlay()
    {
        SimpleSoundComponent soundComp = GetSoundComponent();
        if (!soundComp)
            return false;

        SnapToLocalRadio();
        soundComp.SetSignalValueStr("TransmissionQuality", m_fPendingQuality);

        string factionKey = m_sPendingFactionKey;
        if (factionKey.IsEmpty())
            factionKey = ResolveLocalFactionKey();
        if (factionKey != "USSR")
            factionKey = "US";

        StopQueue();
        BuildClipQueue(
            m_aClipQueue,
            m_iPendingVoiceKind,
            m_iPendingGridPacked,
            m_iPendingParamA,
            m_iPendingParamB,
            factionKey);
        if (m_aClipQueue.Count() == 0)
            return false;

        m_iClipIndex = 0;
        m_bQueueActive = true;
        EnsureHiss();
        if (!PlayNextClip())
        {
            m_bQueueActive = false;
            StopHiss();
            return false;
        }

        GetGame().GetCallqueue().CallLater(AdvanceQueue, 20, false);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected void EnsureHiss()
    {
        SimpleSoundComponent soundComp = GetSoundComponent();
        if (!soundComp)
            return;

        if (m_PlayedHiss != AudioHandle.Invalid)
        {
            if (!soundComp.IsFinishedPlaying(m_PlayedHiss))
                return;
        }

        m_PlayedHiss = soundComp.PlayStr("SOUND_GBRS_HISS");
        ApplyPlayingTransforms();
    }

    //------------------------------------------------------------------------------------------------
    protected void StopHiss()
    {
        AudioSystem.TerminateSound(m_PlayedHiss);
        m_PlayedHiss = AudioHandle.Invalid;
    }

    //------------------------------------------------------------------------------------------------
    protected void StopQueue()
    {
        m_bQueueActive = false;
        GetGame().GetCallqueue().Remove(AdvanceQueue);
        GetGame().GetCallqueue().Remove(FinishQueue);
        AudioSystem.TerminateSound(m_PlayedRadio);
        m_PlayedRadio = AudioHandle.Invalid;
        StopHiss();
        m_aClipQueue.Clear();
        m_iClipIndex = 0;
    }

    //------------------------------------------------------------------------------------------------
    protected void AdvanceQueue()
    {
        if (!m_bQueueActive)
            return;

        SimpleSoundComponent soundComp = GetSoundComponent();
        if (!soundComp)
        {
            m_bQueueActive = false;
            StopHiss();
            TryStartDeferred();
            return;
        }

        if (m_PlayedRadio != AudioHandle.Invalid)
        {
            if (!soundComp.IsFinishedPlaying(m_PlayedRadio))
            {
                ApplyPlayingTransforms();
                GetGame().GetCallqueue().CallLater(AdvanceQueue, 20, false);
                return;
            }
        }

        EnsureHiss();
        ApplyPlayingTransforms();

        if (!PlayNextClip())
        {
            GetGame().GetCallqueue().CallLater(FinishQueue, 80, false);
            return;
        }

        GetGame().GetCallqueue().CallLater(AdvanceQueue, 20, false);
    }

    //------------------------------------------------------------------------------------------------
    protected void FinishQueue()
    {
        if (!m_bQueueActive)
            return;

        m_bQueueActive = false;
        StopHiss();
        if (TryStartDeferred())
            return;
    }

    //------------------------------------------------------------------------------------------------
    protected bool PlayNextClip()
    {
        SimpleSoundComponent soundComp = GetSoundComponent();
        if (!soundComp)
            return false;

        while (m_iClipIndex < m_aClipQueue.Count())
        {
            string eventName = m_aClipQueue.Get(m_iClipIndex);
            m_iClipIndex = m_iClipIndex + 1;
            if (eventName.IsEmpty())
                continue;

            m_PlayedRadio = soundComp.PlayStr(eventName);
            if (m_PlayedRadio != AudioHandle.Invalid)
            {
                ApplyPlayingTransforms();
                return true;
            }
        }

        return false;
    }

    //------------------------------------------------------------------------------------------------
    protected void SnapToLocalRadio()
    {
        PlayerController localController = GetGame().GetPlayerController();
        if (!localController)
            return;

        int playerId = localController.GetPlayerId();
        IEntity radio = GBRS_IntelRadioNet.GetPlayerIntelRadioEntity(playerId);
        IEntity attachTo = radio;

        IEntity controlled = localController.GetControlledEntity();
        if (radio)
        {
            if (controlled)
            {
                float distSq = vector.DistanceSq(radio.GetOrigin(), controlled.GetOrigin());
                if (distSq > (25.0 * 25.0))
                    attachTo = controlled;
            }
        }

        if (!attachTo)
            attachTo = controlled;
        if (!attachTo)
            return;

        vector mat[4];
        attachTo.GetWorldTransform(mat);
        SetWorldTransform(mat);

        SimpleSoundComponent soundComp = GetSoundComponent();
        if (soundComp)
            soundComp.SetTransformation(mat);
    }

    //------------------------------------------------------------------------------------------------
    protected void ApplyPlayingTransforms()
    {
        SnapToLocalRadio();

        vector mat[4];
        GetWorldTransform(mat);

        if (m_PlayedRadio != AudioHandle.Invalid)
            AudioSystem.SetSoundTransformation(m_PlayedRadio, mat);
        if (m_PlayedHiss != AudioHandle.Invalid)
            AudioSystem.SetSoundTransformation(m_PlayedHiss, mat);
    }

    //------------------------------------------------------------------------------------------------
    protected static void BuildClipQueue(
        notnull array<string> clips,
        int voiceKind,
        int gridPacked,
        int paramA,
        int paramB,
        string factionKey)
    {
        clips.Clear();

        string suffix = "US";
        if (factionKey == "USSR")
            suffix = "USSR";

        if (voiceKind == GBRS_RadarStationConstants.INTEL_VOICE_WLR)
            BuildWlrQueue(clips, suffix, gridPacked, paramA, paramB);
        else
            BuildAirQueue(clips, suffix, gridPacked, paramA, paramB);
    }

    //------------------------------------------------------------------------------------------------
    protected static void BuildAirQueue(
        notnull array<string> clips,
        string suffix,
        int gridPacked,
        int headingDeg,
        int altitudeM)
    {
        clips.Insert("SOUND_GBRS_AIR_" + suffix);
        AppendGrid(clips, suffix, gridPacked);

        if (headingDeg >= 0)
        {
            clips.Insert("SOUND_GBRS_HDG_" + suffix);
            AppendThreeDigits(clips, suffix, headingDeg);
        }

        if (altitudeM > 0)
        {
            clips.Insert("SOUND_GBRS_ALT_" + suffix);
            AppendNumberDigits(clips, suffix, altitudeM);
            clips.Insert("SOUND_GBRS_M_" + suffix);
        }

        clips.Insert("SOUND_GBRS_OUT_" + suffix);
    }

    //------------------------------------------------------------------------------------------------
    protected static void BuildWlrQueue(
        notnull array<string> clips,
        string suffix,
        int impactPacked,
        int launchPacked,
        int etaSec)
    {
        clips.Insert("SOUND_GBRS_WLR_" + suffix);

        if (launchPacked > 0)
        {
            clips.Insert("SOUND_GBRS_LCH_" + suffix);
            AppendGrid(clips, suffix, launchPacked);
        }

        clips.Insert("SOUND_GBRS_IMP_" + suffix);
        AppendGrid(clips, suffix, impactPacked);

        if (etaSec > 0)
        {
            clips.Insert("SOUND_GBRS_ETA_" + suffix);
            AppendNumberDigits(clips, suffix, etaSec);
            clips.Insert("SOUND_GBRS_SEC_" + suffix);
        }
        else
        {
            if (etaSec == 0)
                clips.Insert("SOUND_GBRS_NOW_" + suffix);
        }

        clips.Insert("SOUND_GBRS_OUT_" + suffix);
    }

    //------------------------------------------------------------------------------------------------
    protected static void AppendGrid(notnull array<string> clips, string suffix, int gridPacked)
    {
        AppendThreeDigits(clips, suffix, UnpackEast(gridPacked));
        AppendThreeDigits(clips, suffix, UnpackNorth(gridPacked));
    }

    //------------------------------------------------------------------------------------------------
    protected static int UnpackEast(int gridPacked)
    {
        int east = gridPacked / 1000;
        if (east < 0)
            east = 0;
        return east;
    }

    //------------------------------------------------------------------------------------------------
    protected static int UnpackNorth(int gridPacked)
    {
        int east = UnpackEast(gridPacked);
        int north = gridPacked - (east * 1000);
        if (north < 0)
            north = 0;
        return north;
    }

    //------------------------------------------------------------------------------------------------
    protected static void AppendThreeDigits(notnull array<string> clips, string suffix, int value)
    {
        int clamped = value;
        if (clamped < 0)
            clamped = 0;
        if (clamped > 999)
            clamped = clamped % 1000;

        int hundreds = clamped / 100;
        int tens = (clamped / 10) % 10;
        int ones = clamped % 10;
        clips.Insert("SOUND_GBRS_D" + hundreds.ToString() + "_" + suffix);
        clips.Insert("SOUND_GBRS_D" + tens.ToString() + "_" + suffix);
        clips.Insert("SOUND_GBRS_D" + ones.ToString() + "_" + suffix);
    }

    //------------------------------------------------------------------------------------------------
    //! No leading zeros. Zero speaks a single D0.
    protected static void AppendNumberDigits(notnull array<string> clips, string suffix, int value)
    {
        int n = value;
        if (n < 0)
            n = 0;

        if (n == 0)
        {
            clips.Insert("SOUND_GBRS_D0_" + suffix);
            return;
        }

        array<int> digits = {};
        while (n > 0)
        {
            digits.Insert(n % 10);
            n = n / 10;
        }

        int i = digits.Count() - 1;
        while (i >= 0)
        {
            int digit = digits.Get(i);
            clips.Insert("SOUND_GBRS_D" + digit.ToString() + "_" + suffix);
            i = i - 1;
        }
    }

    //------------------------------------------------------------------------------------------------
    protected string ResolveLocalFactionKey()
    {
        SCR_Faction slotted = SCR_Faction.Cast(SCR_FactionManager.SGetLocalPlayerFaction());
        if (slotted)
            return slotted.GetFactionKey();

        PlayerController localController = GetGame().GetPlayerController();
        if (!localController)
            return "";

        IEntity controlled = localController.GetControlledEntity();
        if (!controlled)
            return "";

        Faction entityFaction = SCR_Faction.GetEntityFaction(controlled);
        if (!entityFaction)
            return "";

        return entityFaction.GetFactionKey();
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
        m_bQueueActive = false;
        if (GetGame())
        {
            ScriptCallQueue queue = GetGame().GetCallqueue();
            if (queue)
            {
                queue.Remove(AdvanceQueue);
                queue.Remove(FinishQueue);
            }
        }

        AudioSystem.TerminateSound(m_PlayedRadio);
        AudioSystem.TerminateSound(m_PlayedHiss);
        s_Instance = null;
    }
}
