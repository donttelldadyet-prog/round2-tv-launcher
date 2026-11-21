# Round2 PKG server (owner 20 Nov 2026: "can the program read from a selected folder or drive instead, i usually backup pkgs to
# an external hd"). Runs on the owner's laptop. It:
#   - keeps a list of folders/drives (pkg-folders.txt next to this file) and reads every .pkg in them (and their sub-folders):
#     title, title id, content id, game / patch / DLC, version, firmware needed, size;
#   - shares those files on the shop Wi-Fi so the PS4s' own download manager can install them straight into place
#     (file links carry a secret code made at every start, so guests on the Wi-Fi can't browse or download them);
#   - answers the control centre (on this laptop only): the list, add/remove a folder, pick a folder;
#   - keeps the laptop awake while a PS4 is downloading; 30 min after the last download Windows' own sleep timer takes over.
# Start: double-click "Start Round2 PKG server.bat". Windows asks once to allow it on the network: tick Private (and Public if
# the shop Wi-Fi is marked Public), then Allow.
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$port = 8890
$code = @'
using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Net.NetworkInformation;
using System.Text;
using System.Threading;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public class R2Pkg {
  public string Id, Path, Name, ContentId, TitleId, Title, Category, AppVer, Version, SysVer, Err;
  public long Size, Mtime;
}

// 1.3 (owner 21 Nov: "server doesn't show useful info and i think size counter is wrong"): the PS4 fetches one file over several
// connections at once and asks for some parts again, so counting socket bytes over-counted. Now each (PS4, file) is one transfer
// that remembers which byte ranges were sent; progress, speed, time left and "sent" come from those ranges (each byte once).
public class R2Xfer {
  public string Who, Room, Name, Key; public long Size, Done, LastLogDone; public DateTime Start, LastByte, LastLog; public bool Finished;
  public int Conns; List<long[]> rs = new List<long[]>();
  public long Add(long a, long b) {   // [a, b) sent; returns how many of those bytes are new
    long before = Done; int i = 0;
    while (i < rs.Count && rs[i][1] < a) i++;
    long na = a, nb = b; int j = i;
    while (j < rs.Count && rs[j][0] <= nb) { na = Math.Min(na, rs[j][0]); nb = Math.Max(nb, rs[j][1]); j++; }
    rs.RemoveRange(i, j - i); rs.Insert(i, new long[] { na, nb });
    long d = 0; foreach (long[] r in rs) d += r[1] - r[0]; Done = d; return Done - before;
  }
}
public static class R2Srv {
  public static string ConfigPath, Token, StaffPath, Version = "1.4";
  public static int Port;
  public static List<string> Folders = new List<string>();
  public static List<R2Pkg> Pkgs = new List<R2Pkg>();
  public static DateTime ScannedAt;
  static readonly object L = new object();
  static int active;            // downloads running now
  static DateTime lastDl = DateTime.MinValue;
  static long servedBytes;      // 1.3: unique bytes sent (each part of a file counted once per PS4)
  static Dictionary<string, R2Xfer> xf = new Dictionary<string, R2Xfer>();
  static Dictionary<string, string> rooms = new Dictionary<string, string>();   // PS4 address -> room (pkg-rooms.txt)
  public static string RoomsPath;
  public static void LoadRooms() {
    rooms.Clear(); if (RoomsPath == null || !File.Exists(RoomsPath)) return;
    foreach (string l in File.ReadAllLines(RoomsPath)) { if (l.TrimStart().StartsWith("#")) continue; string[] a = l.Trim().Split(new char[] { ' ', '\t', '=' }, StringSplitOptions.RemoveEmptyEntries); if (a.Length >= 2) rooms[a[0]] = a[1]; }
  }
  static string RoomOf(string ip) { string r; return rooms.TryGetValue(ip, out r) ? r : ip; }
  static string Mb(long b) { return b >= (1L << 30) ? (b / 1073741824.0).ToString("0.00") + " GB" : (b / 1048576.0).ToString("0") + " MB"; }
  static string Dur(double s) { return s < 90 ? ((int)s) + " s" : s < 5400 ? (s / 60).ToString("0.0") + " min" : (s / 3600).ToString("0.0") + " h"; }
  static string Short(string n) { return n.Length > 60 ? n.Substring(0, 57) + "..." : n; }
  // every 30 s: one line per file being sent; a file nobody touched for 10 min is closed with a "stopped" line
  static void Ticker() {
    while (true) {
      Thread.Sleep(30000);
      try {
        List<string> lines = new List<string>();
        lock (xf) {
          List<string> drop = new List<string>();
          foreach (R2Xfer x in xf.Values) {
            if (x.Finished) { if ((DateTime.Now - x.LastByte).TotalMinutes > 2) drop.Add(x.Key); continue; }
            double idle = (DateTime.Now - x.LastByte).TotalSeconds;
            if (idle > 600) { Done(x, false); lines.Add(x.Room + " stopped " + Short(x.Name) + " at " + (100 * x.Done / Math.Max(1, x.Size)) + "% (" + Mb(x.Done) + " of " + Mb(x.Size) + ") - nothing asked for 10 min"); drop.Add(x.Key); continue; }
            double secs = Math.Max(1, (DateTime.Now - x.LastLog).TotalSeconds); double rate = (x.Done - x.LastLogDone) / secs;
            if (x.Done == x.LastLogDone && idle > 60) continue;   // quiet: don't repeat the same line
            string left = rate > 1024 ? " - about " + Dur((x.Size - x.Done) / rate) + " left" : "";
            lines.Add(x.Room + " - " + Short(x.Name) + ": " + Mb(x.Done) + " of " + Mb(x.Size) + " (" + (100 * x.Done / Math.Max(1, x.Size)) + "%) - " + (rate / 1048576).ToString("0.0") + " MB/s" + left + (x.Conns > 1 ? " - " + x.Conns + " connections" : ""));
            x.LastLog = DateTime.Now; x.LastLogDone = x.Done;
          }
          foreach (string k in drop) xf.Remove(k);
        }
        foreach (string l in lines) Log(l);
      } catch { }
    }
  }
  static string DlJson() {
    StringBuilder b = new StringBuilder("["); int n = 0;
    lock (xf) foreach (R2Xfer x in xf.Values) {
      double el = Math.Max(1, (x.LastByte - x.Start).TotalSeconds);
      b.Append(n++ > 0 ? "," : "").Append("{\"room\":" + Q(x.Room) + ",\"name\":" + Q(x.Name) + ",\"size\":" + x.Size + ",\"done\":" + x.Done + ",\"fin\":" + (x.Finished ? "true" : "false") +
        ",\"mbs\":" + (x.Done / el / 1048576).ToString("0.0", System.Globalization.CultureInfo.InvariantCulture) + ",\"conns\":" + x.Conns + ",\"idle\":" + (int)(DateTime.Now - x.LastByte).TotalSeconds + "}"); }
    return b.Append("]").ToString();
  }

