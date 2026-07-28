// Enfusion .conf schema for baked radar signature table (workshop-friendly).
[BaseContainerProps(configRoot: true)]
class RDF_RadarSignatureTableConf
{
    [Attribute("1", UIWidgets.EditBox, "Schema version")]
    int m_iVersion;

    [Attribute(desc: "Per-prefab radar signature rows")]
    ref array<ref RDF_RadarSignatureEntryConf> m_aEntries;
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps(), SCR_BaseContainerCustomTitleField("m_sKey")]
class RDF_RadarSignatureEntryConf
{
    [Attribute("", UIWidgets.EditBox, "Prefab resource name key")]
    string m_sKey;

    [Attribute("1", UIWidgets.EditBox, "Extent X [m]")]
    float m_fSizeX;

    [Attribute("1", UIWidgets.EditBox, "Extent Y [m]")]
    float m_fSizeY;

    [Attribute("1", UIWidgets.EditBox, "Extent Z [m]")]
    float m_fSizeZ;

    [Attribute("1", UIWidgets.EditBox, "Characteristic length [m]")]
    float m_fCharLengthM;

    [Attribute("1", UIWidgets.EditBox, "Mean RCS [m^2]")]
    float m_fMeanRcsM2;

    [Attribute("1", UIWidgets.EditBox, "Swerling model")]
    int m_iSwerling;

    [Attribute("0", UIWidgets.EditBox, "Target type hint")]
    int m_iTypeHint;
}
