// Enfusion .conf schema for HW clutter / MTD bake-back (workshop).
[BaseContainerProps(configRoot: true)]
class RDF_RadarHwCalibConf
{
    [Attribute("RDF_HW_CALIB_V1", UIWidgets.EditBox, "Schema id")]
    string m_sSchema;

    [Attribute("shorad", UIWidgets.EditBox, "Calib preset name")]
    string m_sPreset;

    [Attribute("4000", UIWidgets.EditBox, "PRF [Hz]")]
    float m_fPrfHz;

    [Attribute("0.0001", UIWidgets.EditBox, "MTI clutter floor (linear)")]
    float m_fMtiClutterFloor;

    [Attribute("0.000000001", UIWidgets.EditBox, "MTD non-zero-bin leakage")]
    float m_fMtdClutterLeakage;

    [Attribute("16", UIWidgets.EditBox, "Doppler bin count")]
    int m_iDopplerBinCount;

    [Attribute("mtd_bank", UIWidgets.EditBox, "mti_mode: twopulse / three / mtd_bank")]
    string m_sMtiMode;

    [Attribute("0.5", UIWidgets.EditBox, "Clutter σ_vr [m/s]")]
    float m_fClutterSigmaVrMs;

    [Attribute("1.2", UIWidgets.EditBox, "PRF stagger ratio")]
    float m_fPrfStaggerRatio;
}
