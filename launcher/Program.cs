// ZHLauncher — one double-click: fetch newest build, swap it in, start the game.
// Lives inside the game folder next to generalszh.exe. Sent to the second machine ONCE: it updates the game, the map pack,
// the shipped settings (release.json inside the game zip) and itself.
//
// Per-machine settings:  launcher.json beside this exe (Repo, Debug, AllowMismatch, ExtraArgs; MapPack/Args only as fallbacks).
// Shipped settings:      release.json, delivered inside every zh-game-<tag>.zip (MapPack, Args). Wins over launcher.json.
using System.Diagnostics;
using System.IO.Compression;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;

var dir = AppContext.BaseDirectory;
var cfgPath = Path.Combine(dir, "launcher.json");
var versionPath = Path.Combine(dir, "BUILD_VERSION.txt");
var selfPath = Environment.ProcessPath ?? Path.Combine(dir, "ZHLauncher.exe");
var selfVersion = (Assembly.GetEntryAssembly()?.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion ?? "dev")
                  .Split('+')[0];   // the SDK appends +<git sha>

// Leftover from a previous self-update (the running exe could only be renamed, not deleted).
try { var old = selfPath + ".old"; if (File.Exists(old)) File.Delete(old); } catch { }

var cfg = File.Exists(cfgPath)
    ? JsonSerializer.Deserialize<Config>(File.ReadAllText(cfgPath)) ?? new Config()
    : new Config();

// ---- First-run setup --------------------------------------------------------
// If this exe is not sitting inside a Zero Hour folder (no INIZH.big beside it), build one: locate the Steam install,
// copy it to SetupTarget minus GenTool/GenPatcher/GenLauncher leftovers (Steam's own files are never touched), put this
// launcher and launcher.json in the copy, make a desktop shortcut, and hand over to the copy. "Unzip anywhere, double-click."
if (!File.Exists(Path.Combine(dir, "INIZH.big")) || args.Contains("-setup"))
{
    try
    {
        var steam = FindSteamZeroHour() ?? throw new Exception("Steam's Zero Hour folder was not found. Install Zero Hour from Steam first.");
        var setupDir = string.IsNullOrWhiteSpace(cfg.SetupTarget) ? @"C:\Games\ZeroHour-Custom" : cfg.SetupTarget;
        Log($"First-run setup: Steam install at {steam}");
        Log($"Creating a clean copy at {setupDir} (a few GB, a couple of minutes)...");
        var copied = CopyGameFolder(steam, setupDir);
        Log($"Copied {copied} files.");
        foreach (var f in new[] { Path.GetFileName(selfPath), "launcher.json", "README-SETUP.txt" })
        {
            var src = Path.Combine(dir, f);
            if (File.Exists(src)) File.Copy(src, Path.Combine(setupDir, f), overwrite: true);
        }
        try
        {
            var lnk = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Desktop), "Zero Hour (custom).lnk");
            dynamic shell = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell")!)!;
            var sc = shell.CreateShortcut(lnk);
            sc.TargetPath = Path.Combine(setupDir, Path.GetFileName(selfPath));
            sc.WorkingDirectory = setupDir;
            sc.IconLocation = Path.Combine(setupDir, "GeneralsZH.ico");
            sc.Save();
            Log($"Desktop shortcut created: {lnk}");
        }
        catch (Exception ex) { Log($"Could not create a desktop shortcut ({ex.Message}); start {setupDir}\\{Path.GetFileName(selfPath)} directly."); }
        Log("Setup done. Continuing from the new folder...");
        Process.Start(new ProcessStartInfo(Path.Combine(setupDir, Path.GetFileName(selfPath))) { WorkingDirectory = setupDir, UseShellExecute = true });
        return 0;
    }
    catch (Exception ex)
    {
        Log($"SETUP FAILED: {ex.Message}");
        Log("Fix the above (or copy the Steam game folder by hand and put this exe inside it). Press any key.");
        Console.ReadKey();
        return 3;
    }
}

string? localTag = File.Exists(versionPath) ? File.ReadAllText(versionPath).Split(' ')[0].Trim() : null;
Log($"Launcher {selfVersion}; installed build: {localTag ?? "(none)"}");

