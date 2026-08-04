//------------------------------------------------------------------------------------------------
//! Shovel / entrenching-tool dismantle for destroyed GBRS radar wrecks.
//! Intact stations are not removable this way — combat damage leaves a burning wreck
//! that must be cleared with SCR_CampaignBuildingGadgetToolComponent (etool).
class GBRS_RadarStationShovelDismantleUserAction : ScriptedUserAction
{
    [Attribute("Dismantle wreck", UIWidgets.EditBox, "Shown when wreck can be cleared", "")]
    protected LocalizedString m_sActionName;

    [Attribute("Hold entrenching tool", UIWidgets.EditBox, "Shown when etool is not held", "")]
    protected LocalizedString m_sNeedShovelReason;

    [Attribute("Radar must be destroyed first", UIWidgets.EditBox, "Shown when station is intact", "")]
    protected LocalizedString m_sNotDestroyedReason;

    protected GBRS_RadarStationComponent m_RadarStation;
    protected SCR_GadgetManagerComponent m_GadgetManager;
    protected IEntity m_User;

    protected static const int ALLOWED_PLAYER_DISTANCE_SQ = 10000;

    //------------------------------------------------------------------------------------------------
    override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
    {
        m_RadarStation = FindRadarStation(pOwnerEntity);
    }

    //------------------------------------------------------------------------------------------------
    protected GBRS_RadarStationComponent FindRadarStation(IEntity fromEntity)
    {
        if (!fromEntity)
            return null;

        GBRS_RadarStationComponent onSelf =
            GBRS_RadarStationComponent.Cast(fromEntity.FindComponent(GBRS_RadarStationComponent));
        if (onSelf)
            return onSelf;

        IEntity parent = fromEntity.GetParent();
        while (parent)
        {
            GBRS_RadarStationComponent onParent =
                GBRS_RadarStationComponent.Cast(parent.FindComponent(GBRS_RadarStationComponent));
            if (onParent)
                return onParent;

            parent = parent.GetParent();
        }

        return null;
    }

    //------------------------------------------------------------------------------------------------
    override void OnActionStart(IEntity pUserEntity)
    {
        m_User = pUserEntity;

        ChimeraCharacter character = ChimeraCharacter.Cast(pUserEntity);
        if (!character)
            return;

        if (!SCR_CharacterHelper.IsPlayerOrAIOwner(character))
            return;

        CharacterControllerComponent charController = character.GetCharacterController();
        if (!charController)
            return;

        IEntity tool = GetBuildingTool(pUserEntity);
        if (!tool)
            return;

        CharacterAnimationComponent anim = charController.GetAnimationComponent();
        if (!anim)
            return;

        int itemActionId = anim.BindCommand("CMD_Item_Action");
        ItemUseParameters params = new ItemUseParameters();
        params.SetEntity(tool);
        params.SetAllowMovementDuringAction(false);
        params.SetKeepInHandAfterSuccess(true);
        params.SetCommandID(itemActionId);
        params.SetCommandIntArg(2);
        charController.TryUseItemOverrideParams(params);

        super.OnActionStart(pUserEntity);
    }

    //------------------------------------------------------------------------------------------------
    override void OnActionCanceled(IEntity pOwnerEntity, IEntity pUserEntity)
    {
        m_User = null;
        CancelPlayerAnimation(ChimeraCharacter.Cast(pUserEntity));
        super.OnActionCanceled(pOwnerEntity, pUserEntity);
    }

    //------------------------------------------------------------------------------------------------
    override bool CanBeShownScript(IEntity user)
    {
        if (!m_RadarStation)
            m_RadarStation = FindRadarStation(GetOwner());

        if (!m_RadarStation)
            return false;

        if (!m_RadarStation.IsConfigured())
            return false;

        if (!m_RadarStation.IsDestroyed())
            return false;

        ChimeraCharacter character = ChimeraCharacter.Cast(user);
        if (!character)
            return false;

        if (!m_GadgetManager)
        {
            m_GadgetManager = SCR_GadgetManagerComponent.GetGadgetManager(character);
            return false;
        }

        if (!SCR_CampaignBuildingGadgetToolComponent.Cast(m_GadgetManager.GetHeldGadgetComponent()))
            return false;

        return true;
    }

