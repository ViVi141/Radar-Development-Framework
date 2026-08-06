#ifdef WORKBENCH
[WorkbenchPluginAttribute(
    name: "RDF Bake DEM Heightfield",
    description: "Request in-game DEM tile bake (terrain/slope/water/obstacles) to $profile:RDF/DemData/",
    wbModules: {"WorldEditor"},
    awesomeFontCode: 0xF3C5)]
class RDF_DemBakePlugin : WorkbenchPlugin
{
    [Attribute("1", UIWidgets.CheckBox, "Clear stale BakeDemFull.flag before writing a new request")]
    protected bool m_bClearStaleFlag;

    //------------------------------------------------------------------------------------------------
    override void Run()
    {
        if (m_bClearStaleFlag)
        {
            if (FileIO.FileExists(RDF_DemBakeConstants.BAKE_FLAG_FILE))
                FileIO.DeleteFile(RDF_DemBakeConstants.BAKE_FLAG_FILE);
        }

        RDF_DemTileBake.WriteBakeRequest();

        Print("[RDF DEM Bake] Request written. Play the open world via Workbench Play (not standalone Arma Reforger).", LogLevel.NORMAL);
        Print("[RDF DEM Bake] Output: Documents/My Games/ArmaReforgerWorkbench/profile/RDF/DemData/<worldKey>/tiles/", LogLevel.NORMAL);
        Print(string.Format(
            "[RDF DEM Bake] Defaults: cell=%1m tile_cells=%2. Keep Play running; watch Script log for [RDF DEM Bake] progress=.",
            RDF_DemBakeConstants.CELL_M.ToString(),
            RDF_DemBakeConstants.TILE_CELLS.ToString()), LogLevel.NORMAL);
        Print("[RDF DEM Bake] V3: density + surface_class + column spans.", LogLevel.NORMAL);
        Print("[RDF DEM Bake] Deep water (>=5m): sea surface only (no seabed modeling).", LogLevel.NORMAL);
        if (RDF_DemBakeConstants.ENABLE_COLUMN_ENTITY_QUERY)
            Print("[RDF DEM Bake] Column entity query ON (bridges/canopy).", LogLevel.NORMAL);
        else
            Print("[RDF DEM Bake] Column entity query OFF (safe mode; ground slab only).", LogLevel.WARNING);
        Print("[RDF DEM Bake] Delete tiles/ only for full re-bake; resume skips existing files.", LogLevel.NORMAL);
        Print("[RDF DEM Bake] After bake: pack SURF/HEIGHT — rdf_dem_pack_surface_json.py / rdf_dem_pack_height_json.py", LogLevel.NORMAL);
        Print("[RDF DEM Bake] Offline npz: python tools/dem/rdf_dem_pack.py --world <worldKey>", LogLevel.NORMAL);
    }
}
#endif
