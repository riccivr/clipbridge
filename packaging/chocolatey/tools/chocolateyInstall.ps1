$ErrorActionPreference = 'Stop';
$packageName = 'clipbridge'
$toolsDir = "$(Split-Path -parent $MyInvocation.MyCommand.Definition)"
$url64 = 'https://github.com/riccivr/clipbridge/releases/download/v1.4.1/clipbridge-v1.4.1-windows-x64.zip'
$checksum64 = 'a1da6c32f07466077db05b8e9865a67929569745d372316e47ae4d06892e8dba1'

$packageArgs = @{
  packageName   = $packageName
  unzipLocation = $toolsDir
  url64bit      = $url64
  checksum64    = $checksum64
  checksumType64= 'sha256'
}

Install-ChocolateyZipPackage @packageArgs
