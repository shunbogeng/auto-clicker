<#
本地免费签名：用自签名证书给 AutoClicker.exe 签名。
作用范围：只在"装过这张证书"的电脑上消掉 SmartScreen 的"未知发布者"提示，
也就是说适合你自己这台机器（或自己团队内部几台机器）。给别人分发仍然要买/申请正规证书。

用法（普通 PowerShell 窗口即可，不需要管理员）：
    pwsh -File packaging\windows\sign_selfsigned.ps1 -ExePath .\dist\AutoClicker\AutoClicker.exe

做的事：
  1. 在当前用户的"个人"证书库找 CN=AutoClicker Dev 的代码签名证书，没有就建一张（有效期 5 年）
  2. 把它导入"受信任的根证书颁发机构"和"受信任的发布者"（当前用户级别）
  3. 用 Windows SDK 里的 signtool 对 exe 签名（SHA-256）
#>
param(
    [Parameter(Mandatory = $true)][string]$ExePath
)

$ErrorActionPreference = 'Stop'

$subject = 'CN=AutoClicker Dev, O=AutoClicker, OU=Local Dev'
$exe = (Resolve-Path -LiteralPath $ExePath).Path

# 1. 证书
$cert = Get-ChildItem Cert:\CurrentUser\My |
        Where-Object { $_.Subject -eq $subject -and $_.HasPrivateKey } |
        Sort-Object NotAfter -Descending | Select-Object -First 1

if (-not $cert) {
    Write-Host "没有现成证书，创建一张自签名代码签名证书（5 年有效）..."
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject `
        -CertStoreLocation Cert:\CurrentUser\My -KeyUsage DigitalSignature `
        -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3') `
        -NotAfter (Get-Date).AddYears(5)
}

$thumb = $cert.Thumbprint
Write-Host "证书指纹：$thumb"

# 2. 让本机信任这张证书（不然签名了也是"未知发布者"）
$cerPath = Join-Path $env:TEMP "autoclicker-dev-$thumb.cer"
Export-Certificate -Cert $cert -FilePath $cerPath -Type CERT | Out-Null
foreach ($store in 'TrustedPublisher', 'Root') {
    $path = "Cert:\CurrentUser\$store"
    $already = Get-ChildItem $path | Where-Object { $_.Thumbprint -eq $thumb }
    if (-not $already) {
        Import-Certificate -FilePath $cerPath -CertStoreLocation $path | Out-Null
        Write-Host "已导入 $store"
    } else {
        Write-Host "$store 里已存在"
    }
}

# 3. 找 signtool（Windows SDK）
$signtool = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\' } | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $signtool) { throw '没找到 signtool.exe，请装 Windows SDK（Visual Studio 安装器勾选 Windows SDK 即可）' }

& $signtool.FullName sign /sha1 $thumb /fd SHA256 /v $exe
if ($LASTEXITCODE -ne 0) { throw "签名失败，signtool 退出码 $LASTEXITCODE" }

$sig = Get-AuthenticodeSignature -LiteralPath $exe
Write-Host ("签名结果：" + $sig.Status + " / " + $sig.SignerCertificate.Subject)
