// Scan-beam debug draw matching RDF mechanical-scan gating.
// RDF uses a 3D cone test: dot(scanForward, toTarget) >= cos(halfAngle),
// where halfAngle = AzimuthBeamwidthDeg/2 and scanForward is horizontal.
// The old az×el box was misleading for aircraft (looked inside, failed cone).
class GBRS_RadarScanFrustumVisual
{
    protected static const float DEG_TO_RAD = 0.017453292519943295;
    protected static const int CONE_SEGMENTS = 16;

    // Draw the RDF instantaneous acceptance cone + boresight.
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
        DrawCone(origin, forward, rangeM, coneHalfDeg, colourEdge, colourCore);

        // Optional elevation-beam hint (not the hard gate): thin wedge on boresight.
        if (elevationHalfDeg > 0.1)
            DrawElevationHint(
                origin,
                forward,
                rangeM,
                elevationBoresightDeg,
                elevationHalfDeg,
                colourCore);
    }

    static void DrawCone(
        vector origin,
        vector forward,
        float rangeM,
        float coneHalfDeg,
        int colourEdge,
        int colourCore)
    {
        if (rangeM < 1.0)
            return;

        float flen = forward.Length();
        if (flen < 0.001)
            forward = Vector(1.0, 0.0, 0.0);
        else
            forward = forward * (1.0 / flen);

        if (coneHalfDeg < 0.5)
            coneHalfDeg = 0.5;
        if (coneHalfDeg > 89.0)
            coneHalfDeg = 89.0;

        vector axisX;
        vector axisY;
        BuildConeBasis(forward, axisX, axisY);

        float halfRad = coneHalfDeg * DEG_TO_RAD;
        float sinH = Math.Sin(halfRad);
        float cosH = Math.Cos(halfRad);
        float ringR = rangeM * sinH;
        vector ringCenter = origin + forward * (rangeM * cosH);

        ShapeFlags flags = ShapeFlags.ONCE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP;

        vector ring[CONE_SEGMENTS + 1];
        int i = 0;
        while (i < CONE_SEGMENTS)
        {
            float ang = (6.2831853 * i) / CONE_SEGMENTS;
            vector rim = ringCenter
                + axisX * (Math.Cos(ang) * ringR)
                + axisY * (Math.Sin(ang) * ringR);
            ring[i] = rim;

            vector edge[2];
            edge[0] = origin;
            edge[1] = rim;
            Shape.CreateLines(colourEdge, flags, edge, 2);

            i = i + 1;
        }
        ring[CONE_SEGMENTS] = ring[0];
        Shape.CreateLines(colourEdge, flags, ring, CONE_SEGMENTS + 1);

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

        vector dLo = DirFromAzEl(az, 0.0, elLo);
        vector dHi = DirFromAzEl(az, 0.0, elHi);
        vector dCore = DirFromAzEl(az, 0.0, elevationBoresightDeg);

        ShapeFlags flags = ShapeFlags.ONCE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP;

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

    protected static void BuildConeBasis(vector forward, out vector axisX, out vector axisY)
    {
        vector up = Vector(0.0, 1.0, 0.0);
        axisX = up * forward;
        float xLen = axisX.Length();
        if (xLen < 0.001)
        {
            up = Vector(1.0, 0.0, 0.0);
            axisX = up * forward;
            xLen = axisX.Length();
        }
        if (xLen < 0.001)
        {
            axisX = Vector(1.0, 0.0, 0.0);
            axisY = Vector(0.0, 1.0, 0.0);
            return;
        }

        axisX = axisX * (1.0 / xLen);
        axisY = forward * axisX;
        float yLen = axisY.Length();
        if (yLen < 0.001)
            axisY = Vector(0.0, 1.0, 0.0);
        else
            axisY = axisY * (1.0 / yLen);
    }

    protected static vector DirFromAzEl(float azRad, float azOffDeg, float elDeg)
    {
        float a = azRad + azOffDeg * DEG_TO_RAD;
        float e = elDeg * DEG_TO_RAD;
        float ce = Math.Cos(e);
        return Vector(Math.Cos(a) * ce, Math.Sin(e), Math.Sin(a) * ce);
    }
}
