// Simple, intuitive radar visualisations designed to be immediately readable.
//
// RDF_RadarWorldMarkerDisplay
//   Draws tall coloured poles (30 m) at every detected target in the 3-D world.
//   Poles are visible from a distance and encode target type via colour:
//     Bright green  = large entity (RCS > +10 dBsm)
//     Cyan          = small entity
//     Yellow        = terrain hit with strong return
//     Orange        = weak / near-threshold return
//   A compass rose is drawn at the player's feet to show N/S/E/W orientation.
//
// RDF_RadarTextDisplay
//   Prints a small ASCII top-down radar map to the debug console after each scan.
//   North is up.  One character cell = m_CellSizeM metres.
//   Legend: R=you  @=large entity  *=entity  +=terrain
//
// Usage (from stats handler):
//   RDF_RadarTextDisplay disp = new RDF_RadarTextDisplay();
//   disp.PrintRadarMap(samples, subjectEntity);

// ============================================================
//  World-marker display - tall poles visible in the 3-D world
// ============================================================
class RDF_RadarWorldMarkerDisplay
{
    // Height of each detection pole (m).
    float m_PoleHeight = 30.0;
    // Length of the N/S/E/W compass arms drawn at the player's feet (m).
    float m_CompassLength = 50.0;

    // Pole colours by target class.
    int m_ColorEntityLarge  = ARGB(255,   0, 255,  60);  // bright green
    int m_ColorEntitySmall  = ARGB(255,   0, 200, 255);  // cyan
    int m_ColorTerrainHigh  = ARGB(220, 255, 220,   0);  // yellow
    int m_ColorWeak         = ARGB(160, 255, 110,   0);  // orange
    // North compass arm colour.
    int m_ColorCompassNorth = ARGB(255, 255, 255, 255);  // white
    // Other compass arms.
    int m_ColorCompassOther = ARGB(120, 180, 180, 180);  // grey

    protected ref array<ref Shape> m_Shapes;

    void RDF_RadarWorldMarkerDisplay()
    {
        m_Shapes = new array<ref Shape>();
    }

    // Call once per scan frame to refresh all markers.
    void DrawWorldMarkers(array<ref RDF_LidarSample> samples, IEntity radarEntity)
    {
        if (!radarEntity)
            return;

        m_Shapes.Clear();

        vector origin = radarEntity.GetOrigin();

        DrawCompassRose(origin);

        foreach (RDF_LidarSample baseSample : samples)
        {
            RDF_RadarSample sample = RDF_RadarSample.Cast(baseSample);
            if (!sample || !sample.m_Hit)
                continue;

            DrawPole(sample);
        }
    }

    // Draw a vertical pole with a horizontal cross-mark at the top.
    protected void DrawPole(RDF_RadarSample sample)
    {
        int flags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.ONCE;
        int color = GetPoleColor(sample);

        vector base = sample.m_HitPos;
        vector top  = base;
        top[1]      = top[1] + m_PoleHeight;

        // Vertical shaft.
        vector shaft[2];
        shaft[0] = base;
        shaft[1] = top;
        Shape s = Shape.CreateLines(color, flags, shaft, 2);
        if (s)
            m_Shapes.Insert(s);

        // Horizontal cross at the top (X axis arm).
        float crossR = 2.0;
        vector cx0 = top + Vector(-crossR, 0, 0);
        vector cx1 = top + Vector( crossR, 0, 0);
        vector armX[2];
        armX[0] = cx0;
        armX[1] = cx1;
        Shape sx = Shape.CreateLines(color, flags, armX, 2);
        if (sx)
            m_Shapes.Insert(sx);

        // Horizontal cross at the top (Z axis arm).
        vector cz0 = top + Vector(0, 0, -crossR);
        vector cz1 = top + Vector(0, 0,  crossR);
        vector armZ[2];
        armZ[0] = cz0;
        armZ[1] = cz1;
        Shape sz = Shape.CreateLines(color, flags, armZ, 2);
        if (sz)
            m_Shapes.Insert(sz);
    }

    // Colour encodes target category.
    protected int GetPoleColor(RDF_RadarSample sample)
    {
        bool isEntity = (sample.m_Entity != null);
        float rcs     = sample.GetRCSdBsm();
        float snr     = sample.m_SignalToNoiseRatio;

        if (isEntity && rcs > 10.0)
            return m_ColorEntityLarge;

        if (isEntity)
            return m_ColorEntitySmall;

        if (snr > 40.0)
            return m_ColorTerrainHigh;

        return m_ColorWeak;
    }

