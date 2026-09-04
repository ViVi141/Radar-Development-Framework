// Enfusion .conf schema for fixed-site polar path LUT (workshop).
[BaseContainerProps(configRoot: true)]
class RDF_RadarSitePathLutConf
{
    [Attribute("RDF_SITE_PATH_LUT_V1", UIWidgets.EditBox, "Schema id")]
    string m_sSchema;

    [Attribute("synthetic", UIWidgets.EditBox, "World / bake tag")]
    string m_sWorld;

    [Attribute("0", UIWidgets.EditBox, "Bake origin X")]
    float m_fOriginX;

    [Attribute("25", UIWidgets.EditBox, "Bake origin Y")]
    float m_fOriginY;

    [Attribute("0", UIWidgets.EditBox, "Bake origin Z")]
    float m_fOriginZ;

    [Attribute("4000", UIWidgets.EditBox, "Max range [m]")]
    float m_fMaxRangeM;

    [Attribute("72", UIWidgets.EditBox, "Azimuth bin count")]
    int m_iAzCount;

    [Attribute("40", UIWidgets.EditBox, "Range bin count")]
    int m_iRangeCount;

    [Attribute("0.032", UIWidgets.EditBox, "Wavelength used for bake [m]")]
    float m_fWavelengthM;

    [Attribute(desc: "Row-major az×range path factors (linear)")]
    ref array<float> m_aFactors;
}
