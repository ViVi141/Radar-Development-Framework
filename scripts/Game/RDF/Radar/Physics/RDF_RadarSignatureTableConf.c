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

    // Rotor / micro-Doppler (0 tip speed = no sidebands).
    [Attribute("0", UIWidgets.EditBox, "Main-rotor tip speed [m/s]")]
    float m_fRotorTipSpeedMs;

    [Attribute("0", UIWidgets.EditBox, "Blade count")]
    int m_iBladeCount;

    [Attribute("0", UIWidgets.EditBox, "Rotor RCS fraction of mean [0..1]")]
    float m_fRotorRcsFraction;

    [Attribute("0", UIWidgets.EditBox, "Hub / flash radial width [m/s]")]
    float m_fHubWidthMs;

    [Attribute("0", UIWidgets.EditBox, "Jet-fan tip speed [m/s] (0 = none)")]
    float m_fFanTipSpeedMs;

    [Attribute("0", UIWidgets.EditBox, "Fan blade count")]
    int m_iFanBladeCount;

    [Attribute("0", UIWidgets.EditBox, "Fan RCS fraction of mean [0..1]")]
    float m_fFanRcsFraction;
}
