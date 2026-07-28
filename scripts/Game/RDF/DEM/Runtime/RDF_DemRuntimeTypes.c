// Runtime DEM data contracts for V3 CSV tiles and RDF_DEM_BIN_V1 packs.
class RDF_DemRuntimeManifest
{
    string m_WorldKey;
    float m_BoundsMinX;
    float m_BoundsMinZ;
    float m_BoundsMaxX;
    float m_BoundsMaxZ;
    float m_CellM;
    int m_TileCells;
    int m_TileCountX;
    int m_TileCountZ;
    int m_MaxSpans;

    string m_RootDir;
    string m_TilesDir;

    // Binary pack (workshop). When set, LoadTile seeks this file instead of CSV.
    bool m_IsBinaryPack;
    string m_BinPackPath;
    float m_BinYScale;
    ref map<string, int> m_BinTileByteOffset;
    ref map<string, int> m_BinTileByteLength;
}

class RDF_DemRuntimeCellSample
{
    bool m_Valid;
    float m_TerrainY;
    float m_SlopeDeg;
    float m_WaterDepthM;
    int m_WaterType;
    int m_OccGround;
    int m_EntityHits;
    float m_DensityGcm3;
    int m_SurfaceClass;
    int m_NSpans;
    ref array<float> m_SpanLo;
    ref array<float> m_SpanHi;

    void RDF_DemRuntimeCellSample()
    {
        m_SpanLo = new array<float>();
        m_SpanHi = new array<float>();
    }
}

class RDF_DemRuntimeTile
{
    int m_TileIx;
    int m_TileIz;
    float m_OriginX;
    float m_OriginZ;
    int m_OriginIx;
    int m_OriginIz;
    float m_CellM;
    int m_Size;
    int m_MaxSpans;

    ref array<int> m_HasCell;
    ref array<float> m_TerrainY;
    ref array<float> m_SlopeDeg;
    ref array<float> m_WaterDepthM;
    ref array<int> m_WaterType;
    ref array<int> m_OccGround;
    ref array<int> m_EntityHits;
    ref array<float> m_DensityGcm3;
    ref array<int> m_SurfaceClass;
    ref array<int> m_NSpans;
    ref array<float> m_SpanLo;
    ref array<float> m_SpanHi;

    void RDF_DemRuntimeTile()
    {
        m_HasCell = new array<int>();
        m_TerrainY = new array<float>();
        m_SlopeDeg = new array<float>();
        m_WaterDepthM = new array<float>();
        m_WaterType = new array<int>();
        m_OccGround = new array<int>();
        m_EntityHits = new array<int>();
        m_DensityGcm3 = new array<float>();
        m_SurfaceClass = new array<int>();
        m_NSpans = new array<int>();
        m_SpanLo = new array<float>();
        m_SpanHi = new array<float>();
    }

    protected int CellIndex(int localIx, int localIz)
    {
        return localIz * m_Size + localIx;
    }

    protected int SpanIndex(int localIx, int localIz, int spanIdx)
    {
        int baseIndex = CellIndex(localIx, localIz) * m_MaxSpans;
        return baseIndex + spanIdx;
    }

