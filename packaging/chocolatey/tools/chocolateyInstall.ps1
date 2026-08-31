$ErrorActionPreference = 'Stop';
$packageName = 'clipbridge'
$toolsDir = "$(Split-Path -parent $MyInvocation.MyCommand.Definition)"
$url64 = 'https://github.com/riccivr/clipbridge/releases/download/v1.2.1/clipbridge-v1.2.1-windows-x64.zip'
$checksum64 = 'b9ccfa54acba967e4f34addd50436e86311dc516c82d6f1600b607a66583bd00'

$packageArgs = @{
  packageName   = $packageName
  unzipLocation = $toolsDir
  url64bit      = $url64
  checksum64    = $checksum64
  checksumType64= 'sha256'
}

Install-ChocolateyZipPackage @packageArgs