    // Draw N/S/E/W arms from the radar origin at 1.5 m height.
    // The North arm is white; others are grey.
    // In Enfusion: +X = East, +Z = North.
    protected void DrawCompassRose(vector origin)
    {
        int flags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.ONCE;
        float h = origin[1] + 1.5;

        vector centre = Vector(origin[0], h, origin[2]);

        // North (+Z) - white, longer.
        vector northEnd = Vector(origin[0], h, origin[2] + m_CompassLength);
        vector nArm[2];
        nArm[0] = centre;
        nArm[1] = northEnd;
        Shape sn = Shape.CreateLines(m_ColorCompassNorth, flags, nArm, 2);
        if (sn)
            m_Shapes.Insert(sn);

        // South (-Z).
        vector southEnd = Vector(origin[0], h, origin[2] - m_CompassLength * 0.6);
        vector sArm[2];
        sArm[0] = centre;
        sArm[1] = southEnd;
        Shape ss = Shape.CreateLines(m_ColorCompassOther, flags, sArm, 2);
        if (ss)
            m_Shapes.Insert(ss);

        // East (+X).
        vector eastEnd = Vector(origin[0] + m_CompassLength * 0.6, h, origin[2]);
        vector eArm[2];
        eArm[0] = centre;
        eArm[1] = eastEnd;
        Shape se = Shape.CreateLines(m_ColorCompassOther, flags, eArm, 2);
        if (se)
            m_Shapes.Insert(se);

        // West (-X).
        vector westEnd = Vector(origin[0] - m_CompassLength * 0.6, h, origin[2]);
        vector wArm[2];
        wArm[0] = centre;
        wArm[1] = westEnd;
        Shape sw = Shape.CreateLines(m_ColorCompassOther, flags, wArm, 2);
        if (sw)
            m_Shapes.Insert(sw);
    }
}

// ============================================================
//  Text display - ASCII top-down radar map printed to console
// ============================================================
class RDF_RadarTextDisplay
{
    // Number of cells on each side of the player (grid is size x size).
    // Must be odd so centre cell exists.
    int m_GridHalf = 5;   // => 11 x 11 grid
    // Metres represented by each grid cell.
    float m_CellSizeM = 200.0;  // 200 m/cell => 1000 m radius

    // Print a top-down ASCII map of the current scan.
    void PrintRadarMap(array<ref RDF_LidarSample> samples, IEntity radarEntity)
    {
        if (!radarEntity)
            return;

        vector origin = radarEntity.GetOrigin();

        int size = m_GridHalf * 2 + 1;  // 11

        // Build flat row-major grid (size*size cells), initialised to space.
        array<string> grid = new array<string>();
        int total = size * size;
        int i = 0;
        while (i < total)
        {
            grid.Insert(" ");
            i = i + 1;
        }

        // Mark player at centre.
        SetCell(grid, size, m_GridHalf, m_GridHalf, "R");

        // Plot detected targets.
        foreach (RDF_LidarSample baseSample : samples)
        {
            RDF_RadarSample sample = RDF_RadarSample.Cast(baseSample);
            if (!sample || !sample.m_Hit)
                continue;

            vector delta = sample.m_HitPos - origin;
            float dx = delta[0];   // East  (+X)
            float dz = delta[2];   // North (+Z)

            // Convert real-world offset to grid column / row.
            // col increases going East; row decreases going North (row 0 = North edge).
            int col = m_GridHalf + (int)(dx / m_CellSizeM);
            int row = m_GridHalf - (int)(dz / m_CellSizeM);

            if (col < 0 || col >= size || row < 0 || row >= size)
                continue;

            string cur = GetCell(grid, size, col, row);
            if (cur == "R")
                continue;

            bool isEntity = (sample.m_Entity != null);
            float rcs = sample.GetRCSdBsm();

            string marker;
            if (isEntity && rcs > 10.0)
                marker = "@";   // large entity
            else if (isEntity)
                marker = "*";   // small entity
            else
                marker = "+";   // terrain / diffuse return

            SetCell(grid, size, col, row, marker);
        }

        // Render map to log.
        int cellI = (int)m_CellSizeM;
        string border = "+";
        int b = 0;
        while (b < size)
        {
            border = border + "-";
            b = b + 1;
        }
        border = border + "+";

        Print("[Radar]       N");
        Print("[Radar] " + border);

        int r = 0;
        while (r < size)
        {
            string line = "[Radar] ";
            if (r == m_GridHalf)
                line = line + "W|";
            else
                line = line + " |";

            int c = 0;
            while (c < size)
            {
                line = line + GetCell(grid, size, c, r);
                c = c + 1;
            }

            if (r == m_GridHalf)
                line = line + "|E";
            else
                line = line + "|";

            Print(line);
            r = r + 1;
        }

        Print("[Radar] " + border);
        Print("[Radar]       S");
        Print("[Radar] cell=" + cellI + "m  @=entity(big)  *=entity  +=terrain  R=you");
    }

    protected void SetCell(array<string> grid, int size, int col, int row, string val)
    {
        if (col < 0 || col >= size || row < 0 || row >= size)
            return;
        int idx = row * size + col;
        if (idx >= 0 && idx < grid.Count())
            grid[idx] = val;
    }

    protected string GetCell(array<string> grid, int size, int col, int row)
    {
        if (col < 0 || col >= size || row < 0 || row >= size)
            return " ";
        int idx = row * size + col;
        if (idx >= 0 && idx < grid.Count())
            return grid[idx];
        return " ";
    }
}
