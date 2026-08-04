// Scan-beam debug draw for RDF mechanical scanning.
// Azimuth gate is horizontal; elevation is a separate vertical wedge.
// Do not draw a full 3D cone around a level axis — that puts the lower
// half underground.
class GBRS_RadarScanFrustumVisual
{
    protected static const float DEG_TO_RAD = 0.017453292519943295;
    protected static const int FAN_SEGMENTS = 16;

    // Draw the RDF instantaneous azimuth fan + elevation boresight wedge.
    static void Draw(
        vector origin,
        vector forward,
        float rangeM,
        float coneHalfDeg,
        float elevationBoresightDeg,
        float elevationHalfDeg,
        int colourEdge,
        int colourCore)
    {
        DrawAzimuthFan(origin, forward, rangeM, coneHalfDeg, colourEdge, colourCore);

        if (elevationHalfDeg > 0.1)
            DrawElevationHint(
                origin,
                forward,
                rangeM,
                elevationBoresightDeg,
                elevationHalfDeg,
                colourCore);
    }

    // Horizontal sector in the XZ plane through the antenna origin.
    static void DrawAzimuthFan(
        vector origin,
        vector forward,
        float rangeM,
        float coneHalfDeg,
        int colourEdge,
        int colourCore)
    {
        if (rangeM < 1.0)
            return;

        // Flatten to horizontal — matches RDF_RadarScanner.GetScanForward.
        forward[1] = 0.0;
        float flen = forward.Length();
        if (flen < 0.001)
            forward = Vector(1.0, 0.0, 0.0);
        else
            forward = forward * (1.0 / flen);

        if (coneHalfDeg < 0.5)
            coneHalfDeg = 0.5;
        if (coneHalfDeg > 89.0)
            coneHalfDeg = 89.0;

        float az = Math.Atan2(forward[2], forward[0]);
        float halfRad = coneHalfDeg * DEG_TO_RAD;
        float azLo = az - halfRad;
        float azHi = az + halfRad;

        // Respect terrain depth. NOZBUFFER makes rays remain visible after
        // entering hills or dropping below the terrain surface.
        ShapeFlags flags = ShapeFlags.ONCE | ShapeFlags.TRANSP;

        vector leftDir = DirFromAzEl(azLo, 0.0, 0.0);
        vector rightDir = DirFromAzEl(azHi, 0.0, 0.0);
        vector leftEnd = origin + leftDir * rangeM;
        vector rightEnd = origin + rightDir * rangeM;

        vector leftEdge[2];
        leftEdge[0] = origin;
        leftEdge[1] = leftEnd;
        Shape.CreateLines(colourEdge, flags, leftEdge, 2);

        vector rightEdge[2];
        rightEdge[0] = origin;
        rightEdge[1] = rightEnd;
        Shape.CreateLines(colourEdge, flags, rightEdge, 2);

        vector arc[FAN_SEGMENTS + 1];
        int i = 0;
        while (i <= FAN_SEGMENTS)
        {
            float t = i;
            t = t / FAN_SEGMENTS;
            float a = azLo + (azHi - azLo) * t;
            arc[i] = origin + DirFromAzEl(a, 0.0, 0.0) * rangeM;
            i = i + 1;
        }
        Shape.CreateLines(colourEdge, flags, arc, FAN_SEGMENTS + 1);

        vector core[2];
        core[0] = origin;
        core[1] = origin + forward * rangeM;
        Shape.CreateLines(colourCore, flags, core, 2);
    }

    protected static void DrawElevationHint(
        vector origin,
        vector forward,
        float rangeM,
        float elevationBoresightDeg,
        float elevationHalfDeg,
        int colour)
    {
        float flen = forward.Length();
        if (flen < 0.001)
            return;
        forward = forward * (1.0 / flen);

        float az = Math.Atan2(forward[2], forward[0]);
        float elLo = elevationBoresightDeg - elevationHalfDeg;
        float elHi = elevationBoresightDeg + elevationHalfDeg;

        // Keep the visual wedge above the local horizon so lines do not
        // punch through the terrain near the mast.
        if (elLo < 0.0)
            elLo = 0.0;
        if (elHi < elLo + 0.5)
            elHi = elLo + 0.5;
        if (elevationBoresightDeg < elLo)
            elevationBoresightDeg = elLo;
        if (elevationBoresightDeg > elHi)
            elevationBoresightDeg = elHi;

        vector dLo = DirFromAzEl(az, 0.0, elLo);
        vector dHi = DirFromAzEl(az, 0.0, elHi);
        vector dCore = DirFromAzEl(az, 0.0, elevationBoresightDeg);

        ShapeFlags flags = ShapeFlags.ONCE | ShapeFlags.TRANSP;

        vector lo[2];
        lo[0] = origin;
        lo[1] = origin + dLo * rangeM;
        Shape.CreateLines(colour, flags, lo, 2);

        vector hi[2];
        hi[0] = origin;
        hi[1] = origin + dHi * rangeM;
        Shape.CreateLines(colour, flags, hi, 2);

        vector core[2];
        core[0] = origin;
        core[1] = origin + dCore * rangeM;
        Shape.CreateLines(colour, flags, core, 2);
    }

    protected static vector DirFromAzEl(float azRad, float azOffDeg, float elDeg)
    {
        float a = azRad + azOffDeg * DEG_TO_RAD;
        float e = elDeg * DEG_TO_RAD;
        float ce = Math.Cos(e);
        return Vector(Math.Cos(a) * ce, Math.Sin(e), Math.Sin(a) * ce);
    }
}