  [DllImport("kernel32.dll")] static extern uint SetThreadExecutionState(uint f);

  // 1.4: the page shows the last lines of this window too
  static List<string> logs = new List<string>();
  public static void Log(string s) { string l = DateTime.Now.ToString("HH:mm:ss") + "  " + s; Console.WriteLine(l); lock (logs) { logs.Add(l); if (logs.Count > 200) logs.RemoveRange(0, logs.Count - 200); } }
  // 1.4: finished / stopped transfers for the page (newest first, 40 kept)
  static List<string> recent = new List<string>();
  static void Done(R2Xfer x, bool ok) { double secs = Math.Max(1, (x.LastByte - x.Start).TotalSeconds);
    string j = "{\"room\":" + Q(x.Room) + ",\"name\":" + Q(x.Name) + ",\"size\":" + x.Size + ",\"done\":" + x.Done + ",\"ok\":" + (ok ? "true" : "false") + ",\"secs\":" + (int)secs +
      ",\"mbs\":" + (x.Done / secs / 1048576).ToString("0.0", System.Globalization.CultureInfo.InvariantCulture) + ",\"end\":" + Q(x.LastByte.ToString("dd MMM HH:mm")) + "}";
    lock (recent) { recent.Insert(0, j); if (recent.Count > 40) recent.RemoveAt(40); } }
  static string LiveJson() {
    StringBuilder b = new StringBuilder("{\"ok\":true,\"v\":" + Q(Version) + ",\"push\":" + Q(PushNote) + ",\"active\":" + active + ",\"servedMB\":" + (servedBytes >> 20) + ",\"dl\":" + DlJson() + ",\"recent\":[");
    lock (recent) b.Append(string.Join(",", recent.ToArray())); b.Append("],\"log\":[");
    lock (logs) { int from = Math.Max(0, logs.Count - 60); for (int i = from; i < logs.Count; i++) b.Append(i > from ? "," : "").Append(Q(logs[i])); }
    return b.Append("]}").ToString(); }
  public static string PagePath;

  // ---------- folders ----------
  public static void LoadFolders() {
    Folders.Clear();
    if (File.Exists(ConfigPath)) foreach (string l in File.ReadAllLines(ConfigPath)) { string t = l.Trim(); if (t.Length > 0 && !Folders.Contains(t)) Folders.Add(t); }
  }
  static void SaveFolders() { File.WriteAllLines(ConfigPath, Folders.ToArray()); }

  // ---------- reading a PKG ----------
  static uint BE32(byte[] b, int o) { return (uint)(b[o] << 24 | b[o + 1] << 16 | b[o + 2] << 8 | b[o + 3]); }
  static uint LE32(byte[] b, int o) { return (uint)(b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24); }
  static int LE16(byte[] b, int o) { return b[o] | b[o + 1] << 8; }
  static string Cstr(byte[] b, int o, int max) { int e = o; while (e < o + max && e < b.Length && b[e] != 0) e++; return Encoding.UTF8.GetString(b, o, e - o); }
  static void ReadAt(FileStream f, long off, byte[] buf) { f.Seek(off, SeekOrigin.Begin); int n = 0; while (n < buf.Length) { int r = f.Read(buf, n, buf.Length - n); if (r <= 0) throw new Exception("file ends early"); n += r; } }

