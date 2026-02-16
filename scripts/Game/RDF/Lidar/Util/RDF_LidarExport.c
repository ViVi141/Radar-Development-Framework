// Export LiDAR scan results to CSV-style text. Use Print to console or copy for external tools.
class RDF_LidarExport
{
    // CSV header line.
    static string GetCSVHeader()
    {
        return "index,hit,startX,startY,startZ,endX,endY,endZ,dirX,dirY,dirZ,hitPosX,hitPosY,hitPosZ,distance,colliderName";
    }

    // Format one sample as CSV row.
    static string SampleToCSVRow(RDF_LidarSample sample)
    {
        if (!sample) return "";
        string hitStr = "0";
        if (sample.m_Hit)
            hitStr = "1";
        vector s = sample.m_Start;
        vector e = sample.m_End;
        vector d = sample.m_Dir;
        vector h = sample.m_HitPos;
        string coll = sample.m_ColliderName;
        if (!coll)
            coll = "";
        string row = sample.m_Index.ToString() + "," + hitStr + ",";
        row = row + s[0].ToString() + "," + s[1].ToString() + "," + s[2].ToString() + ",";
        row = row + e[0].ToString() + "," + e[1].ToString() + "," + e[2].ToString() + ",";
        row = row + d[0].ToString() + "," + d[1].ToString() + "," + d[2].ToString() + ",";
        row = row + h[0].ToString() + "," + h[1].ToString() + "," + h[2].ToString() + ",";
        row = row + sample.m_Distance.ToString() + ",\"" + coll + "\"";
        return row;
    }

    // Print full CSV (header + all rows) to console. Copy from log for external files.
    static void PrintCSVToConsole(array<ref RDF_LidarSample> samples)
    {
        if (!samples) return;
        Print(RDF_LidarExport.GetCSVHeader());
        for (int i = 0; i < samples.Count(); i++)
        {
            RDF_LidarSample s = samples.Get(i);
            if (s)
                Print(RDF_LidarExport.SampleToCSVRow(s));
        }
    }

    // Export last scan from a visualizer to console (CSV).
    static void ExportLastScanToConsole(RDF_LidarVisualizer visualizer)
    {
        if (!visualizer) return;
        ref array<ref RDF_LidarSample> samples = visualizer.GetLastSamples();
        RDF_LidarExport.PrintCSVToConsole(samples);
    }

    // Export samples to CSV file. Overwrites if file exists. Returns true on success.
    static bool ExportToFile(array<ref RDF_LidarSample> samples, string path)
    {
        if (!samples || !path || path == "")
            return false;
        FileHandle f = FileIO.OpenFile(path, FileMode.WRITE);
        if (!f)
            return false;
        f.WriteLine(RDF_LidarExport.GetCSVHeader());
        for (int i = 0; i < samples.Count(); i++)
        {
            RDF_LidarSample s = samples.Get(i);
            if (s)
                f.WriteLine(RDF_LidarExport.SampleToCSVRow(s));
        }
        f.Close();
        return true;
    }

    // Append samples to CSV file. If file does not exist, creates with header. Returns true on success.
    static bool AppendToFile(array<ref RDF_LidarSample> samples, string path, bool writeHeaderIfNew = true)
    {
        if (!samples || !path || path == "")
            return false;
        bool exists = FileIO.FileExist(path);
        FileMode mode;
        if (exists)
            mode = FileMode.APPEND;
        else
            mode = FileMode.WRITE;
        FileHandle f = FileIO.OpenFile(path, mode);
        if (!f)
            return false;
        if (!exists && writeHeaderIfNew)
            f.WriteLine(RDF_LidarExport.GetCSVHeader());
        for (int i = 0; i < samples.Count(); i++)
        {
            RDF_LidarSample s = samples.Get(i);
            if (s)
                f.WriteLine(RDF_LidarExport.SampleToCSVRow(s));
        }
        f.Close();
        return true;
    }

    // Extended CSV header for AI training / offline analysis. Includes time, subject pose, scan metadata.
    static string GetExtendedCSVHeader()
    {
        return "index,hit,time,originX,originY,originZ,dirX,dirY,dirZ,elevation,azimuth,maxRange,distance,hitPosX,hitPosY,hitPosZ,targetName,subjectVelX,subjectVelY,subjectVelZ,subjectYaw,subjectPitch,scanId,frameIndex,entityClass";
    }

