$ErrorActionPreference = 'Stop';
$packageName = 'clipbridge'
$toolsDir = "$(Split-Path -parent $MyInvocation.MyCommand.Definition)"
$url64 = 'https://github.com/riccivr/clipbridge/releases/download/v1.5.0/clipbridge-v1.5.0-windows-x64.zip'
$checksum64 = '9bb7af1c29355a4b98807326df32e9b71661953b9209bdef96839521339e10e5'

$packageArgs = @{
  packageName   = $packageName
  unzipLocation = $toolsDir
  url64bit      = $url64
  checksum64    = $checksum64
  checksumType64= 'sha256'
}

Install-ChocolateyZipPackage @packageArgs