  public static R2Pkg Read(string p) {
    R2Pkg k = new R2Pkg(); k.Path = p; k.Name = System.IO.Path.GetFileName(p);
    using (System.Security.Cryptography.SHA1 sh = System.Security.Cryptography.SHA1.Create()) k.Id = BitConverter.ToString(sh.ComputeHash(Encoding.UTF8.GetBytes(p.ToLowerInvariant()))).Replace("-", "").Substring(0, 16).ToLowerInvariant();   // stable link id (the path), not the list position
    try {
      FileInfo fi = new FileInfo(p); k.Size = fi.Length; k.Mtime = (long)(fi.LastWriteTimeUtc - new DateTime(1970, 1, 1)).TotalSeconds;
      using (FileStream f = new FileStream(p, FileMode.Open, FileAccess.Read, FileShare.ReadWrite)) {
        byte[] h = new byte[0x100]; ReadAt(f, 0, h);
        if (BE32(h, 0) != 0x7F434E54) throw new Exception("not a PS4 package");
        uint n = BE32(h, 0x10), tab = BE32(h, 0x18);
        k.ContentId = Cstr(h, 0x40, 36);
        if (k.ContentId.Length >= 16) k.TitleId = k.ContentId.Substring(7, 9);
        if (n == 0 || n > 20000) throw new Exception("odd entry table");
        byte[] t = new byte[n * 32]; ReadAt(f, tab, t);
        for (int i = 0; i < n; i++) {
          if (BE32(t, i * 32) != 0x1000) continue;   // param.sfo
          uint off = BE32(t, i * 32 + 16), sz = BE32(t, i * 32 + 20);
          if (sz < 20 || sz > 1 << 20) break;
          byte[] s = new byte[sz]; ReadAt(f, off, s);
          if (LE32(s, 0) != 0x46535000) break;     // "\0PSF"
          int kt = (int)LE32(s, 8), dt = (int)LE32(s, 12), ne = (int)LE32(s, 16);
          for (int j = 0; j < ne && 20 + j * 16 + 16 <= s.Length; j++) {
            int e = 20 + j * 16, ko = LE16(s, e), fmt = LE16(s, e + 2); int len = (int)LE32(s, e + 4), dof = (int)LE32(s, e + 12);
            string key = Cstr(s, kt + ko, 64); int at = dt + dof; if (at < 0 || at >= s.Length) continue;
            string val = fmt == 0x0404 ? "0x" + LE32(s, at).ToString("x8") : Cstr(s, at, Math.Max(0, Math.Min(len, s.Length - at)));
            if (key == "TITLE") k.Title = val; else if (key == "CATEGORY") k.Category = val; else if (key == "APP_VER") k.AppVer = val;
            else if (key == "VERSION") k.Version = val; else if (key == "TITLE_ID" && string.IsNullOrEmpty(k.TitleId)) k.TitleId = val;
            else if (key == "SYSTEM_VER" && fmt == 0x0404) { uint v = LE32(s, at); k.SysVer = ((v >> 24) & 0xff).ToString("x") + "." + ((v >> 16) & 0xff).ToString("x2"); }
          }
          break;
        }
      }
      if (string.IsNullOrEmpty(k.Category)) k.Category = "?";
    } catch (Exception e) { k.Err = e.Message; }
    return k;
  }

  static void Walk(string d, List<string> o, int depth) {
    if (depth > 8) return;
    try { foreach (string f in Directory.GetFiles(d, "*.pkg")) o.Add(f); } catch { }
    try { foreach (string s in Directory.GetDirectories(d)) { string nm = System.IO.Path.GetFileName(s); if (nm.StartsWith("$") || nm == "System Volume Information") continue; Walk(s, o, depth + 1); } } catch { }
  }
  public static void Scan() {
    List<string> files = new List<string>(); foreach (string d in Folders) if (Directory.Exists(d)) Walk(d, files, 0);
    Dictionary<string, R2Pkg> old = new Dictionary<string, R2Pkg>(); lock (L) foreach (R2Pkg p in Pkgs) old[p.Path] = p;
    List<R2Pkg> nw = new List<R2Pkg>();
    foreach (string f in files) {
      R2Pkg o; long mt = 0, sz = 0; try { FileInfo fi = new FileInfo(f); sz = fi.Length; mt = (long)(fi.LastWriteTimeUtc - new DateTime(1970, 1, 1)).TotalSeconds; } catch { }
      if (old.TryGetValue(f, out o) && o.Size == sz && o.Mtime == mt) nw.Add(o); else nw.Add(Read(f));
    }
    lock (L) { Pkgs = nw; ScannedAt = DateTime.Now; }
    Log("scanned " + Folders.Count + " folder(s): " + nw.Count + " PKG file(s)");
  }

