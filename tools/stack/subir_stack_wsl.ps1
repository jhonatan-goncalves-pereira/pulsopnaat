# Sobe a stack MQTT -> Telegraf -> InfluxDB -> Grafana no Docker Engine do WSL2 (maquinas sem Docker Desktop).
# Pre-requisito unico: distro Ubuntu no WSL com docker.io e docker-compose-v2 (ver README_DOCKER.md).
# Uso, na raiz do repositorio:  powershell -ExecutionPolicy Bypass -File tools\stack\subir_stack_wsl.ps1 [-Online]
# -Online: tambem publica o Grafana num link HTTPS publico (servico tunel, cloudflared) e mostra o link.
# Arquivo mantido em ASCII puro: o Windows PowerShell 5.1 le .ps1 sem BOM como ANSI.
param([switch]$Online)
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

$perfil = @()
if ($Online) { $perfil = @('--profile', 'online') }
wsl -d $distro -u root --cd $repoWsl -- docker compose @perfil up -d
if ($LASTEXITCODE -ne 0) {
    Write-Error "docker compose up falhou em $repoWsl"
    exit 1
}
wsl -d $distro -u root --cd $repoWsl -- docker compose @perfil ps

$porta = (Select-String -Path "$repoWin\.env" -Pattern '^GRAFANA_PORT=(\d+)').Matches | ForEach-Object { $_.Groups[1].Value }
if (-not $porta) { $porta = '3000' }
Write-Host "Grafana: http://localhost:$porta/d/pulsopnaat-monitor  (usuario/senha do .env, padrao admin/admin)"

if ($Online) {
    # O cloudflared imprime o endereco aleatorio alguns segundos depois de conectar
    $link = $null
    for ($i = 0; $i -lt 60 -and -not $link; $i++) {
        $log = wsl -d $distro -u root -- docker logs tunel 2>&1 | Out-String
        $m = [regex]::Matches($log, 'https://[a-z0-9-]+\.trycloudflare\.com')
        if ($m.Count -gt 0) { $link = $m[$m.Count - 1].Value } else { Start-Sleep -Seconds 1 }
    }
    if (-not $link) {
        Write-Error 'tunel nao informou o link - veja: wsl -d Ubuntu -u root -- docker logs tunel'
        exit 1
    }
    Write-Host "Online:  $link/d/pulsopnaat-monitor?kiosk&refresh=5s"
    Write-Host '         (muda a cada reinicio do tunel; sem login se GRAFANA_ANONYMOUS_ENABLED=true no .env)'
}
