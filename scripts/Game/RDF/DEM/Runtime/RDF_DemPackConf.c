// Workshop-packaged DemData uses CONFResourceClass (.conf). Game/dedicated-server
// runtimes do not register a Resource loader for .json (Workbench-only), which
// caused: "Resource loader for extension not registered!" for DemData JSON.
[BaseContainerProps(configRoot: true)]
class RDF_DemSurfManifestConf
{
    [Attribute("RDF_SURF_JSON_V1", UIWidgets.EditBox, "Pack magic")]
    string m_sMagic;

    [Attribute("", UIWidgets.EditBox, "World key e.g. GM_Eden")]
    string m_sWorld;

    [Attribute("0", UIWidgets.EditBox, "Bounds min X")]
    float m_fBoundsMinX;

    [Attribute("0", UIWidgets.EditBox, "Bounds min Z")]
    float m_fBoundsMinZ;

    [Attribute("0", UIWidgets.EditBox, "Bounds max X")]
    float m_fBoundsMaxX;

    [Attribute("0", UIWidgets.EditBox, "Bounds max Z")]
    float m_fBoundsMaxZ;

    [Attribute("2", UIWidgets.EditBox, "Cell size [m]")]
    float m_fCellM;

    [Attribute("32", UIWidgets.EditBox, "Tile edge cells")]
    int m_iTileCells;

    [Attribute("1", UIWidgets.EditBox, "Tile count X")]
    int m_iTileCountX;

    [Attribute("1", UIWidgets.EditBox, "Tile count Z")]
    int m_iTileCountZ;

    [Attribute("surf_chunks/", UIWidgets.EditBox, "Chunks subdir")]
    string m_sChunksDir;
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps(configRoot: true)]
class RDF_DemHeightManifestConf
{
    [Attribute("RDF_HEIGHT_JSON_V1", UIWidgets.EditBox, "Pack magic")]
    string m_sMagic;

    [Attribute("", UIWidgets.EditBox, "World key e.g. GM_Eden")]
    string m_sWorld;

    [Attribute("0", UIWidgets.EditBox, "Bounds min X")]
    float m_fBoundsMinX;

    [Attribute("0", UIWidgets.EditBox, "Bounds min Z")]
    float m_fBoundsMinZ;

    [Attribute("0", UIWidgets.EditBox, "Bounds max X")]
    float m_fBoundsMaxX;

    [Attribute("0", UIWidgets.EditBox, "Bounds max Z")]
    float m_fBoundsMaxZ;

    [Attribute("2", UIWidgets.EditBox, "Cell size [m]")]
    float m_fCellM;

    [Attribute("32", UIWidgets.EditBox, "Tile edge cells")]
    int m_iTileCells;

    [Attribute("1", UIWidgets.EditBox, "Tile count X")]
    int m_iTileCountX;

    [Attribute("1", UIWidgets.EditBox, "Tile count Z")]
    int m_iTileCountZ;

    [Attribute("0.1", UIWidgets.EditBox, "Y quanta scale")]
    float m_fYScale;

    [Attribute("height_chunks/", UIWidgets.EditBox, "Chunks subdir")]
    string m_sChunksDir;
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps(configRoot: true)]
class RDF_DemChunkRowConf
{
    [Attribute("0", UIWidgets.EditBox, "Tile row iz")]
    int m_iIz;

    [Attribute(desc: "Tiles on this row")]
    ref array<ref RDF_DemChunkTileConf> m_aTiles;
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps()]
class RDF_DemChunkTileConf
{
    [Attribute("0", UIWidgets.EditBox, "Tile ix")]
    int m_iIx;

    [Attribute("", UIWidgets.EditBox, "Hex payload")]
    string m_sHex;
}
