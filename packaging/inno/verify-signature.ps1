# Перевіряє Authenticode. Шлях лише з -Path, без інтерполяції в -Command.
param(
  [Parameter(Mandatory = $true)][string]$Path
)
$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $Path)) {
  exit 1
}
$signature = Get-AuthenticodeSignature -FilePath $Path
if ($signature.Status -eq "Valid") {
  exit 0
}
exit 1
