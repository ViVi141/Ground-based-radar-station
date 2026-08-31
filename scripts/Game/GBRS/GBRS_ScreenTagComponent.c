//------------------------------------------------------------------------------------------------
//! Marks a child mesh as a PPI / CONTACT / OPTICS CRT for GBRS_ConsoleComponent.
[ComponentEditorProps(category: "GameScripted/GBRS", description: "CRT slot tag for GBRS console screens")]
class GBRS_ScreenTagComponentClass : ScriptComponentClass
{
}

class GBRS_ScreenTagComponent : ScriptComponent
{
    [Attribute("0", UIWidgets.ComboBox, "Which CRT this mesh drives", "", ParamEnumArray.FromEnum(EGBRS_ScreenKind))]
    protected EGBRS_ScreenKind m_eKind;

    EGBRS_ScreenKind GetKind()
    {
        return m_eKind;
    }
}
