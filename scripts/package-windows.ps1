param(
    [string]$BuildDir = "build",
    [Parameter(Mandatory=$true)][string]$QtDir
)
$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildPath = Join-Path $RepoRoot $BuildDir
$Exe = Join-Path $BuildPath "app/Xinqing.exe"
if (-not (Test-Path $Exe)) { $Exe = Join-Path $BuildPath "app/Release/Xinqing.exe" }
if (-not (Test-Path $Exe)) { throw "Build Xinqing in Release mode first." }
$DeployTool = Join-Path $QtDir "bin/windeployqt.exe"
if (-not (Test-Path $DeployTool)) { throw "QtDir must contain bin/windeployqt.exe." }
$Dist = Join-Path $RepoRoot "dist"
$Package = Join-Path $Dist "Xinqing-Windows-x64"
if (Test-Path $Package) { throw "Remove or rename the previous dist/Xinqing-Windows-x64 folder before packaging." }
New-Item -ItemType Directory -Force $Package | Out-Null
Copy-Item $Exe (Join-Path $Package "Xinqing.exe")
& $DeployTool --release --compiler-runtime --no-translations --dir $Package (Join-Path $Package "Xinqing.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed." }
Copy-Item (Join-Path $RepoRoot "THIRD_PARTY_NOTICES.md") $Package
Copy-Item (Join-Path $RepoRoot "docs/quickstart-windows.txt") (Join-Path $Package "使用说明.txt")
$Licenses = Join-Path $Package "licenses/Qt"
New-Item -ItemType Directory -Force $Licenses | Out-Null
# Carry Qt's license texts and third-party notices, keeping their source paths.
$SourceZip = Join-Path ([System.IO.Path]::GetTempPath()) "xinqing-qtbase-src-6.8.3.zip"
if (-not (Test-Path $SourceZip)) {
    Invoke-WebRequest -Uri "https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.zip" -OutFile $SourceZip
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Archive = [System.IO.Compression.ZipFile]::OpenRead($SourceZip)
try {
    foreach ($Entry in $Archive.Entries) {
        if (-not $Entry.Name) { continue }
        $Relative = $Entry.FullName -replace '^[^/]+/', ''
        if ($Relative -match '^LICENSES/' -or $Relative -match '(^|/)3rdparty/.*(LICENSE|COPYING|COPYRIGHT|NOTICE|qt_attribution\.json)') {
            $Target = Join-Path $Licenses $Relative
            New-Item -ItemType Directory -Force (Split-Path -Parent $Target) | Out-Null
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($Entry, $Target, $true)
        }
    }
} finally { $Archive.Dispose() }
if (-not (Test-Path (Join-Path $Licenses "LICENSES/LGPL-3.0-only.txt"))) { throw "Qt license extraction failed." }
if (Test-Path (Join-Path $QtDir "sbom")) { Copy-Item -Recurse (Join-Path $QtDir "sbom") $Licenses }
$Zip = Join-Path $Dist "Xinqing-Windows-x64.zip"
if (Test-Path $Zip) { throw "Package archive already exists; rename or remove it before packaging." }
Compress-Archive -Path $Package -DestinationPath $Zip
$Hash = (Get-FileHash $Zip -Algorithm SHA256).Hash.ToLowerInvariant()
"$Hash  Xinqing-Windows-x64.zip" | Set-Content -Encoding ascii (Join-Path $Dist "SHA256SUMS.txt")
Write-Output "Created $Zip"