    // Format one sample as extended CSV row. Pass subject metadata from caller.
    static string SampleToExtendedCSVRow(RDF_LidarSample sample, float currentTime, float maxRange, vector subjectVel, float subjectYaw, float subjectPitch, int scanId, int frameIndex)
    {
        if (!sample) return "";
        vector s = sample.m_Start;
        vector d = sample.m_Dir;
        vector h = sample.m_HitPos;
        string hitStr = "0";
        if (sample.m_Hit)
            hitStr = "1";
        float elevation = Math.Atan2(d[2], Math.Sqrt(d[0] * d[0] + d[1] * d[1]));
        float azimuth = Math.Atan2(d[1], d[0]);
        string targetName = "";
        if (sample.m_Entity)
        {
            string n = sample.m_Entity.GetName();
            if (n)
                targetName = n;
        }
        if (targetName == "" && sample.m_ColliderName)
            targetName = sample.m_ColliderName;
        string entityClass = "";
        if (sample.m_Entity && sample.m_ColliderName)
            entityClass = sample.m_ColliderName;
        string row = sample.m_Index.ToString() + "," + hitStr + "," + currentTime.ToString() + ",";
        row = row + s[0].ToString() + "," + s[1].ToString() + "," + s[2].ToString() + ",";
        row = row + d[0].ToString() + "," + d[1].ToString() + "," + d[2].ToString() + ",";
        row = row + elevation.ToString() + "," + azimuth.ToString() + ",";
        row = row + maxRange.ToString() + "," + sample.m_Distance.ToString() + ",";
        row = row + h[0].ToString() + "," + h[1].ToString() + "," + h[2].ToString() + ",";
        row = row + "\"" + targetName + "\",";
        row = row + subjectVel[0].ToString() + "," + subjectVel[1].ToString() + "," + subjectVel[2].ToString() + ",";
        row = row + subjectYaw.ToString() + "," + subjectPitch.ToString() + ",";
        row = row + scanId.ToString() + "," + frameIndex.ToString() + ",";
        row = row + "\"" + entityClass + "\"";
        return row;
    }

    // Serialize samples to compact CSV string for network broadcast (see RFC in code comments).
    // Serialize samples to compact CSV string for network broadcast (see RFC in code comments).
    // Optional: quantize floats to reduce size and optionally apply simple RLE compression.
    static string SamplesToCSV(array<ref RDF_LidarSample> samples, bool compress = false, int decimalPlaces = 3)
    {
        if (!samples || samples.Count() == 0)
            return string.Empty;

        float mul = Math.Pow(10.0, decimalPlaces);
        string csv = "";
        for (int i = 0; i < samples.Count(); i++)
        {
            RDF_LidarSample s = samples.Get(i);
            // Format: idx|hit|startX,startY,startZ|hitX,hitY,hitZ|dirX,dirY,dirZ|dist
            string hitStr = "0";
            if (s.m_Hit)
                hitStr = "1";
            string part = s.m_Index.ToString() + "|" + hitStr + "|";
            part = part + (Math.Round(s.m_Start[0] * mul) / mul).ToString() + "," + (Math.Round(s.m_Start[1] * mul) / mul).ToString() + "," + (Math.Round(s.m_Start[2] * mul) / mul).ToString() + "|";
            part = part + (Math.Round(s.m_HitPos[0] * mul) / mul).ToString() + "," + (Math.Round(s.m_HitPos[1] * mul) / mul).ToString() + "," + (Math.Round(s.m_HitPos[2] * mul) / mul).ToString() + "|";
            part = part + (Math.Round(s.m_Dir[0] * mul) / mul).ToString() + "," + (Math.Round(s.m_Dir[1] * mul) / mul).ToString() + "," + (Math.Round(s.m_Dir[2] * mul) / mul).ToString() + "|";
            part = part + (Math.Round(s.m_Distance * mul) / mul).ToString();
            if (i < samples.Count() - 1)
                csv += part + ";";
            else
                csv += part;
        }

        if (compress)
            return "RLE:" + RLECompress(csv);

        return csv;
    }

