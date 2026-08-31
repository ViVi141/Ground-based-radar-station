//------------------------------------------------------------------------------------------------
//! Root of the operator console (military table + three CRT children).
[ComponentEditorProps(category: "GameScripted/GBRS", description: "GBRS operator console root (finds CRT screen meshes)")]
class GBRS_ConsoleComponentClass : ScriptComponentClass
{
}

class GBRS_ConsoleComponent : ScriptComponent
{
    //------------------------------------------------------------------------------------------------
    static GBRS_ConsoleComponent FindOnStation(IEntity stationRoot)
    {
        if (!stationRoot)
            return null;

        GBRS_ConsoleComponent onSelf =
            GBRS_ConsoleComponent.Cast(stationRoot.FindComponent(GBRS_ConsoleComponent));
        if (onSelf)
            return onSelf;

        return FindInChildren(stationRoot);
    }

    //------------------------------------------------------------------------------------------------
    protected static GBRS_ConsoleComponent FindInChildren(IEntity root)
    {
        if (!root)
            return null;

        IEntity child = root.GetChildren();
        while (child)
        {
            GBRS_ConsoleComponent onChild =
                GBRS_ConsoleComponent.Cast(child.FindComponent(GBRS_ConsoleComponent));
            if (onChild)
                return onChild;

            GBRS_ConsoleComponent nested = FindInChildren(child);
            if (nested)
                return nested;

            child = child.GetSibling();
        }

        return null;
    }

    //------------------------------------------------------------------------------------------------
    IEntity FindScreenMesh(EGBRS_ScreenKind kind)
    {
        IEntity owner = GetOwner();
        if (!owner)
            return null;

        return FindScreenRecursive(owner, kind);
    }

    //------------------------------------------------------------------------------------------------
    protected IEntity FindScreenRecursive(IEntity root, EGBRS_ScreenKind kind)
    {
        if (!root)
            return null;

        GBRS_ScreenTagComponent tag =
            GBRS_ScreenTagComponent.Cast(root.FindComponent(GBRS_ScreenTagComponent));
        if (tag)
        {
            if (tag.GetKind() == kind)
                return root;
        }

        IEntity child = root.GetChildren();
        while (child)
        {
            IEntity found = FindScreenRecursive(child, kind);
            if (found)
                return found;

            child = child.GetSibling();
        }

        return null;
    }

    //------------------------------------------------------------------------------------------------
    GBRS_RadarStationComponent FindRadarStation()
    {
        IEntity fromEntity = GetOwner();
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
}
