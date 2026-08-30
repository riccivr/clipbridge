# scripts/build_msix.ps1 - Automated MSIX Packaging & Local Testing Script
Param(
    [string]$PackageDir = "package",
    [string]$OutputFile = "clipbridge-v1.1.0-x64.msix",
    [switch]$InstallLocal
)

$ErrorActionPreference = "Stop"

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host " ClipBridge MSIX Packager (Win32 Bridge) " -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

# 1. Ensure binary payload exists
$exeSource = "clipbridge-portable.exe"
if (-not (Test-Path $exeSource)) {
    Write-Host "Building clipbridge binaries..." -ForegroundColor Yellow
    wsl bash -c "cd /mnt/c/Users/ricci/Desktop/code/clipbridge && make exe"
}

Copy-Item -Path $exeSource -Destination "$PackageDir\clipbridge.exe" -Force
Write-Host "[OK] Staged clipbridge.exe into $PackageDir" -ForegroundColor Green

# 2. Locate MakeAppx.exe
$makeAppx = Get-Command MakeAppx.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
if (-not $makeAppx) {
    $sdkPaths = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\MakeAppx.exe",
        "${env:ProgramFiles}\Windows Kits\10\bin\*\x64\MakeAppx.exe",
        "${env:LOCALAPPDATA}\Microsoft\WindowsApps\MakeAppx.exe"
    )
    $found = Resolve-Path $sdkPaths -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) {
        $makeAppx = $found.Path
    }
}

if (-not $makeAppx) {
    Write-Host ""
    Write-Host "[!] MakeAppx.exe not found on system." -ForegroundColor Yellow
    Write-Host "    You can install the official Windows SDK via Windows Terminal / winget:" -ForegroundColor Cyan
    Write-Host "    winget install Microsoft.WindowsSDK.10.0.22621" -ForegroundColor White
    Write-Host ""
    Write-Host "    Or install 'MSIX Packaging Tool' from Microsoft Store:" -ForegroundColor Cyan
    Write-Host "    ms-windows-store://pdp/?productid=9N5LW3JBCXKF" -ForegroundColor White
    return
}

Write-Host "[OK] Using MakeAppx: $makeAppx" -ForegroundColor Green

# 3. Pack MSIX Container
Write-Host "Packing $OutputFile..." -ForegroundColor Cyan
& $makeAppx pack /d $PackageDir /p $OutputFile /o /v

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "[SUCCESS] Generated $OutputFile" -ForegroundColor Green
    Write-Host "File size: $((Get-Item $OutputFile).Length / 1KB) KB" -ForegroundColor Green
} else {
    Write-Host "[ERROR] Failed to package MSIX" -ForegroundColor Red
    exit 1
}

# 4. Self-Sign for Local Testing if requested
if ($InstallLocal) {
    Write-Host "Signing for local testing..." -ForegroundColor Cyan
    $certName = "CN=Ricardo Veronese Ricci"
    $cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $certName } | Select-Object -First 1
    if (-not $cert) {
        $cert = New-SelfSignedCertificate -Type Custom -Subject $certName `
            -KeyUsage DigitalSignature -FriendlyName "ClipBridge Dev Cert" `
            -CertStoreLocation "Cert:\CurrentUser\My" `
            -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3")
        Export-Certificate -Cert $cert -FilePath "dev_cert.cer" | Out-Null
        Import-Certificate -FilePath "dev_cert.cer" -CertStoreLocation "Cert:\CurrentUser\Root" | Out-Null
    }

    $signtool = Get-Command signtool.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
    if (-not $signtool) {
        $foundSign = Resolve-Path "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($foundSign) { $signtool = $foundSign.Path }
    }

    if ($signtool) {
        & $signtool sign /fd SHA256 /a /sha1 $cert.Thumbprint $OutputFile
        Write-Host "[OK] Signed $OutputFile with local test certificate" -ForegroundColor Green
        Add-AppxPackage -Path $OutputFile
        Write-Host "[OK] Installed $OutputFile locally via Add-AppxPackage!" -ForegroundColor Green
    }
}
