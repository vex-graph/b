$ErrorActionPreference = 'Stop'

function Stop-Install([string] $Message) {
    throw "b installer: $Message"
}

$version = if ($env:B_VERSION) { $env:B_VERSION } else { 'latest' }
if ($version -ne 'latest' -and $version -notmatch '^[A-Za-z0-9._+-]+$') {
    Stop-Install 'B_VERSION contains unsupported characters'
}

$architecture = $env:PROCESSOR_ARCHITECTURE
if ($env:PROCESSOR_ARCHITEW6432) { $architecture = $env:PROCESSOR_ARCHITEW6432 }
switch ($architecture.ToUpperInvariant()) {
    'AMD64' { $target = 'windows-x86_64' }
    'ARM64' { $target = 'windows-arm64' }
    default { Stop-Install "unsupported architecture: $architecture" }
}

if ($version -eq 'latest') {
    $defaultBase = 'https://vex-graph.github.io/b/downloads/latest'
} else {
    $tag = if ($version.StartsWith('v')) { $version } else { "v$version" }
    $defaultBase = "https://github.com/vex-graph/b/releases/download/$tag"
}
$base = if ($env:B_DOWNLOAD_BASE_URL) {
    $env:B_DOWNLOAD_BASE_URL.TrimEnd('/')
} else {
    $defaultBase
}

$asset = "b-$target.tar.gz"
$scratch = Join-Path ([IO.Path]::GetTempPath()) ("b-install-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
try {
    $archive = Join-Path $scratch $asset
    $checksumFile = "$archive.sha256"
    & curl.exe --fail --location --silent --show-error --output $archive "$base/$asset"
    if ($LASTEXITCODE -ne 0) { Stop-Install "cannot download $asset" }
    & curl.exe --fail --location --silent --show-error --output $checksumFile "$base/$asset.sha256"
    if ($LASTEXITCODE -ne 0) { Stop-Install "cannot download $asset checksum" }

    $expected = ((Get-Content -Raw $checksumFile) -split '\s+')[0].ToLowerInvariant()
    if ($expected -notmatch '^[0-9a-f]{64}$') { Stop-Install 'checksum file is malformed' }
    $actual = (Get-FileHash -Algorithm SHA256 $archive).Hash.ToLowerInvariant()
    if ($actual -ne $expected) { Stop-Install 'archive checksum does not match' }

    $unpacked = Join-Path $scratch 'unpacked'
    New-Item -ItemType Directory -Path $unpacked | Out-Null
    # GNU tar (MSYS2, Git Bash) reads "C:\..." or "C:/..." as host:path and
    # tries to reach a machine named C. A bare file name carries no colon, so
    # unpack from the scratch directory; bsdtar (System32\tar.exe) agrees.
    Push-Location $scratch
    try {
        & tar.exe -xzf $asset -C $unpacked
    } finally {
        Pop-Location
    }
    if ($LASTEXITCODE -ne 0) { Stop-Install 'cannot unpack the archive' }
    if (-not (Test-Path (Join-Path $unpacked 'b.cmd')) -or
        -not (Test-Path (Join-Path $unpacked 'b-core.exe'))) {
        Stop-Install 'archive is incomplete'
    }

    $installDirectory = if ($env:B_INSTALL_DIR) {
        $env:B_INSTALL_DIR
    } else {
        Join-Path $env:LOCALAPPDATA 'b\bin'
    }
    New-Item -ItemType Directory -Force -Path $installDirectory | Out-Null
    Copy-Item -Force (Join-Path $unpacked 'b.cmd') $installDirectory
    Copy-Item -Force (Join-Path $unpacked 'b-core.exe') $installDirectory

    $stateDirectory = if ($env:B_INSTALL_STATE_DIR) {
        $env:B_INSTALL_STATE_DIR
    } else {
        Join-Path $env:LOCALAPPDATA 'b'
    }
    New-Item -ItemType Directory -Force -Path $stateDirectory | Out-Null
    $record = Join-Path $stateDirectory 'org.vexgraph.b.record'
    $recordText = "application=org.vexgraph.b`nstate=installed`nversion=$version`ntarget=$target`ninstall_dir=$installDirectory`n"
    [IO.File]::WriteAllText($record, $recordText, [Text.UTF8Encoding]::new($false))

    if ($env:B_INSTALL_NO_PATH -ne '1') {
        $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
        $pathEntries = @($userPath -split ';' | Where-Object { $_ })
        if (-not ($pathEntries | Where-Object { $_.TrimEnd('\') -ieq $installDirectory.TrimEnd('\') })) {
            $newPath = (@($pathEntries) + $installDirectory) -join ';'
            [Environment]::SetEnvironmentVariable('Path', $newPath, 'User')
        }
        if (-not (($env:Path -split ';') | Where-Object { $_.TrimEnd('\') -ieq $installDirectory.TrimEnd('\') })) {
            $env:Path = "$installDirectory;$env:Path"
        }
    }

    Write-Output "Installed b for $target in $installDirectory"
    Write-Output 'Run b --help to get started.'
} finally {
    Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue
}
