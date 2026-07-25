// Resolve world key from GetGame().GetWorldFile() path (e.g. GM_Eden.ent -> GM_Eden).
class RDF_DemWorldKey
{
    static string FromPath(string worldFile)
    {
        if (worldFile.IsEmpty())
            return string.Empty;

        string s = worldFile;
        s.Replace("\\", "/");

        int slash = s.LastIndexOf("/");
        if (slash >= 0)
            s = s.Substring(slash + 1, s.Length() - slash - 1);

        int dot = s.LastIndexOf(".");
        if (dot > 0)
            s = s.Substring(0, dot);

        return s;
    }
}
