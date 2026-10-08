# Dev tool: launch latencyforge.exe, capture its window to PNG.
# Usage: powershell -File tools\screenshot.ps1 -Page 4 -Out shots\kernel.png [-Exe path]
param(
    [int]$Page = 0,
    [string]$Out = "shots\home.png",
    [string]$Exe = "$PSScriptRoot\..\build\x64-dev\app\Release\latencyforge.exe",
    [int]$DelayMs = 1500,
    [string]$ExtraArgs = ""   # space-separated, e.g. "--demo --skip-wizard --show preview"
)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
[W]::SetProcessDPIAware() | Out-Null
$exe = (Resolve-Path $Exe).Path
$p = Start-Process -FilePath $exe -ArgumentList (@("--page", $Page) + @($ExtraArgs -split '\s+' | Where-Object { $_ })) -PassThru
Start-Sleep -Milliseconds $DelayMs
$p.Refresh()
[W]::ShowWindow($p.MainWindowHandle, 9) | Out-Null
[W]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 600
$r = New-Object W+RECT
[W]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
$w = $r.R - $r.L; $h = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
# PrintWindow captures only the target window even if another window is in front (PW_RENDERFULLCONTENT=2).
[IntPtr]$hdc = $g.GetHdc()
[W]::PrintWindow($p.MainWindowHandle, $hdc, 2) | Out-Null
$g.ReleaseHdc($hdc)
$dir = Split-Path -Parent (Join-Path (Get-Location) $Out)
New-Item -ItemType Directory -Force $dir | Out-Null
$bmp.Save((Join-Path (Get-Location) $Out), [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Stop-Process -Id $p.Id -Force
"saved $Out (${w}x${h})"
