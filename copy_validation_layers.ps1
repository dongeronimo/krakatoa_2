param(
    [string]$ExtractedPath = "$env:TEMP\vulkan-layers\arm64-v8a"
)

Write-Host "Instalando Vulkan Validation Layers no dispositivo..." -ForegroundColor Cyan

# Verificar se a pasta existe
if (-not (Test-Path $ExtractedPath)) {
    Write-Host "Erro: Pasta nao encontrada: $ExtractedPath" -ForegroundColor Red
    exit 1
}

Set-Location $ExtractedPath

# Encontrar todas as .so
$libraries = Get-ChildItem -Filter "libVkLayer*.so"

if ($libraries.Count -eq 0) {
    Write-Host "Erro: Nenhuma library .so encontrada" -ForegroundColor Red
    exit 1
}

Write-Host "Encontradas $($libraries.Count) libraries:" -ForegroundColor Green
$libraries | ForEach-Object { Write-Host "  - $($_.Name)" }

# Push cada .so
Write-Host "`nCopiando libraries para /data/local/tmp/..." -ForegroundColor Cyan
foreach ($lib in $libraries) {
    Write-Host "  Pushing $($lib.Name)..." -ForegroundColor Gray
    adb push $lib.FullName /data/local/tmp/
}

# Push JSONs tambem
$jsons = Get-ChildItem -Filter "*.json"
foreach ($json in $jsons) {
    Write-Host "  Pushing $($json.Name)..." -ForegroundColor Gray
    adb push $json.FullName /data/local/tmp/
}

# Setar propriedades
Write-Host "`nConfigurando propriedades do sistema..." -ForegroundColor Cyan
adb shell setprop debug.vulkan.layers VK_LAYER_KHRONOS_validation
adb shell setprop debug.vulkan.layer.path /data/local/tmp

# Verificar
Write-Host "`nVerificando instalacao..." -ForegroundColor Cyan
Write-Host "Propriedades do sistema:" -ForegroundColor Yellow
adb shell getprop | Select-String vulkan

Write-Host "`nArquivos no device:" -ForegroundColor Yellow
adb shell ls -la /data/local/tmp/libVkLayer*

Write-Host "`n[OK] Instalacao completa!" -ForegroundColor Green
Write-Host "Reinicie seu app para ativar as validation layers." -ForegroundColor Yellow