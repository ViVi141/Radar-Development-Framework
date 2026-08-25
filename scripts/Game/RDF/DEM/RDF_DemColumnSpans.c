// Per-column height-interval sampler for RDF DEM V3.
// One XZ cell → sorted, merged [y_lo, y_hi] spans from entity world bounds
// (plus an optional thin ground slab at terrainY). This is a vertical profile,
// not a dense voxel grid — same idea as airborne LiDAR multi-return columns.
class RDF_DemColumnSpans
{
    protected static ref array<float> s_Lo;
    protected static ref array<float> s_Hi;
    protected static float s_CellMinX;
    protected static float s_CellMaxX;
    protected static float s_CellMinZ;
    protected static float s_CellMaxZ;
    protected static float s_QueryMinY;
    protected static float s_QueryMaxY;
    protected static int s_EntityHits;
    protected static const int MAX_RAW_SPANS = 64;

    //------------------------------------------------------------------------------------------------
    static void EnsureBuffers()
    {
        if (!s_Lo)
            s_Lo = new array<float>();
        if (!s_Hi)
            s_Hi = new array<float>();
    }

    //------------------------------------------------------------------------------------------------
    // Fills outLo/outHi with up to MAX_SPANS merged intervals (absolute world Y).
    // Returns span count. outEntityHits = accepted entities. outOccGround = near-ground flag.
    static int SampleColumn(
        notnull World world,
        float cx,
        float cz,
        float terrainY,
        float cellM,
        bool queryEntities,
        out array<float> outLo,
        out array<float> outHi,
        out int outEntityHits,
        out int outOccGround)
    {
        EnsureBuffers();
        s_Lo.Clear();
        s_Hi.Clear();
        s_EntityHits = 0;
        outEntityHits = 0;
        outOccGround = 0;

        if (!outLo)
            outLo = new array<float>();
        if (!outHi)
            outHi = new array<float>();
        outLo.Clear();
        outHi.Clear();

        float half = cellM * 0.5;
        s_CellMinX = cx - half;
        s_CellMaxX = cx + half;
        s_CellMinZ = cz - half;
        s_CellMaxZ = cz + half;
        s_QueryMinY = terrainY + RDF_DemBakeConstants.COLUMN_Y_MIN_OFFSET;
        s_QueryMaxY = terrainY + RDF_DemBakeConstants.COLUMN_Y_MAX_OFFSET;

        if (queryEntities)
        {
            vector aabbMin = Vector(s_CellMinX, s_QueryMinY, s_CellMinZ);
            vector aabbMax = Vector(s_CellMaxX, s_QueryMaxY, s_CellMaxZ);
            world.QueryEntitiesByAABB(
                aabbMin, aabbMax, QueryColumnCallback, null, EQueryEntitiesFlags.STATIC);
        }

        // Always include a thin ground slab so empty cells still have a surface.
        AddSpan(terrainY - 0.05, terrainY + 0.15);

        SortAndMerge();

        int maxSpans = RDF_DemBakeConstants.MAX_SPANS;
        int count = s_Lo.Count();
        if (count > maxSpans)
            count = maxSpans;

        float nearTop = terrainY + RDF_DemBakeConstants.OCC_CLEARANCE_M;
        for (int i = 0; i < count; i++)
        {
            float lo = s_Lo.Get(i);
            float hi = s_Hi.Get(i);
            outLo.Insert(lo);
            outHi.Insert(hi);

            // Near-ground occupancy (excluding the thin ground slab itself).
            if (hi > terrainY + 0.3 && lo < nearTop)
                outOccGround = 1;
        }

        outEntityHits = s_EntityHits;
        return count;
    }