using var http = new HttpClient { Timeout = TimeSpan.FromMinutes(5) };
http.DefaultRequestHeaders.UserAgent.ParseAdd("ZHLauncher/1.0");
http.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
if (!string.IsNullOrWhiteSpace(cfg.Token))
    http.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", cfg.Token);

Release? gameRel = null;
try
{
    // Not /releases/latest: that is simply the newest non-prerelease by date, and the repo also publishes add-on and
    // map-pack releases (addons-v1, maps-v1) as prereleases. Pick the newest release that is a game release: tag "v*",
    // not a prerelease/draft, and carrying a zh-game-*.zip asset.
    var rels = await http.GetFromJsonAsync<Release[]>($"https://api.github.com/repos/{cfg.Repo}/releases?per_page=30")
               ?? throw new Exception("no releases returned");
    gameRel = rels.FirstOrDefault(r => r.tag_name.StartsWith("v", StringComparison.OrdinalIgnoreCase)
                                       && !r.prerelease && !r.draft
                                       && r.assets.Any(a => a.name.StartsWith("zh-game-", StringComparison.OrdinalIgnoreCase)))
              ?? throw new Exception("no game release (tag v*, with zh-game-*.zip) found");
    Log($"Latest release:  {gameRel.tag_name}");

    if (gameRel.tag_name != localTag)
    {
        var asset = gameRel.assets.First(a => a.name.StartsWith("zh-game-", StringComparison.OrdinalIgnoreCase));
        Log($"Downloading {asset.name} ({asset.size / 1024 / 1024} MB)...");
        var tmp = await Download(http, asset);

        // Keep one backup of the previous exe in case a build is broken
        var exe = Path.Combine(dir, cfg.Executable);
        if (File.Exists(exe)) File.Copy(exe, exe + ".prev", overwrite: true);

        ZipFile.ExtractToDirectory(tmp, dir, overwriteFiles: true);
        File.Delete(tmp);
        Log($"Updated to {gameRel.tag_name}");
    }
    else Log("Already up to date.");
}
catch (Exception ex)
{
    // Offline, GitHub down: just play whatever is installed.
    Log($"Update check skipped: {ex.Message}");
}

// ---- Launcher self-update ----------------------------------------------------
// The release carries zh-launcher-<tag>.zip built from the same commit. If this exe's embedded version differs, swap it:
// a running exe cannot be overwritten or deleted on Windows, but it can be renamed, so: rename self -> .old, drop the new
// exe in place, start it, exit. ZHLAUNCHER_UPDATED guards against a loop if versions could never agree.
if (gameRel is not null && gameRel.tag_name != selfVersion && Environment.GetEnvironmentVariable("ZHLAUNCHER_UPDATED") is null)
{
    try
    {
        var asset = gameRel.assets.FirstOrDefault(a => a.name.Equals($"zh-launcher-{gameRel.tag_name}.zip", StringComparison.OrdinalIgnoreCase));
        if (asset is null) Log($"Launcher {selfVersion} != release {gameRel.tag_name}, but the release has no launcher zip; keeping this one.");
        else
        {
            Log($"Updating launcher {selfVersion} -> {gameRel.tag_name}...");
            var tmp = await Download(http, asset);
            var stage = Path.Combine(Path.GetTempPath(), "zh-launcher-" + gameRel.tag_name);
            if (Directory.Exists(stage)) Directory.Delete(stage, recursive: true);
            ZipFile.ExtractToDirectory(tmp, stage);
            File.Delete(tmp);
            var fresh = Path.Combine(stage, Path.GetFileName(selfPath));
            if (!File.Exists(fresh)) throw new Exception($"{Path.GetFileName(selfPath)} not in launcher zip");
            var old = selfPath + ".old";
            if (File.Exists(old)) File.Delete(old);
            File.Move(selfPath, old);
            File.Copy(fresh, selfPath);
            Directory.Delete(stage, recursive: true);
            Log("Launcher updated; restarting.");
            var psi = new ProcessStartInfo(selfPath) { WorkingDirectory = dir, UseShellExecute = false };
            psi.Environment["ZHLAUNCHER_UPDATED"] = "1";
            Process.Start(psi);
            return 0;
        }
    }
    catch (Exception ex)
    {
        Log($"Launcher self-update skipped: {ex.Message}");
    }
}

