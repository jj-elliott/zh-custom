// ZHLauncher — one double-click: fetch newest build, swap it in, start the game.
// Lives inside the game folder next to generalszh.exe. Reads launcher.json beside it.
using System.Diagnostics;
using System.IO.Compression;
using System.Net.Http.Headers;
using System.Security.Cryptography;
using System.Text.Json;

var dir = AppContext.BaseDirectory;
var cfgPath = Path.Combine(dir, "launcher.json");
var versionPath = Path.Combine(dir, "BUILD_VERSION.txt");

var cfg = File.Exists(cfgPath)
    ? JsonSerializer.Deserialize<Config>(File.ReadAllText(cfgPath)) ?? new Config()
    : new Config();

string? localTag = File.Exists(versionPath) ? File.ReadAllText(versionPath).Split(' ')[0].Trim() : null;
Log($"Installed build: {localTag ?? "(none)"}");

try
{
    using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(20) };
    http.DefaultRequestHeaders.UserAgent.ParseAdd("ZHLauncher/1.0");
    http.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
    if (!string.IsNullOrWhiteSpace(cfg.Token))
        http.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", cfg.Token);

    var rel = await http.GetFromJsonAsync<Release>($"https://api.github.com/repos/{cfg.Repo}/releases/latest");
    if (rel is null) throw new Exception("no release returned");
    Log($"Latest release:  {rel.tag_name}");

    if (rel.tag_name != localTag)
    {
        var asset = rel.assets.FirstOrDefault(a => a.name.StartsWith("zh-game-", StringComparison.OrdinalIgnoreCase))
                    ?? throw new Exception("release has no zh-game-*.zip asset");

        Log($"Downloading {asset.name} ({asset.size / 1024 / 1024} MB)...");
        // Private repos must use the API asset URL with Accept: application/octet-stream
        using var req = new HttpRequestMessage(HttpMethod.Get, asset.url);
        req.Headers.Accept.Clear();
        req.Headers.Accept.ParseAdd("application/octet-stream");
        using var resp = await http.SendAsync(req, HttpCompletionOption.ResponseHeadersRead);
        resp.EnsureSuccessStatusCode();

        var tmp = Path.Combine(Path.GetTempPath(), asset.name);
        await using (var fs = File.Create(tmp)) await resp.Content.CopyToAsync(fs);

        // Keep one backup of the previous exe in case a build is broken
        var exe = Path.Combine(dir, cfg.Executable);
        if (File.Exists(exe)) File.Copy(exe, exe + ".prev", overwrite: true);

        ZipFile.ExtractToDirectory(tmp, dir, overwriteFiles: true);
        File.Delete(tmp);
        Log($"Updated to {rel.tag_name}");
    }
    else Log("Already up to date.");
}
catch (Exception ex)
{
    // Offline, token expired, GitHub down: just play whatever is installed.
    Log($"Update check skipped: {ex.Message}");
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

// Surface the last crash dump so it gets sent instead of forgotten.
var dumps = Path.Combine(dir, "crashdumps");
Directory.CreateDirectory(dumps);
var lastDump = new DirectoryInfo(dumps).GetFiles("*.dmp").OrderByDescending(f => f.LastWriteTime).FirstOrDefault();
if (lastDump is not null && lastDump.LastWriteTime > DateTime.Now.AddDays(-2))
    Log($"NOTE: crash dump from last session at {lastDump.FullName}. Send it along with BUILD_VERSION.txt.");

if (problems.Count > 0)
{
    foreach (var p in problems) Log("  ! " + p);
    Log(cfg.AllowMismatch ? "Launching anyway (AllowMismatch=true); expect desyncs." : "Not launching. Fix the above or set AllowMismatch in launcher.json. Press any key.");
    if (!cfg.AllowMismatch) { Console.ReadKey(); return 2; }
}

// ---- Launch ----------------------------------------------------------------
var args = cfg.Args;
if (cfg.Debug)
{
    Directory.CreateDirectory(Path.Combine(dir, "logs"));
    args += $" -netlog \"logs/{DateTime.Now:yyyyMMdd-HHmmss}.log\"";   // flag added in Phase 4 instrumentation
}

Log($"Launching {cfg.Executable} {args}");
Process.Start(new ProcessStartInfo(target, args) { WorkingDirectory = dir, UseShellExecute = true });
return 0;

static void Log(string s) => Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] {s}");

record Config
{
    public string Repo { get; init; } = "YOUR_GITHUB_USER/zh-custom";
    public string Token { get; init; } = "";          // fine-grained PAT, read-only on this repo's Contents
    public string Executable { get; init; } = "generalszh.exe";
    public string Args { get; init; } = "-win";       // e.g. "-win -xres 2560 -yres 1440"
    public bool Debug { get; init; } = false;         // true = pass -netlog, write logs/<timestamp>.log
    public bool AllowMismatch { get; init; } = false; // true = launch even if assets.sha256 doesn't match
}
record Release(string tag_name, Asset[] assets);
record Asset(string name, string url, long size);