    void InitializeStorage(int size, int maxSpans)
    {
        m_Size = size;
        m_MaxSpans = maxSpans;

        int cellCount = size * size;
        int spanCount = cellCount * maxSpans;

        m_HasCell.Resize(cellCount);
        m_TerrainY.Resize(cellCount);
        m_SlopeDeg.Resize(cellCount);
        m_WaterDepthM.Resize(cellCount);
        m_WaterType.Resize(cellCount);
        m_OccGround.Resize(cellCount);
        m_EntityHits.Resize(cellCount);
        m_DensityGcm3.Resize(cellCount);
        m_SurfaceClass.Resize(cellCount);
        m_NSpans.Resize(cellCount);
        m_SpanLo.Resize(spanCount);
        m_SpanHi.Resize(spanCount);

        for (int i = 0; i < cellCount; i++)
        {
            m_HasCell.Set(i, 0);
            m_TerrainY.Set(i, 0.0);
            m_SlopeDeg.Set(i, 0.0);
            m_WaterDepthM.Set(i, 0.0);
            m_WaterType.Set(i, 0);
            m_OccGround.Set(i, 0);
            m_EntityHits.Set(i, 0);
            m_DensityGcm3.Set(i, 0.0);
            m_SurfaceClass.Set(i, ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN);
            m_NSpans.Set(i, 0);
        }

        for (int j = 0; j < spanCount; j++)
        {
            m_SpanLo.Set(j, 0.0);
            m_SpanHi.Set(j, 0.0);
        }
    }

    bool SetCell(
        int localIx,
        int localIz,
        float terrainY,
        float slopeDeg,
        float waterDepthM,
        int waterType,
        int occGround,
        int entityHits,
        float densityGcm3,
        int surfaceClass,
        int nSpans,
        notnull array<float> spanLo,
        notnull array<float> spanHi)
    {
        if (localIx < 0 || localIz < 0 || localIx >= m_Size || localIz >= m_Size)
            return false;
        if (nSpans < 0 || nSpans > m_MaxSpans)
            return false;

        int idx = CellIndex(localIx, localIz);
        m_HasCell.Set(idx, 1);
        m_TerrainY.Set(idx, terrainY);
        m_SlopeDeg.Set(idx, slopeDeg);
        m_WaterDepthM.Set(idx, waterDepthM);
        m_WaterType.Set(idx, waterType);
        m_OccGround.Set(idx, occGround);
        m_EntityHits.Set(idx, entityHits);
        m_DensityGcm3.Set(idx, densityGcm3);
        m_SurfaceClass.Set(idx, surfaceClass);
        m_NSpans.Set(idx, nSpans);

        for (int s = 0; s < m_MaxSpans; s++)
        {
            int spanIdx = SpanIndex(localIx, localIz, s);
            float lo = 0.0;
            float hi = 0.0;
            if (s < spanLo.Count())
                lo = spanLo.Get(s);
            if (s < spanHi.Count())
                hi = spanHi.Get(s);
            m_SpanLo.Set(spanIdx, lo);
            m_SpanHi.Set(spanIdx, hi);
        }

        return true;
    }

    bool TryGetCell(int localIx, int localIz, out RDF_DemRuntimeCellSample outSample)
    {
        outSample = new RDF_DemRuntimeCellSample();
        if (localIx < 0 || localIz < 0 || localIx >= m_Size || localIz >= m_Size)
            return false;

        int idx = CellIndex(localIx, localIz);
        if (m_HasCell.Get(idx) == 0)
            return false;

        outSample.m_Valid = true;
        outSample.m_TerrainY = m_TerrainY.Get(idx);
        outSample.m_SlopeDeg = m_SlopeDeg.Get(idx);
        outSample.m_WaterDepthM = m_WaterDepthM.Get(idx);
        outSample.m_WaterType = m_WaterType.Get(idx);
        outSample.m_OccGround = m_OccGround.Get(idx);
        outSample.m_EntityHits = m_EntityHits.Get(idx);
        outSample.m_DensityGcm3 = m_DensityGcm3.Get(idx);
        outSample.m_SurfaceClass = m_SurfaceClass.Get(idx);
        outSample.m_NSpans = m_NSpans.Get(idx);

        outSample.m_SpanLo.Clear();
        outSample.m_SpanHi.Clear();
        for (int s = 0; s < m_MaxSpans; s++)
        {
            int spanIdx = SpanIndex(localIx, localIz, s);
            outSample.m_SpanLo.Insert(m_SpanLo.Get(spanIdx));
            outSample.m_SpanHi.Insert(m_SpanHi.Get(spanIdx));
        }

        return true;
    }
}
