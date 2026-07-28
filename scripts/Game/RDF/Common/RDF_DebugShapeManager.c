//------------------------------------------------------------------------------------------------
//! Holds debug Shape refs so they stay alive until Clear() — aligned with SCR_DebugShapeManager.
class RDF_DebugShapeManager
{
    protected ref set<ref Shape> m_Shapes = new set<ref Shape>();

    protected static const int DEFAULT_SHAPE_COLOUR = ARGB(255, 255, 0, 0);
    protected static const ShapeFlags DEFAULT_SHAPE_FLAGS = ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP;

    //------------------------------------------------------------------------------------------------
    //! Create a straight line.
    //! \param[in] from origin
    //! \param[in] to destination
    //! \param[in] colour ARGB
    //! \param[in] additionalFlags extra ShapeFlags OR'd with defaults
    //! \return the created line
    Shape AddLine(vector from, vector to, int colour = DEFAULT_SHAPE_COLOUR, ShapeFlags additionalFlags = 0)
    {
        vector points[2];
        points[0] = from;
        points[1] = to;
        Shape shape = Shape.CreateLines(colour, DEFAULT_SHAPE_FLAGS | additionalFlags, points, 2);
        m_Shapes.Insert(shape);
        return shape;
    }

    //------------------------------------------------------------------------------------------------
    //! Create a polyline (2–50 points; extras clipped).
    //! \param[in] points vertex list
    //! \param[in] colour ARGB
    //! \param[in] additionalFlags extra ShapeFlags
    //! \return the polyline or null if fewer than 2 points
    Shape AddPolyLine(notnull array<vector> points, int colour = DEFAULT_SHAPE_COLOUR, ShapeFlags additionalFlags = 0)
    {
        int count = points.Count();
        if (count < 2)
            return null;

        if (count > 50)
            count = 50;

        vector pointsS[50];
        for (int i = 0; i < count; i++)
        {
            pointsS[i] = points.Get(i);
        }

        Shape shape = Shape.CreateLines(colour, DEFAULT_SHAPE_FLAGS | additionalFlags, pointsS, count);
        m_Shapes.Insert(shape);
        return shape;
    }

    //------------------------------------------------------------------------------------------------
    //! Horizontal circle on XZ (official CreateCircle helper).
    //! \param[in] centre world position (Y = plane height)
    //! \param[in] radius metres
    //! \param[in] colour ARGB
    //! \param[in] subdivisions segment count (clamped 2–50 by CreateCircle)
    //! \param[in] additionalFlags extra ShapeFlags
    //! \return the created circle
    Shape AddCircleXZ(
        vector centre,
        float radius,
        int colour = DEFAULT_SHAPE_COLOUR,
        int subdivisions = 24,
        ShapeFlags additionalFlags = 0)
    {
        Shape shape = CreateCircle(
            centre, vector.Up, radius, colour, subdivisions, DEFAULT_SHAPE_FLAGS | additionalFlags);
        m_Shapes.Insert(shape);
        return shape;
    }

    //------------------------------------------------------------------------------------------------
    //! Horizontal arc on XZ (portion of a circle).
    //! \param[in] centre world position
    //! \param[in] angleStartRad counter-clockwise radians from +X toward +Z
    //! \param[in] coveredAngleRad signed sweep radians
    //! \param[in] radius metres
    //! \param[in] colour ARGB
    //! \param[in] subdivisions segment count
    //! \param[in] additionalFlags extra ShapeFlags
    //! \return the created arc
    Shape AddCircleArcXZ(
        vector centre,
        float angleStartRad,
        float coveredAngleRad,
        float radius,
        int colour = DEFAULT_SHAPE_COLOUR,
        int subdivisions = 16,
        ShapeFlags additionalFlags = 0)
    {
        if (coveredAngleRad < 0.0)
        {
            angleStartRad = angleStartRad + coveredAngleRad;
            coveredAngleRad = -coveredAngleRad;
        }

        if (angleStartRad < 0.0 || angleStartRad > Math.PI2)
            angleStartRad = Math.Repeat(angleStartRad, Math.PI2);

        if (coveredAngleRad > Math.PI2)
            coveredAngleRad = Math.Repeat(coveredAngleRad, Math.PI2);

        vector forward;
        forward[0] = Math.Cos(angleStartRad);
        forward[1] = 0.0;
        forward[2] = Math.Sin(angleStartRad);

        Shape shape = CreateCircleArc(
            centre,
            vector.Up,
            forward,
            0.0,
            coveredAngleRad * Math.RAD2DEG,
            radius,
            colour,
            subdivisions,
            DEFAULT_SHAPE_FLAGS | additionalFlags);
        m_Shapes.Insert(shape);
        return shape;
    }

    //------------------------------------------------------------------------------------------------
    //! Create a sphere.
    //! \param[in] centre world position
    //! \param[in] radius metres
    //! \param[in] colour ARGB
    //! \param[in] additionalFlags extra ShapeFlags
    //! \return the created sphere
    Shape AddSphere(vector centre, float radius, int colour = DEFAULT_SHAPE_COLOUR, ShapeFlags additionalFlags = 0)
    {
        Shape shape = Shape.CreateSphere(colour, DEFAULT_SHAPE_FLAGS | additionalFlags, centre, radius);
        m_Shapes.Insert(shape);
        return shape;
    }

    //------------------------------------------------------------------------------------------------
    //! Adopt an externally created Shape (e.g. CreateTris fan).
    //! \param[in] shape owned shape
    void Add(notnull Shape shape)
    {
        m_Shapes.Insert(shape);
    }

    //------------------------------------------------------------------------------------------------
    //! Drop all managed shapes (GPU resources released when refs die).
    void Clear()
    {
        m_Shapes.Clear();
    }
}
