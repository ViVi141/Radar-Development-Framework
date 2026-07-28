// Enfusion .conf schema for radar surface EM parameters (workshop-friendly).
[BaseContainerProps(configRoot: true)]
class RDF_RadarSurfaceTableConf
{
    [Attribute("X", UIWidgets.EditBox, "Radar band tag (X/C/S/L/VHF)")]
    string m_sBand;

    [Attribute("3", UIWidgets.EditBox, "Sea state 0-6 (water σ⁰ offset reference)")]
    int m_iSeaState;

    [Attribute("30", UIWidgets.EditBox, "Reference grazing angle for σ⁰_ref [deg]")]
    float m_fThetaRefDeg;

    [Attribute(desc: "Per ERDF_DemSurfaceClass EM parameters")]
    ref array<ref RDF_RadarSurfaceEntryConf> m_aEntries;
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps(), SCR_BaseContainerCustomTitleField("m_sName")]
class RDF_RadarSurfaceEntryConf
{
    [Attribute("0", UIWidgets.EditBox, "ERDF_DemSurfaceClass id")]
    int m_iId;

    [Attribute("unknown", UIWidgets.EditBox, "Debug / calibration name")]
    string m_sName;

    [Attribute("-18", UIWidgets.EditBox, "σ⁰ at reference grazing [dB]")]
    float m_fSigma0RefDb;

    [Attribute("1", UIWidgets.EditBox, "Constant-gamma exponent k")]
    float m_fGammaK;

    [Attribute("1", UIWidgets.EditBox, "Clutter power scale")]
    float m_fClutterScale;

    [Attribute("10", UIWidgets.EditBox, "Relative permittivity εᵣ")]
    float m_fDielectric;

    [Attribute("0.08", UIWidgets.EditBox, "Roughness proxy [0..]")]
    float m_fRoughness;

    [Attribute("0", UIWidgets.EditBox, "One-way volume attenuation [dB/km]")]
    float m_fAttenuationDbPerKm;
}