    //------------------------------------------------------------------------------------------------
    override bool CanBePerformedScript(IEntity user)
    {
        if (!m_RadarStation)
            return false;

        if (!m_RadarStation.IsDestroyed())
        {
            SetCannotPerformReason(m_sNotDestroyedReason);
            return false;
        }

        if (m_User && m_User != user)
        {
            SetCannotPerformReason("#AR-UserAction_Blocked_InUseByOther");
            return false;
        }

        ChimeraCharacter character = ChimeraCharacter.Cast(user);
        if (!character)
            return false;

        if (!m_GadgetManager)
            m_GadgetManager = SCR_GadgetManagerComponent.GetGadgetManager(character);

        if (!m_GadgetManager)
        {
            SetCannotPerformReason(m_sNeedShovelReason);
            return false;
        }

        if (!SCR_CampaignBuildingGadgetToolComponent.Cast(m_GadgetManager.GetHeldGadgetComponent()))
        {
            SetCannotPerformReason(m_sNeedShovelReason);
            return false;
        }

        IEntity root = m_RadarStation.GetOwner();
        if (!root)
            return false;

        if (vector.DistanceSqXZ(root.GetOrigin(), character.GetOrigin()) > ALLOWED_PLAYER_DISTANCE_SQ)
            return false;

        return true;
    }

    //------------------------------------------------------------------------------------------------
    override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
    {
        m_User = null;

        ChimeraCharacter character = ChimeraCharacter.Cast(pUserEntity);
        CancelPlayerAnimation(character);

        if (!m_RadarStation)
            m_RadarStation = FindRadarStation(pOwnerEntity);

        if (!m_RadarStation)
            return;

        if (!m_RadarStation.IsDestroyed())
            return;

        IEntity root = m_RadarStation.GetOwner();
        if (!root)
            return;

        RplComponent rpl = RplComponent.Cast(root.FindComponent(RplComponent));
        if (rpl && rpl.IsProxy())
            return;

        if (character)
        {
            if (character.GetCharacterController().GetLifeState() != ECharacterLifeState.ALIVE)
                return;

            if (vector.DistanceSqXZ(root.GetOrigin(), character.GetOrigin()) > ALLOWED_PLAYER_DISTANCE_SQ)
                return;
        }

        m_RadarStation.StopDestroyedFireEffects();

        SCR_EditableEntityComponent editable =
            SCR_EditableEntityComponent.Cast(root.FindComponent(SCR_EditableEntityComponent));
        if (editable)
        {
            editable.Delete(true, true);
            return;
        }

        RplComponent.DeleteRplEntity(root, false);
    }

    //------------------------------------------------------------------------------------------------
    override bool GetActionNameScript(out string outName)
    {
        outName = m_sActionName;
        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected IEntity GetBuildingTool(notnull IEntity ent)
    {
        SCR_GadgetManagerComponent gadgetManager = SCR_GadgetManagerComponent.GetGadgetManager(ent);
        if (!gadgetManager)
            return null;

        return gadgetManager.GetHeldGadget();
    }

    //------------------------------------------------------------------------------------------------
    protected void CancelPlayerAnimation(ChimeraCharacter character)
    {
        if (!character)
            return;

        CharacterControllerComponent charController = character.GetCharacterController();
        if (!charController)
            return;

        CharacterAnimationComponent pAnimationComponent = charController.GetAnimationComponent();
        if (!pAnimationComponent)
            return;

        CharacterCommandHandlerComponent cmdHandler = pAnimationComponent.GetCommandHandler();
        if (cmdHandler)
            cmdHandler.FinishItemUse(true);
    }
}
