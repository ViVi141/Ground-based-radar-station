//------------------------------------------------------------------------------------------------
//! Player-map grid labels (100 m squares, easting then northing, 3-digit pad).
//! Matches SCR_MapCursorModule: world metres * 0.01, wrap 1000, ToString(3, 0).
class GBRS_MapGrid
{
    static const float PRECISION = 0.01;
    static const float WRAP = 1000.0;

    //------------------------------------------------------------------------------------------------
    static string Format(vector pos)
    {
        float east = pos[0] * PRECISION;
        float north = pos[2] * PRECISION;
        if (east < 0.0)
            east = WRAP + east;
        if (north < 0.0)
            north = WRAP + north;

        return Math.Floor(east).ToString(3, 0) + " " + Math.Floor(north).ToString(3, 0);
    }
}