    // Parse CSV string generated by SamplesToCSV back into samples.
    static array<ref RDF_LidarSample> ParseCSVToSamples(string csv)
    {
        array<ref RDF_LidarSample> outSamples = new array<ref RDF_LidarSample>();
        if (!csv || csv == string.Empty)
            return outSamples;

        // Detect simple compression wrapper (RLE)
        if (csv.StartsWith("RLE:"))
        {
            string raw = csv.Substring(4, csv.Length() - 4);
            raw = RLEDecompress(raw);
            if (!raw || raw == string.Empty)
                return outSamples;
            csv = raw;
        }

        array<string> parts = new array<string>();
        csv.Split(";", parts, false);
        foreach (string part : parts)
        {
            array<string> f = new array<string>();
            part.Split("|", f, false);
            if (f.Count() < 6)
                continue;

            int idx = f.Get(0).ToInt();
            bool hit = f.Get(1) == "1";

            array<string> vals = new array<string>();
            // start
            f.Get(2).Split(",", vals, false);
            if (vals.Count() < 3) continue; // malformed start
            vector start = Vector(vals.Get(0).ToFloat(), vals.Get(1).ToFloat(), vals.Get(2).ToFloat());
            vals.Clear();
            // hitPos
            f.Get(3).Split(",", vals, false);
            if (vals.Count() < 3) continue; // malformed hitPos
            vector hitPos = Vector(vals.Get(0).ToFloat(), vals.Get(1).ToFloat(), vals.Get(2).ToFloat());
            vals.Clear();
            // dir
            f.Get(4).Split(",", vals, false);
            if (vals.Count() < 3) continue; // malformed dir
            vector dir = Vector(vals.Get(0).ToFloat(), vals.Get(1).ToFloat(), vals.Get(2).ToFloat());
            vals.Clear();
            float dist = f.Get(5).ToFloat();

            // Treat zero-distance hits as invalid
            if (dist <= 0.0)
                hit = false;

            RDF_LidarSample s = new RDF_LidarSample();
            s.m_Index = idx;
            s.m_Hit = hit;
            s.m_Start = start;
            s.m_HitPos = hitPos;
            s.m_Dir = dir;
            s.m_Distance = dist;

            outSamples.Insert(s);
        }

        return outSamples;
    }

    // ------------------ Simple RLE compression helpers ------------------
    static string RLECompress(string s)
    {
        if (!s) return "";
        string outStr = "";
        int run = 1;
        for (int i = 1; i <= s.Length(); i++)
        {
            bool end = (i == s.Length());
            string cur = s.Substring(i - 1, 1);
            string next = "";
            if (!end)
                next = s.Substring(i, 1);
            if (!end && next == cur)
            {
                run++;
                continue;
            }
            if (run >= 4)
            {
                // encode as: c<cnt> where cnt is decimal number, e.g. A#12
                outStr += cur + "#" + run.ToString();
            }
            else
            {
                for (int r = 0; r < run; r++)
                    outStr += cur;
            }
            run = 1;
        }
        return outStr;
    }

    static string RLEDecompress(string s)
    {
        if (!s) return "";
        string outStr = "";
        int i = 0;
        while (i < s.Length())
        {
            string c = s.Substring(i, 1);
            // If '#' is immediately after current char, parse the count
            if (i + 1 >= s.Length())
            {
                // no run-length encoding, append remainder
                outStr += s.Substring(i, s.Length() - i);
                break;
            }

            string nextChar = s.Substring(i + 1, 1);
            if (nextChar == "#")
            {
                int numStart = i + 2; // after c and '#'
                int numEnd = numStart;
                while (numEnd < s.Length())
                {
                    string digit = s.Substring(numEnd, 1);
                    if (digit < "0" || digit > "9")
                        break;
                    numEnd++;
                }
                if (numEnd == numStart)
                {
                    // no digits found after '#', treat as literal and advance by 1
                    outStr += c;
                    i++;
                }
                else
                {
                    string numStr = s.Substring(numStart, numEnd - numStart);
                    int cnt = numStr.ToInt();
                    for (int r = 0; r < cnt; r++)
                        outStr += c;
                    i = numEnd;
                }
            }
            else
            {
                // no encoding for this char, append it and continue
                outStr += c;
                i++;
            }
        }
        return outStr;
    }
}