  // ---------- JSON ----------
  static string Q(string s) {
    if (s == null) return "null"; StringBuilder b = new StringBuilder("\"");
    foreach (char c in s) { if (c == '"' || c == '\\') b.Append('\\').Append(c); else if (c < 32) b.Append("\\u").Append(((int)c).ToString("x4")); else b.Append(c); }
    return b.Append('"').ToString();
  }
  static List<string> Ips() {
    List<string> o = new List<string>();
    foreach (NetworkInterface ni in NetworkInterface.GetAllNetworkInterfaces()) {
      if (ni.OperationalStatus != OperationalStatus.Up || ni.NetworkInterfaceType == NetworkInterfaceType.Loopback) continue;
      foreach (UnicastIPAddressInformation a in ni.GetIPProperties().UnicastAddresses)
        if (a.Address.AddressFamily == AddressFamily.InterNetwork) { string s = a.Address.ToString(); if (!s.StartsWith("169.254.")) o.Add(s + "|" + ni.Name); }
    }
    return o;
  }
  static string ListJson() {
    StringBuilder b = new StringBuilder("{\"ok\":true,\"push\":" + Q(PushNote) + ",\"staff\":" + (Staff().Length > 0 ? "true" : "false") + ",\"v\":" + Q(Version) + ",\"port\":" + Port + ",\"token\":" + Q(Token) + ",\"active\":" + active + ",\"servedMB\":" + (servedBytes >> 20) + ",\"dl\":" + DlJson() + ",\"scanned\":" + Q(ScannedAt.ToString("HH:mm:ss")) + ",\"ips\":[");
    List<string> ips = Ips(); for (int i = 0; i < ips.Count; i++) { string[] p = ips[i].Split('|'); b.Append(i > 0 ? "," : "").Append("{\"ip\":" + Q(p[0]) + ",\"nic\":" + Q(p[1]) + "}"); }
    b.Append("],\"folders\":[");
    for (int i = 0; i < Folders.Count; i++) b.Append(i > 0 ? "," : "").Append("{\"path\":" + Q(Folders[i]) + ",\"there\":" + (Directory.Exists(Folders[i]) ? "true" : "false") + "}");
    b.Append("],\"pkgs\":[");
    List<R2Pkg> ps; lock (L) ps = new List<R2Pkg>(Pkgs);
    for (int i = 0; i < ps.Count; i++) { R2Pkg p = ps[i];
      b.Append(i > 0 ? "," : "").Append("{\"id\":" + Q(p.Id) + ",\"name\":" + Q(p.Name) + ",\"path\":" + Q(p.Path) + ",\"size\":" + p.Size + ",\"cid\":" + Q(p.ContentId) + ",\"tid\":" + Q(p.TitleId) +
        ",\"title\":" + Q(p.Title) + ",\"cat\":" + Q(p.Category) + ",\"appver\":" + Q(p.AppVer) + ",\"ver\":" + Q(p.Version) + ",\"fw\":" + Q(p.SysVer) + ",\"err\":" + Q(p.Err) +
        ",\"url\":" + Q("/f/" + Token + "/" + p.Id + "/" + Uri.EscapeDataString(p.Name)) + "}"); }
    return b.Append("]}").ToString();
  }

