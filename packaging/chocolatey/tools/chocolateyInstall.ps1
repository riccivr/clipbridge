$ErrorActionPreference = 'Stop';
$packageName = 'clipbridge'
$toolsDir = "$(Split-Path -parent $MyInvocation.MyCommand.Definition)"
$url64 = 'https://github.com/riccivr/clipbridge/releases/download/v1.4.1/clipbridge-v1.4.1-windows-x64.zip'
$checksum64 = 'e4e4a433cf628b5fd3d93088293f418f9ce9d3b8ab7113294ebfc59d59a903b1'

$packageArgs = @{
  packageName   = $packageName
  unzipLocation = $toolsDir
  url64bit      = $url64
  checksum64    = $checksum64
  checksumType64= 'sha256'
}

Install-ChocolateyZipPackage @packageArgs
