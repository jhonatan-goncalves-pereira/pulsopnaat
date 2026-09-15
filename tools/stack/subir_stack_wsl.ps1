# Sobe a stack MQTT -> Telegraf -> InfluxDB -> Grafana no Docker Engine do WSL2 (maquinas sem Docker Desktop).
# Pre-requisito unico: distro Ubuntu no WSL com docker.io e docker-compose-v2 (ver README_DOCKER.md).
# Uso, na raiz do repositorio:  powershell -ExecutionPolicy Bypass -File tools\stack\subir_stack_wsl.ps1
# Arquivo mantido em ASCII puro: o Windows PowerShell 5.1 le .ps1 sem BOM como ANSI.
$env:WSL_UTF8 = '1'
$distro = 'Ubuntu'
$repoWin = (Resolve-Path "$PSScriptRoot\..\..").Path
# C:\Users\x -> /mnt/c/Users/x (feito aqui: o wsl passa argumentos por um shell que come as barras invertidas)
$repoWsl = '/mnt/' + $repoWin.Substring(0, 1).ToLower() + ($repoWin.Substring(2) -replace '\\', '/')

if (-not (Test-Path "$repoWin\.env")) {
    Copy-Item "$repoWin\.env.example" "$repoWin\.env"
    Write-Host '.env criado a partir do .env.example'
}

# O WSL desliga a distro quando nenhum processo do Windows a usa; um sleep oculto a mantem de pe.
$keepalive = Get-CimInstance Win32_Process -Filter "Name='wsl.exe'" | Where-Object { $_.CommandLine -like '*sleep infinity*' }
if (-not $keepalive) {
    Start-Process -WindowStyle Hidden -FilePath wsl.exe -ArgumentList '-d', $distro, '-u', 'root', '--', 'sleep', 'infinity'
}

wsl -d $distro -u root -- sh -c 'pgrep -x dockerd >/dev/null || (nohup dockerd >/var/log/dockerd.log 2>&1 &); i=0; until docker info >/dev/null 2>&1; do i=$((i+1)); [ $i -gt 30 ] && exit 1; sleep 1; done'
if ($LASTEXITCODE -ne 0) {
    Write-Error 'dockerd nao subiu - veja /var/log/dockerd.log dentro do WSL'
    exit 1
}

wsl -d $distro -u root --cd $repoWsl -- docker compose up -d
if ($LASTEXITCODE -ne 0) {
    Write-Error "docker compose up falhou em $repoWsl"
    exit 1
}
wsl -d $distro -u root --cd $repoWsl -- docker compose ps

$porta = (Select-String -Path "$repoWin\.env" -Pattern '^GRAFANA_PORT=(\d+)').Matches | ForEach-Object { $_.Groups[1].Value }
if (-not $porta) { $porta = '3000' }
Write-Host "Grafana: http://localhost:$porta/d/pulsopnaat-monitor  (usuario/senha do .env, padrao admin/admin)"