  // ---------- folder picker (Windows dialog, on this laptop) ----------
  // ---------- tell the booking engine what's here (1.1, 20 Nov 11:10pm: the control centre page can't reach this laptop
  // directly from https, so the list goes to the engine; the control centre reads it from there, from any phone or computer) ----------
  static string lastPushed = ""; static DateTime lastPushAt = DateTime.MinValue; public static string PushNote = "not sent yet";
  static string Staff() { try { return File.Exists(StaffPath) ? File.ReadAllText(StaffPath).Trim() : ""; } catch { return ""; } }
  static string BestIp() { List<string> ips = Ips(); foreach (string x in ips) if (x.StartsWith("192.168.")) return x.Split('|')[0]; return ips.Count > 0 ? ips[0].Split('|')[0] : ""; }
  static string PushJson() {
    StringBuilder b = new StringBuilder("{\"v\":" + Q(Version) + ",\"ip\":" + Q(BestIp()) + ",\"port\":" + Port + ",\"token\":" + Q(Token) + ",\"dl\":" + DlJson() + ",\"folders\":[");   // 1.4: + what's being sent now
    for (int i = 0; i < Folders.Count; i++) b.Append(i > 0 ? "," : "").Append(Q(Folders[i]));
    b.Append("],\"pkgs\":[");
    List<R2Pkg> ps; lock (L) ps = new List<R2Pkg>(Pkgs); int n = 0;
    foreach (R2Pkg p in ps) { if (p.Err != null || p.TitleId == null) continue;
      b.Append(n++ > 0 ? "," : "").Append("{\"id\":" + Q(p.Id) + ",\"name\":" + Q(p.Name) + ",\"dir\":" + Q(System.IO.Path.GetDirectoryName(p.Path)) + ",\"size\":" + p.Size + ",\"cid\":" + Q(p.ContentId) + ",\"tid\":" + Q(p.TitleId) +
        ",\"title\":" + Q(p.Title) + ",\"cat\":" + Q(p.Category) + ",\"appver\":" + Q(p.AppVer) + ",\"ver\":" + Q(p.Version) + ",\"fw\":" + Q(p.SysVer) + "}"); }
    return b.Append("]}").ToString();
  }
  public static void Pusher() {
    ServicePointManager.SecurityProtocol = (SecurityProtocolType)3072;   // TLS 1.2
    while (true) {
      try {
        string st = Staff(), js = PushJson(); bool due = js != lastPushed || (DateTime.Now - lastPushAt).TotalMinutes >= 10;
        if (st.Length == 0) PushNote = "add the staff code on this page so the control centre sees your files";
        else if (due) {
          using (WebClient w = new WebClient()) {
            System.Collections.Specialized.NameValueCollection f = new System.Collections.Specialized.NameValueCollection();
            f.Add("staff", st); f.Add("action", "pkglap"); f.Add("data", js); f.Add("active", active.ToString()); f.Add("servedMB", (servedBytes >> 20).ToString());
            string r = Encoding.UTF8.GetString(w.UploadValues("https://round2-book.donttelldadyet.workers.dev/gm", f));
            if (r.Contains("\"ok\":true")) { lastPushed = js; lastPushAt = DateTime.Now; PushNote = "control centre up to date (" + DateTime.Now.ToString("HH:mm") + ")"; }
            else PushNote = "the engine said: " + (r.Length > 120 ? r.Substring(0, 120) : r);
          }
        }
      } catch (Exception e) { PushNote = "couldn't reach the engine: " + e.Message; }
      Thread.Sleep(30000);
    }
  }
  public static void PushNow() { lastPushed = ""; }

  static string Pick() {
    string res = null;
    Thread t = new Thread(delegate () {
      System.Windows.Forms.Form top = new System.Windows.Forms.Form(); top.TopMost = true; top.ShowInTaskbar = false;
      System.Windows.Forms.FolderBrowserDialog d = new System.Windows.Forms.FolderBrowserDialog();
      d.Description = "Round2: pick a folder or drive with PS4 PKG files"; d.ShowNewFolderButton = false;
      if (d.ShowDialog(top) == System.Windows.Forms.DialogResult.OK) res = d.SelectedPath; top.Dispose();
    });
    t.SetApartmentState(ApartmentState.STA); t.Start(); t.Join();
    return res;
  }

