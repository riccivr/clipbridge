param (
    [string[]]$FilePaths = @("clipbridge.exe", "clipbridge-portable.exe", "clipbridge-setup.exe")
)

$ErrorActionPreference = 'Stop'

# Find existing riccivr code-signing cert or create one
$cert = Get-ChildItem -Path Cert:\CurrentUser\My -CodeSigningCert | Where-Object { $_.Subject -match "CN=riccivr" } | Select-Object -First 1

if (-not $cert) {
    Write-Host "Generating Authenticode code-signing certificate for riccivr..."
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=riccivr, O=riccivr" -CertStoreLocation "Cert:\CurrentUser\My" -NotAfter (Get-Date).AddYears(5)

    # Trust in CurrentUser TrustedPublisher & Root stores
    $pubStore = New-Object System.Security.Cryptography.X509Certificates.X509Store("TrustedPublisher", "CurrentUser")
    $pubStore.Open("ReadWrite")
    $pubStore.Add($cert)
    $pubStore.Close()

    $rootStore = New-Object System.Security.Cryptography.X509Certificates.X509Store("Root", "CurrentUser")
    $rootStore.Open("ReadWrite")
    $rootStore.Add($cert)
    $rootStore.Close()
}

foreach ($fp in $FilePaths) {
    if (Test-Path $fp) {
        Write-Host "Signing $fp as Publisher: Ricardo Veronese Ricci..."
        $sig = Set-AuthenticodeSignature -FilePath $fp -Certificate $cert -HashAlgorithm SHA256
        Write-Host "Signed $fp (Status: $($sig.Status))"
    }
}