// ---- Shipped settings (release.json from the game zip) overrule launcher.json fallbacks -------------------------------
var shipped = new Shipped();
var shippedPath = Path.Combine(dir, "release.json");
if (File.Exists(shippedPath))
{
    try { shipped = JsonSerializer.Deserialize<Shipped>(File.ReadAllText(shippedPath)) ?? shipped; }
    catch (Exception ex) { Log($"release.json unreadable ({ex.Message}); using launcher.json values."); }
}
var mapPack = string.IsNullOrWhiteSpace(shipped.MapPack) ? cfg.MapPack : shipped.MapPack;
var gameArgs = string.IsNullOrWhiteSpace(shipped.Args) ? cfg.Args : shipped.Args;
if (!string.IsNullOrWhiteSpace(cfg.ExtraArgs)) gameArgs += " " + cfg.ExtraArgs.Trim();

// ---- Map pack ------------------------------------------------------------
// Custom maps live in the user's Documents folder, not the game folder, so the game zip can't carry them. A versioned
// pack (scripts/make-map-pack.ps1 -Publish) is a release asset zh-maps-<MapPack>.zip; both machines install the same one.
if (!string.IsNullOrWhiteSpace(mapPack))
{
    try
    {
        var mapsDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),
                                   "Command and Conquer Generals Zero Hour Data", "Maps");
        var marker = Path.Combine(mapsDir, ".zh-mappack");
        var installed = File.Exists(marker) ? File.ReadAllText(marker).Trim() : "(none)";
        if (installed == mapPack) Log($"Map pack {mapPack} already installed.");
        else
        {
            var rel = await http.GetFromJsonAsync<Release>($"https://api.github.com/repos/{cfg.Repo}/releases/tags/{mapPack}")
                      ?? throw new Exception($"no release tagged {mapPack}");
            var asset = rel.assets.FirstOrDefault(a => a.name.Equals($"zh-maps-{mapPack}.zip", StringComparison.OrdinalIgnoreCase))
                        ?? throw new Exception($"release {mapPack} has no zh-maps-{mapPack}.zip");
            Log($"Installing map pack {mapPack} ({asset.size / 1024 / 1024} MB) over {installed}...");
            var tmp = await Download(http, asset);
            Directory.CreateDirectory(mapsDir);
            ZipFile.ExtractToDirectory(tmp, mapsDir, overwriteFiles: true);   // adds/overwrites; never deletes the player's own maps
            File.Delete(tmp);
            Log($"Map pack {mapPack} installed to {mapsDir}");
        }
    }
    catch (Exception ex)
    {
        Log($"Map pack check skipped: {ex.Message}");
    }
}

var target = Path.Combine(dir, cfg.Executable);
if (!File.Exists(target))
{
    Log($"ERROR: {cfg.Executable} not found in {dir}. Press any key.");
    Console.ReadKey();
    return 1;
}

// ---- Pre-flight ----------------------------------------------------------
var problems = new List<string>();

// Vulkan: DXVK silently no-ops without it, which means two different render paths across the two machines.
if (File.Exists(Path.Combine(dir, "d3d9.dll")) && !File.Exists(Path.Combine(Environment.SystemDirectory, "vulkan-1.dll")))
    problems.Add("DXVK is bundled but no Vulkan runtime found (vulkan-1.dll). Update GPU drivers or remove d3d8.dll/d3d9.dll.");