    //------------------------------------------------------------------------------------------------
    protected static bool QueryColumnCallback(IEntity entity)
    {
        if (!entity)
            return true;
        if (s_Lo.Count() >= MAX_RAW_SPANS)
            return false;

        vector mins;
        vector maxs;
        entity.GetWorldBounds(mins, maxs);

        float ex = maxs[0] - mins[0];
        float ey = maxs[1] - mins[1];
        float ez = maxs[2] - mins[2];
        // Reject non-finite bounds: NaN fails every comparison below and would
        // poison the span sort/merge.
        if (!(ex >= 0) || !(ey >= 0) || !(ez >= 0))
            return true;
        if (ex > RDF_DemBakeConstants.MAX_ENTITY_XZ_EXTENT_M)
            return true;
        if (ez > RDF_DemBakeConstants.MAX_ENTITY_XZ_EXTENT_M)
            return true;
        if (ey > RDF_DemBakeConstants.MAX_ENTITY_Y_EXTENT_M)
            return true;
        if (ey < RDF_DemBakeConstants.SPAN_MIN_THICKNESS_M)
            return true;

        // Must overlap this cell in XZ.
        if (maxs[0] < s_CellMinX || mins[0] > s_CellMaxX)
            return true;
        if (maxs[2] < s_CellMinZ || mins[2] > s_CellMaxZ)
            return true;

        float lo = mins[1];
        float hi = maxs[1];
        if (hi < s_QueryMinY || lo > s_QueryMaxY)
            return true;

        if (lo < s_QueryMinY)
            lo = s_QueryMinY;
        if (hi > s_QueryMaxY)
            hi = s_QueryMaxY;
        if (hi - lo < RDF_DemBakeConstants.SPAN_MIN_THICKNESS_M)
            return true;

        AddSpan(lo, hi);
        s_EntityHits = s_EntityHits + 1;
        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected static void AddSpan(float lo, float hi)
    {
        if (hi <= lo)
            return;
        s_Lo.Insert(lo);
        s_Hi.Insert(hi);
    }

    //------------------------------------------------------------------------------------------------
    protected static void SortAndMerge()
    {
        int n = s_Lo.Count();
        if (n <= 1)
            return;

        // Insertion sort by lo.
        for (int i = 1; i < n; i++)
        {
            float keyLo = s_Lo.Get(i);
            float keyHi = s_Hi.Get(i);
            int j = i - 1;
            while (j >= 0)
            {
                if (s_Lo.Get(j) <= keyLo)
                    break;
                s_Lo.Set(j + 1, s_Lo.Get(j));
                s_Hi.Set(j + 1, s_Hi.Get(j));
                j = j - 1;
            }
            s_Lo.Set(j + 1, keyLo);
            s_Hi.Set(j + 1, keyHi);
        }

        // Compact merged spans in-place. The previous implementation allocated
        // two temporary arrays for every cell, causing severe GC pressure after
        // hundreds of thousands of cells.
        float curLo = s_Lo.Get(0);
        float curHi = s_Hi.Get(0);
        float gap = RDF_DemBakeConstants.SPAN_MERGE_GAP_M;
        int writeIndex = 0;

        for (int k = 1; k < n; k++)
        {
            float lo = s_Lo.Get(k);
            float hi = s_Hi.Get(k);
            if (lo <= curHi + gap)
            {
                if (hi > curHi)
                    curHi = hi;
            }
            else
            {
                s_Lo.Set(writeIndex, curLo);
                s_Hi.Set(writeIndex, curHi);
                writeIndex = writeIndex + 1;
                curLo = lo;
                curHi = hi;
            }
        }
        s_Lo.Set(writeIndex, curLo);
        s_Hi.Set(writeIndex, curHi);
        writeIndex = writeIndex + 1;
        s_Lo.Resize(writeIndex);
        s_Hi.Resize(writeIndex);
    }

    //------------------------------------------------------------------------------------------------
    // Append fixed-width span fields to a CSV row builder (n_spans + MAX_SPANS pairs).
    static string FormatSpanFields(array<float> spanLo, array<float> spanHi, int spanCount)
    {
        int maxSpans = RDF_DemBakeConstants.MAX_SPANS;
        if (spanCount < 0)
            spanCount = 0;
        if (spanCount > maxSpans)
            spanCount = maxSpans;

        // Collect fragments into an array and join via divide-and-conquer so
        // the total copy work is O(n log n) instead of the O(n²) cost of
        // repeated `row = row + " " + x` (16 concatenations/cell × 1024
        // cells/tile = ~16k quadratic-row builds per tile).
        array<string> parts = new array<string>();
        parts.Reserve(1 + maxSpans * 2);
        parts.Insert(spanCount.ToString());
        for (int i = 0; i < maxSpans; i++)
        {
            if (i < spanCount)
            {
                parts.Insert(spanLo.Get(i).ToString());
                parts.Insert(spanHi.Get(i).ToString());
            }
            else
            {
                parts.Insert("0");
                parts.Insert("0");
            }
        }
        return JoinSpanParts(parts, " ");
    }

    // Divide-and-conquer join: O(n log n) total copy work vs O(n²) for
    // repeated `row += part`. Each merge level copies the full payload once.
    protected static string JoinSpanParts(array<string> parts, string sep)
    {
        int n = parts.Count();
        if (n == 0)
            return string.Empty;
        if (n == 1)
            return parts.Get(0);
        return JoinSpanRange(parts, sep, 0, n);
    }

    protected static string JoinSpanRange(array<string> parts, string sep, int lo, int hi)
    {
        if (lo + 1 == hi)
            return parts.Get(lo);
        int mid = (lo + hi) / 2;
        return JoinSpanRange(parts, sep, lo, mid) + sep + JoinSpanRange(parts, sep, mid, hi);
    }
}
