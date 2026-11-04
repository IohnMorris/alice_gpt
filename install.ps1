$ErrorActionPreference = 'Stop'
$taskUrl = 'https://raw.githubusercontent.com/IohnMorris/alice_gpt/main/AlisaVoice-Mini2-browser-preview.zip'
$expected = 'f67aa3529cf700002b37d3198ffeb8717cb52b1bf79901df3cbf4c4d077ca4ae'
$base = Join-Path $env:LOCALAPPDATA 'AlisaVoice-Setup'
$folder = Join-Path $base ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $folder -Force | Out-Null
$zip = Join-Path $folder 'setup.zip'
try {
    Write-Host 'Загрузка установочного комплекта…'
    Invoke-WebRequest -UseBasicParsing -Uri $taskUrl -OutFile $zip
    if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw 'Контрольная сумма комплекта не совпала.' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $prefix = [System.IO.Path]::GetFullPath($folder).TrimEnd('\') + '\'
        foreach ($entry in $archive.Entries) {
            $resolved = [System.IO.Path]::GetFullPath((Join-Path $folder $entry.FullName))
            if (-not $resolved.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) { throw 'Некорректный путь в архиве.' }
        }
    } finally { $archive.Dispose() }
    [System.IO.Compression.ZipFile]::ExtractToDirectory($zip, $folder)
    $panel = Join-Path $folder 'AlisaVoice-Browser-Setup'
    $python = Join-Path $folder 'AlisaVoice-Mini2-test\windows\runtime\python.exe'
    if (-not (Test-Path -LiteralPath $python) -or -not (Test-Path -LiteralPath (Join-Path $panel 'server.py'))) { throw 'Комплект неполный.' }
    Write-Host 'Открываем локальный мастер установки…'
    Start-Process -FilePath $python -ArgumentList '-B','-X','utf8','server.py' -WorkingDirectory $panel -WindowStyle Hidden
} catch {
    Write-Error ('Установка не запущена: ' + $_.Exception.Message)
}