// Assets: a .big mismatch desyncs exactly like a code mismatch. assets.sha256 ships in the release zip.
var manifest = Path.Combine(dir, "assets.sha256");
if (File.Exists(manifest))
{
    foreach (var line in File.ReadLines(manifest).Where(l => l.Trim().Length > 0 && !l.StartsWith('#')))
    {
        var parts = line.Split(new[] { ' ', '*' }, 2, StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length != 2) continue;
        var (want, rel) = (parts[0].Trim().ToLowerInvariant(), parts[1].Trim());
        var file = Path.Combine(dir, rel);
        if (!File.Exists(file)) { problems.Add($"Missing asset: {rel}"); continue; }
        using var fs = File.OpenRead(file);
        var got = Convert.ToHexString(SHA256.HashData(fs)).ToLowerInvariant();
        if (got != want) problems.Add($"Asset differs from release: {rel} (yours {got[..8]}, expected {want[..8]})");
    }
    Log(problems.Count == 0 ? "Assets match release manifest." : "Asset check FAILED.");
}
else Log("No assets.sha256 in folder; skipping asset check.");

// Surface the last crash dump so it gets sent instead of forgotten. The engine's own MiniDumper writes
// CrashM*/CrashF*.dmp to the user-data folder under Documents; the game-dir folder is kept for anything else.
var dumpDirs = new[]
{
    Path.Combine(dir, "crashdumps"),
    Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "Command and Conquer Generals Zero Hour Data", "CrashDumps"),
};
var lastDump = dumpDirs.Where(Directory.Exists)
    .SelectMany(d => new DirectoryInfo(d).GetFiles("*.dmp"))
    .OrderByDescending(f => f.LastWriteTime).FirstOrDefault();
if (lastDump is not null && lastDump.LastWriteTime > DateTime.Now.AddDays(-2))
    Log($"NOTE: crash dump from last session at {lastDump.FullName}. Send the CrashM*.dmp (small) along with BUILD_VERSION.txt.");

if (problems.Count > 0)
{
    foreach (var p in problems) Log("  ! " + p);
    Log(cfg.AllowMismatch ? "Launching anyway (AllowMismatch=true); expect desyncs." : "Not launching. Fix the above or set AllowMismatch in launcher.json. Press any key.");
    if (!cfg.AllowMismatch) { Console.ReadKey(); return 2; }
}

// ---- Launch ----------------------------------------------------------------
if (cfg.Debug)
{
    Directory.CreateDirectory(Path.Combine(dir, "logs"));
    gameArgs += $" -netlog \"logs/{DateTime.Now:yyyyMMdd-HHmmss}.log\"";   // flag added in Phase 4 instrumentation
}

Log($"Launching {cfg.Executable} {gameArgs}");
Process.Start(new ProcessStartInfo(target, gameArgs) { WorkingDirectory = dir, UseShellExecute = true });
return 0;

static void Log(string s) => Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] {s}");