  // ---------- HTTP ----------
  public static void Run() {
    TcpListener l = new TcpListener(IPAddress.Any, Port); l.Start();
    Log("sharing on port " + Port + " - addresses: " + string.Join(", ", Ips().ToArray()));
    Thread keep = new Thread(KeepAwake); keep.IsBackground = true; keep.Start();
    Thread push = new Thread(Pusher); push.IsBackground = true; push.Start();
    Thread tick = new Thread(Ticker); tick.IsBackground = true; tick.Start();
    while (true) { TcpClient c = l.AcceptTcpClient(); Thread th = new Thread(Handle); th.IsBackground = true; th.Start(c); }
  }
  static void KeepAwake() {
    while (true) {
      bool busy = active > 0 || (DateTime.Now - lastDl).TotalMinutes < 30;
      try { SetThreadExecutionState(busy ? 0x80000001u : 0x80000000u); } catch { }   // ES_CONTINUOUS | ES_SYSTEM_REQUIRED while downloads run
      Thread.Sleep(30000);
    }
  }
  static void Send(Stream s, int code, string type, byte[] body, string extra) {
    string st = code == 200 ? "OK" : code == 204 ? "No Content" : code == 206 ? "Partial Content" : code == 403 ? "Forbidden" : code == 404 ? "Not Found" : code == 416 ? "Range Not Satisfiable" : "Error";
    string h = "HTTP/1.1 " + code + " " + st + "\r\nContent-Type: " + type + "\r\nContent-Length: " + (body == null ? 0 : body.Length) +
      "\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Private-Network: true\r\nAccess-Control-Allow-Headers: *\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nCache-Control: no-store\r\nConnection: close\r\n" + (extra ?? "") + "\r\n";
    byte[] hb = Encoding.ASCII.GetBytes(h); s.Write(hb, 0, hb.Length); if (body != null && body.Length > 0) s.Write(body, 0, body.Length);
  }
  static void Handle(object o) {
    TcpClient c = (TcpClient)o;
    try {
      c.NoDelay = true; c.SendBufferSize = 1 << 20; NetworkStream s = c.GetStream(); s.ReadTimeout = 30000;
      string line = ReadLine(s); if (line == null) return;
      Dictionary<string, string> hd = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase); string hl;
      while (!string.IsNullOrEmpty(hl = ReadLine(s))) { int k = hl.IndexOf(':'); if (k > 0) hd[hl.Substring(0, k).Trim()] = hl.Substring(k + 1).Trim(); }
      string[] p = line.Split(' '); if (p.Length < 2) return; string m = p[0], path = p[1];
      bool local = IPAddress.IsLoopback(((IPEndPoint)c.Client.RemoteEndPoint).Address);
      if (m == "OPTIONS") { Send(s, 204, "text/plain", null, null); return; }
      if (path.StartsWith("/f/")) { File_(s, m, path, hd, c); return; }
      if (path.StartsWith("/j/")) { Manifest(s, path, hd); return; }
      if (!local) { Send(s, 403, "text/plain", Encoding.ASCII.GetBytes("this laptop only"), null); return; }
      string body = "";
      if (m == "POST") { int len = 0; string cl; if (hd.TryGetValue("Content-Length", out cl)) int.TryParse(cl, out len); if (len > 0 && len < 1 << 16) { byte[] b = new byte[len]; int n = 0; while (n < len) { int r = s.Read(b, n, len - n); if (r <= 0) break; n += r; } body = Encoding.UTF8.GetString(b, 0, n); } }
      string q = path.Contains("?") ? Uri.UnescapeDataString(path.Substring(path.IndexOf('?') + 1)) : ""; string route = path.Split('?')[0];
      if (route == "/api/staff") { string k = body.Trim(); if (k.StartsWith("code=")) k = Uri.UnescapeDataString(k.Substring(5)); if (k.Length > 0 && k.Length < 200) { File.WriteAllText(StaffPath, k); PushNow(); } Send(s, 200, "application/json", Encoding.UTF8.GetBytes(ListJson()), null); return; }
      if (route == "/api/live") { Send(s, 200, "application/json", Encoding.UTF8.GetBytes(LiveJson()), null); return; }
      if (route == "/api/list") { Send(s, 200, "application/json", Encoding.UTF8.GetBytes(ListJson()), null); return; }
      if (route == "/api/scan") { Scan(); Send(s, 200, "application/json", Encoding.UTF8.GetBytes(ListJson()), null); return; }
      if (route == "/api/pick") { string f = Pick(); if (f != null && !Folders.Contains(f)) { Folders.Add(f); SaveFolders(); Scan(); } Send(s, 200, "application/json", Encoding.UTF8.GetBytes(ListJson()), null); return; }
      if (route == "/api/add") { string f = (body.Length > 0 ? body : q).Trim(); if (f.StartsWith("path=")) f = f.Substring(5); if (Directory.Exists(f) && !Folders.Contains(f)) { Folders.Add(f); SaveFolders(); Scan(); } Send(s, 200, "application/json", Encoding.UTF8.GetBytes(ListJson()), null); return; }
      if (route == "/api/remove") { string f = (body.Length > 0 ? body : q).Trim(); if (f.StartsWith("path=")) f = f.Substring(5); if (Folders.Remove(f)) { SaveFolders(); Scan(); } Send(s, 200, "application/json", Encoding.UTF8.GetBytes(ListJson()), null); return; }
      if (route == "/" || route == "/index.html") { string pg = null; try { if (PagePath != null && File.Exists(PagePath)) pg = File.ReadAllText(PagePath); } catch { }   // 1.4: the page lives in pkg-page.html (built-in one if it's missing)
        Send(s, 200, "text/html; charset=utf-8", Encoding.UTF8.GetBytes(pg ?? Page()), null); return; }
      Send(s, 404, "text/plain", Encoding.ASCII.GetBytes("not found"), null);
    } catch (Exception) { } finally { try { c.Close(); } catch { } }
  }
  static string ReadLine(Stream s) {
    StringBuilder b = new StringBuilder(); int ch, n = 0;
    while ((ch = s.ReadByte()) >= 0) { if (ch == '\n') return b.ToString().TrimEnd('\r'); b.Append((char)ch); if (++n > 8192) return null; }
    return b.Length > 0 ? b.ToString() : null;
  }
  static void File_(NetworkStream s, string m, string path, Dictionary<string, string> hd, TcpClient c) {
    string[] seg = path.Split('/');   // "", "f", token, id, name
    R2Pkg p = null;
    if (seg.Length >= 4 && seg[2] == Token) lock (L) foreach (R2Pkg x in Pkgs) if (x.Id == seg[3]) { p = x; break; }
    if (p == null || !File.Exists(p.Path)) { Send(s, 404, "text/plain", Encoding.ASCII.GetBytes("no such file"), null); return; }
    long size = new FileInfo(p.Path).Length, a = 0, b = size - 1; bool part = false; string rg;
    if (hd.TryGetValue("Range", out rg) && rg.StartsWith("bytes=")) {
      string[] r = rg.Substring(6).Split(','); string[] ab = r[0].Split('-');
      if (ab[0].Length > 0) { a = long.Parse(ab[0]); if (ab.Length > 1 && ab[1].Length > 0) b = Math.Min(long.Parse(ab[1]), size - 1); }
      else if (ab.Length > 1 && ab[1].Length > 0) { a = Math.Max(0, size - long.Parse(ab[1])); }
      if (a > b || a >= size) { Send(s, 416, "text/plain", null, "Content-Range: bytes */" + size + "\r\n"); return; }
      part = true;
    }
    long len = b - a + 1;
    string h = "HTTP/1.1 " + (part ? "206 Partial Content" : "200 OK") + "\r\nContent-Type: application/octet-stream\r\nContent-Length: " + len + "\r\nAccept-Ranges: bytes\r\n" +
      (part ? "Content-Range: bytes " + a + "-" + b + "/" + size + "\r\n" : "") + "Connection: close\r\n\r\n";
    byte[] hb = Encoding.ASCII.GetBytes(h); s.Write(hb, 0, hb.Length);
    if (m == "HEAD") return;
    Interlocked.Increment(ref active); lastDl = DateTime.Now;
    string who = ((IPEndPoint)c.Client.RemoteEndPoint).Address.ToString();
    R2Xfer xt; string key = who + "|" + p.Id; bool fresh = false;
    lock (xf) { if (!xf.TryGetValue(key, out xt) || xt.Finished) { xt = new R2Xfer(); xt.Key = key; xt.Who = who; xt.Room = RoomOf(who); xt.Name = p.Name; xt.Size = size;
        xt.Start = xt.LastByte = xt.LastLog = DateTime.Now; xf[key] = xt; fresh = true; } xt.Conns++; }
    if (fresh) Log(xt.Room + " started " + p.Name + " (" + Mb(size) + ")" + (a > 0 ? " - from " + Mb(a) : ""));
    long pos = a;
    try {
      using (FileStream f = new FileStream(p.Path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite, 1 << 20)) {
        f.Seek(a, SeekOrigin.Begin); byte[] buf = new byte[1 << 20]; long left = len;
        while (left > 0) { int r = f.Read(buf, 0, (int)Math.Min(buf.Length, left)); if (r <= 0) break; s.Write(buf, 0, r); left -= r; lastDl = DateTime.Now;
          bool done = false; double secs = 0;
          lock (xf) { long nw = xt.Add(pos, pos + r); Interlocked.Add(ref servedBytes, nw); xt.LastByte = DateTime.Now;
            if (!xt.Finished && xt.Done >= xt.Size) { xt.Finished = true; done = true; secs = Math.Max(1, (xt.LastByte - xt.Start).TotalSeconds); } }
          pos += r;
          if (done) { Done(xt, true); }
          if (done) Log(xt.Room + " finished " + p.Name + ": " + Mb(size) + " in " + Dur(secs) + " (" + (size / secs / 1048576).ToString("0.0") + " MB/s average)");
        }
      }
    } finally { Interlocked.Decrement(ref active); lastDl = DateTime.Now; lock (xf) xt.Conns--; }
  }
  // 1.2 (Kaeden 23:21: the PS4 took a DLC from a direct link but refused the game): the PS4's download manager wants what the
  // PlayStation Store gives it - a small JSON "reference package" that lists the pieces of the PKG - like flatz's Remote Package
  // Installer makes: originalFileSize + packageDigest from the PKG header, one piece = the whole file on this laptop.
  static void Manifest(NetworkStream s, string path, Dictionary<string, string> hd) {
    string[] seg = path.Split('/');   // "", "j", token, "<id>.json"
    R2Pkg p = null; string id = seg.Length >= 4 ? seg[3].Replace(".json", "") : "";
    if (seg.Length >= 4 && seg[2] == Token) lock (L) foreach (R2Pkg x in Pkgs) if (x.Id == id) { p = x; break; }
    if (p == null || !File.Exists(p.Path)) { Send(s, 404, "text/plain", Encoding.ASCII.GetBytes("no such file"), null); return; }
    byte[] h = new byte[0x1000];
    using (FileStream f = new FileStream(p.Path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite)) ReadAt(f, 0, h);
    ulong psize = 0; for (int i = 0; i < 8; i++) psize = (psize << 8) | h[0x430 + i];
    StringBuilder dg = new StringBuilder(); for (int i = 0; i < 32; i++) dg.Append(h[0xFE0 + i].ToString("x2"));
    long size = new FileInfo(p.Path).Length; string host; if (!hd.TryGetValue("Host", out host)) host = BestIp() + ":" + Port;
    string js = "{\"originalFileSize\":" + (psize > 0 ? psize : (ulong)size) + ",\"packageDigest\":\"" + dg + "\",\"numberOfSplitFiles\":1,\"pieces\":[{\"url\":\"http://" + host + "/f/" + Token + "/" + p.Id + "/x.pkg\",\"fileOffset\":0,\"fileSize\":" + size + ",\"hashValue\":\"0000000000000000000000000000000000000000\"}]}";
    Log("manifest for " + p.Name + " (" + (size >> 20) + " MB) sent");
    Send(s, 200, "application/json", Encoding.ASCII.GetBytes(js), null);
  }
  static string Page() {
    return "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'><title>Round2 PKG server</title>" +
      "<style>body{font:15px system-ui;margin:20px;background:#111;color:#eee}button{font:inherit;padding:6px 12px;margin:2px}td{padding:3px 8px;border-bottom:1px solid #333}.g{color:#8f8}.p{color:#8cf}.a{color:#fc8}.e{color:#f88}</style>" +
      "<h2>Round2 PKG server</h2><p>Keep this running. Send games from the control centre (staff.round2.sg &rsaquo; PS4 games).</p>" +
      "<div id=k></div><div id=f></div><button onclick=\"go('/api/pick')\">+ Add folder or drive</button> <button onclick=\"go('/api/scan')\">Scan again</button><div id=l></div>" +
      "<script>function mb(b){return b>=1073741824?(b/1073741824).toFixed(2)+' GB':Math.round(b/1048576)+' MB'}" +
      "function dl(a){if(!a||!a.length)return '<h3>Sending now</h3><p>Nothing right now.</p>';return '<h3>Sending now</h3><table>'+a.map(x=>'<tr><td>'+x.room+'</td><td>'+x.name+'</td><td>'+mb(x.done)+' of '+mb(x.size)+'</td><td><b>'+Math.floor(100*x.done/Math.max(1,x.size))+'%</b></td><td>'+(x.fin?'<span class=g>finished</span>':x.idle>60?'<span class=a>waiting '+x.idle+' s</span>':x.mbs+' MB/s')+'</td></tr>').join('')+'</table>'}" +
      "function go(u){fetch(u).then(r=>r.json()).then(show)}function rm(p){fetch('/api/remove',{method:'POST',body:p}).then(r=>r.json()).then(show)}" +
      "function stf(){fetch('/api/staff',{method:'POST',body:document.getElementById('sc').value}).then(r=>r.json()).then(show)}" +
      "function show(j){document.getElementById('k').innerHTML='<p>Control centre: '+j.push+'</p>'+(j.staff?'':'<p>Staff code: <input id=sc type=password> <button onclick=stf()>Save</button></p>');document.getElementById('f').innerHTML='<h3>Folders</h3>'+j.folders.map(f=>'<div>'+f.path+(f.there?'':' <span class=e>(not connected)</span>')+' <button onclick=\\'rm('+JSON.stringify(f.path)+')\\'>remove</button></div>').join('')+'<p>Shared at '+j.ips.map(i=>i.ip+' ('+i.nic+')').join(', ')+' port '+j.port+' &middot; sent '+j.servedMB+' MB</p>'+dl(j.dl);" +
      "var c={gd:'g',gp:'p',ac:'a'},n={gd:'Game',gp:'Patch',ac:'DLC'};document.getElementById('l').innerHTML='<h3>'+j.pkgs.length+' PKG files</h3><table>'+j.pkgs.map(p=>'<tr><td class='+(p.err?'e':c[p.cat]||'')+'>'+(p.err?'unreadable':n[p.cat]||p.cat)+'</td><td>'+(p.title||p.name)+'</td><td>'+(p.tid||'')+'</td><td>'+(p.appver||p.ver||'')+'</td><td>FW '+(p.fw||'?')+'</td><td>'+(p.size/1073741824).toFixed(2)+' GB</td><td>'+p.name+'</td></tr>').join('')+'</table>'}go('/api/list');setInterval(()=>go('/api/list'),5000)</script>";
  }
}
'@
Add-Type -TypeDefinition $code -ReferencedAssemblies System.Windows.Forms -Language CSharp
[R2Srv]::Port = $port
[R2Srv]::ConfigPath = Join-Path $here 'pkg-folders.txt'
[R2Srv]::RoomsPath = Join-Path $here 'pkg-rooms.txt'   # 1.3: "<PS4 address> <room>" per line, so the window says Kaeden instead of 192.168.88.5
[R2Srv]::LoadRooms()
[R2Srv]::PagePath = Join-Path $here 'pkg-page.html'   # 1.4: the page (Round2 look)
[R2Srv]::StaffPath = Join-Path $here 'pkg-staff.txt'   # the staff code, so the list reaches the control centre (via the engine)
# the secret part of the file links - kept in pkg-token.txt so queued jobs still work after a restart of this program
$tokFile = Join-Path $here 'pkg-token.txt'
if (Test-Path $tokFile) { $tok = (Get-Content $tokFile -Raw).Trim() }
if (-not $tok -or $tok.Length -lt 16) { $b = New-Object byte[] 12; (New-Object System.Security.Cryptography.RNGCryptoServiceProvider).GetBytes($b); $tok = ([BitConverter]::ToString($b) -replace '-', '').ToLower(); Set-Content -Path $tokFile -Value $tok }
[R2Srv]::Token = $tok
[R2Srv]::LoadFolders()
Write-Host "Round2 PKG server - keep this window open. Page: http://localhost:$port/" -ForegroundColor Cyan
if ([R2Srv]::Folders.Count -eq 0) { Write-Host 'No folders yet - opening the page so you can add one.'; }
[R2Srv]::Scan()
Start-Process "http://localhost:$port/"
[R2Srv]::Run()
