# Stages public SDK headers for the full-feature Veyra build under D:\Projects\Veyra-work\deps\sdks.
# Same repositories and commits as the repo's scripts/ci/fetch-sdks.ps1, plus the RIFE NuGet packages
# (versions from docs/RIFE_DIRECTML.md). Headers/import data only; no runtime DLL is shipped from here.
$ErrorActionPreference = 'Stop'
$W = 'D:\Projects\Veyra-work'; $S = "$W\deps\sdks"; $src = "$W\deps\sdk-src"
New-Item -ItemType Directory -Force $S, $src | Out-Null
$pins = @(
    @{ Repo = 'intel/xess';                              Commit = '8fe81bdbbaf00b3c1b733fd0d830c333dc84e6f0' },
    @{ Repo = 'GPUOpen-LibrariesAndSDKs/FidelityFX-SDK'; Commit = '7539ae8b6568ad29ed78a753404351daf3b5cca7' },
    @{ Repo = 'GPUOpen-LibrariesAndSDKs/AMF';            Commit = '57ee0cfb0e1a08d9041f77435653794c1b012be0' },
    @{ Repo = 'quietvoid/dovi_tool';                     Commit = 'd1abe0e27ff2c7ab3339614d06db9f8a058af6b2' }
)
function Invoke-Git { & git @args; if ($LASTEXITCODE -ne 0) { throw "git $args failed" } }
foreach ($p in $pins) {
    $dir = Join-Path $src (($p.Repo -split '/')[1])
    if (Test-Path "$dir\.git") {
        $head = (& git -C $dir rev-parse HEAD).Trim()
        if ($head -eq $p.Commit) { Write-Host "ok $($p.Repo)"; continue }
        Remove-Item $dir -Recurse -Force
    }
    Write-Host "fetch $($p.Repo) @ $($p.Commit)"
    Invoke-Git init -q $dir
    Invoke-Git -C $dir remote add origin "https://github.com/$($p.Repo).git"
    Invoke-Git -C $dir fetch -q --depth 1 origin $p.Commit
    Invoke-Git -C $dir -c advice.detachedHead=false -c core.autocrlf=false checkout -q FETCH_HEAD
    if ((& git -C $dir rev-parse HEAD).Trim() -ne $p.Commit) { throw "$($p.Repo) pin mismatch" }
}
function Copy-Tree($From, $To) { New-Item -ItemType Directory -Force $To | Out-Null; Copy-Item "$From\*" $To -Recurse -Force }
Copy-Tree "$src\xess\inc" "$S\intel\xess-3.0.2\inc"
foreach ($sub in 'api', 'framegeneration', 'upscalers') {
    Copy-Tree "$src\FidelityFX-SDK\Kits\FidelityFX\$sub\include" "$S\amd\FidelityFX-SDK-2.4.0\Kits\FidelityFX\$sub\include"
}
Copy-Tree "$src\AMF\amf\public\include" "$S\amd\AMF-SDK-1.5.3\include"

# DLSS SDK and nv-codec-headers: GitHub source archives at the pinned commits, unpacked under deps\.
$archives = @(
    @{ Repo = 'NVIDIA/DLSS';             Commit = '9981c6350dc58c0ce21f024fd65e31e851c31689'; Zip = 'dlss-310.9.16.zip'; Out = "$W\deps\dlss\DLSS-310.9.16" },
    @{ Repo = 'FFmpeg/nv-codec-headers'; Commit = '88fee5c37318c991a8762d423530f91681e32e3a'; Zip = 'nvch-n13.0.19.1.zip'; Out = "$W\deps\nvch\nv-codec-headers-n13.0.19.1" }
)
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach ($a in $archives) {
    if (Test-Path "$($a.Out)\include") { Write-Host "ok $($a.Out)"; continue }
    $zip = "$W\deps\downloads\$($a.Zip)"
    if (-not (Test-Path $zip)) {
        Invoke-WebRequest "https://codeload.github.com/$($a.Repo)/zip/$($a.Commit)" -OutFile $zip -UseBasicParsing
    }
    $tmp = "$W\tmp\unzip-$($a.Zip)"; if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
    [IO.Compression.ZipFile]::ExtractToDirectory($zip, $tmp)
    $root = Get-ChildItem $tmp -Directory | Select-Object -First 1
    New-Item -ItemType Directory -Force (Split-Path $a.Out) | Out-Null
    Move-Item $root.FullName $a.Out; Remove-Item $tmp -Recurse -Force
    Write-Host ("{0} @ {1} sha256={2}" -f $a.Repo, $a.Commit, (Get-FileHash $zip).Hash.ToLower())
}

# RIFE: ONNX Runtime DirectML 1.24.4 and DirectML 1.15.4 NuGet packages (headers for the build).
$ort = "$S\ort-dml"
foreach ($n in @(@{Id='microsoft.ml.onnxruntime.directml';Ver='1.24.4';Dir='ort'}, @{Id='microsoft.ai.directml';Ver='1.15.4';Dir='dml'})) {
    $pkg = "$W\downloads\$($n.Id).$($n.Ver).nupkg"
    if (-not (Test-Path $pkg)) {
        Invoke-WebRequest "https://api.nuget.org/v3-flatcontainer/$($n.Id)/$($n.Ver)/$($n.Id).$($n.Ver).nupkg" -OutFile $pkg -UseBasicParsing
    }
    $out = "$ort\$($n.Dir)"; if (Test-Path $out) { Remove-Item $out -Recurse -Force }
    [IO.Compression.ZipFile]::ExtractToDirectory($pkg, $out)
    Write-Host ("{0} {1} sha256={2}" -f $n.Id, $n.Ver, (Get-FileHash $pkg).Hash.ToLower())
}
Write-Host "staged in $S"