// Steam writes the game's registry key (installScript.vdf); fall back to scanning the Steam library folders.
static string? FindSteamZeroHour()
{
    static bool IsZh(string? p) => p is not null && File.Exists(Path.Combine(p, "INIZH.big")) && File.Exists(Path.Combine(p, "generals.exe"));
    foreach (var key in new[] { @"SOFTWARE\WOW6432Node\Electronic Arts\EA Games\Command and Conquer Generals Zero Hour",
                                @"SOFTWARE\Electronic Arts\EA Games\Command and Conquer Generals Zero Hour" })
    {
        var p = Microsoft.Win32.Registry.LocalMachine.OpenSubKey(key)?.GetValue("InstallPath") as string;
        if (IsZh(p)) return Path.TrimEndingDirectorySeparator(p!);
    }
    var libs = new List<string>();
    var steamRoot = Microsoft.Win32.Registry.LocalMachine.OpenSubKey(@"SOFTWARE\WOW6432Node\Valve\Steam")?.GetValue("InstallPath") as string
                    ?? @"C:\Program Files (x86)\Steam";
    libs.Add(steamRoot);
    var vdf = Path.Combine(steamRoot, "steamapps", "libraryfolders.vdf");
    if (File.Exists(vdf))
        foreach (var line in File.ReadLines(vdf))
        {
            var t = line.Trim();
            if (t.StartsWith("\"path\"")) libs.Add(t.Split('"', StringSplitOptions.RemoveEmptyEntries).Last().Replace(@"\\", @"\"));
        }
    foreach (var lib in libs.Distinct())
        foreach (var name in new[] { "Command & Conquer Generals - Zero Hour",
                                     @"Command and Conquer Generals - Zero Hour\Command and Conquer Generals Zero Hour",
                                     "Command and Conquer Generals - Zero Hour" })
        {
            var p = Path.Combine(lib, "steamapps", "common", name);
            if (IsZh(p)) return p;
        }
    return null;
}

// Copy the Steam folder minus third-party leftovers (GenTool proxy, GenPatcher add-ons/backups, GenLauncher, a stale rebuilt exe).
// Same list as scripts/stage-install.ps1. Idempotent: existing identical files are skipped.
static int CopyGameFolder(string src, string dst)
{
    var skipFile = new System.Text.RegularExpressions.Regex(
        @"^(\d{3}_.*\.big|d3d8\.dll|d3d8\.cfg|d3d9\.dll|dxvk\.conf|GenToolUpdater\.exe|game\.dat-VISION\.log|EdgeScroller\.exe|CustomContentMaps\.big|generalszh\.exe|.*\.bak)$",
        System.Text.RegularExpressions.RegexOptions.IgnoreCase);
    var skipDir = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { "launcher", "crashdumps", "logs" };
    int n = 0;
    foreach (var file in Directory.EnumerateFiles(src, "*", SearchOption.AllDirectories))
    {
        var rel = Path.GetRelativePath(src, file);
        var top = rel.Split(Path.DirectorySeparatorChar)[0];
        if (skipDir.Contains(top) && rel.Contains(Path.DirectorySeparatorChar)) continue;
        if (skipFile.IsMatch(Path.GetFileName(file)) && !rel.Contains(Path.DirectorySeparatorChar)) continue;   // top level only; Data\INI\INIZH.big stays (engine ignores it)
        var to = Path.Combine(dst, rel);
        Directory.CreateDirectory(Path.GetDirectoryName(to)!);
        var si = new FileInfo(file);
        var di = new FileInfo(to);
        if (!di.Exists || di.Length != si.Length || di.LastWriteTimeUtc != si.LastWriteTimeUtc) File.Copy(file, to, overwrite: true);
        if (++n % 100 == 0) Log($"  ... {n} files");
    }
    return n;
}

static async Task<string> Download(HttpClient http, Asset asset)
{
    // The API asset URL with Accept: application/octet-stream works for public and private repos alike.
    using var req = new HttpRequestMessage(HttpMethod.Get, asset.url);
    req.Headers.Accept.Clear();
    req.Headers.Accept.ParseAdd("application/octet-stream");
    using var resp = await http.SendAsync(req, HttpCompletionOption.ResponseHeadersRead);
    resp.EnsureSuccessStatusCode();
    var tmp = Path.Combine(Path.GetTempPath(), asset.name);
    await using (var fs = File.Create(tmp)) await resp.Content.CopyToAsync(fs);
    return tmp;
}

record Config
{
    public string Repo { get; init; } = "jj-elliott/zh-custom";
    public string Token { get; init; } = "";          // only needed for a private repo (fine-grained PAT, Contents: Read)
    public string Executable { get; init; } = "generalszh.exe";
    public string Args { get; init; } = "-win";       // fallback only; release.json (shipped in the game zip) wins
    public string ExtraArgs { get; init; } = "";      // per-machine additions, e.g. "-xres 2560 -yres 1440"
    public bool Debug { get; init; } = false;         // true = pass -netlog, write logs/<timestamp>.log
    public bool AllowMismatch { get; init; } = false; // true = launch even if assets.sha256 doesn't match
    public string MapPack { get; init; } = "";        // fallback only; release.json wins
    public string SetupTarget { get; init; } = "";    // first-run setup copies the Steam install here (default C:\Games\ZeroHour-Custom)
}
record Shipped
{
    public string MapPack { get; init; } = "";        // release tag of the map pack both machines install (e.g. "maps-v1")
    public string Args { get; init; } = "";           // game arguments both machines use (e.g. "-win")
}
record Release(string tag_name, Asset[] assets, bool prerelease = false, bool draft = false);
record Asset(string name, string url, long size);
