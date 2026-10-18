# Builds the Android app into android\build\zeman.apk, ready to copy to a phone.
#
# Nothing is installed system-wide: a JDK, the Android SDK and the NDK are downloaded into
# android\tools on the first run (about 1.5 GB) and reused after that. No Gradle: the app is one
# native library, compiled with the NDK's clang and packaged with aapt2, zipalign and apksigner.
#
# Usage:  build.bat [-AcceptLicenses] [-Clean]
#   -AcceptLicenses  Accept the Android SDK licenses without showing them. Without it, the first
#                    run shows each license and asks.
#   -Clean           Rebuild raylib too, not only the app.
param(
    [switch]$AcceptLicenses,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
# Invoke-WebRequest is many times slower with its progress bar in Windows PowerShell 5.1.
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$AndroidDir = $PSScriptRoot
$RootDir = Split-Path -Parent $AndroidDir
$ToolsDir = Join-Path $AndroidDir 'tools'
$BuildDir = Join-Path $AndroidDir 'build'
$JdkDir = Join-Path $ToolsDir 'jdk'
$SdkDir = Join-Path $ToolsDir 'sdk'
$ExternalDir = Join-Path $RootDir 'external'

# Pinned versions, so that every build uses the same tools.
$JdkUrl = 'https://api.adoptium.net/v3/binary/latest/21/ga/windows/x64/jdk/hotspot/normal/eclipse'
$CommandLineToolsUrl = 'https://dl.google.com/android/repository/commandlinetools-win-13114758_latest.zip'
$BuildToolsVersion = '35.0.0'
$PlatformVersion = '35'
$NdkVersion = '27.2.12479018'
$MinSdkVersion = 24
$Abi = 'arm64-v8a'
$ClangTarget = "aarch64-linux-android$MinSdkVersion"
# A key made once for this project and kept, because Android only installs an update of an app
# signed with the same key as the installed one.
$KeystorePath = Join-Path $AndroidDir 'keystore\zeman.keystore'
$KeystorePassword = 'android'
$KeyAlias = 'zeman'

function Invoke-Tool([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$(Split-Path -Leaf $Program) failed with exit code $LASTEXITCODE."
    }
}

# Downloads a zip and moves its single top folder, or with -KeepRoot its whole content, to
# $Destination.
function Get-Archive([string]$Url, [string]$Destination, [string]$Name, [switch]$KeepRoot) {
    $archive = Join-Path $ToolsDir "$Name.zip"
    $extract = Join-Path $ToolsDir "$Name-extract"
    Write-Host "Downloading $Name..."
    New-Item -ItemType Directory -Force $ToolsDir | Out-Null
    Invoke-WebRequest -Uri $Url -OutFile $archive -UseBasicParsing
    if (Test-Path $extract) { Remove-Item -Recurse -Force $extract }
    Expand-Archive -Path $archive -DestinationPath $extract
    New-Item -ItemType Directory -Force (Split-Path -Parent $Destination) | Out-Null
    if ($KeepRoot) {
        Move-Directory $extract $Destination
    } else {
        $top = Get-ChildItem $extract | Select-Object -First 1
        Move-Directory $top.FullName $Destination
        Remove-Item -Recurse -Force $extract
    }
    Remove-Item -Force $archive
}

# Antivirus software often holds files that were just extracted for a few seconds, which makes
# moving their folder fail with "access denied"; so the move is retried for a while.
function Move-Directory([string]$Source, [string]$Destination) {
    for ($attempt = 1; $attempt -le 30; $attempt++) {
        try {
            Move-Item $Source $Destination -ErrorAction Stop
            return
        } catch {
            if ($attempt -eq 30) { throw }
            Start-Sleep -Seconds 2
        }
    }
}

function Install-Tools {
    if (-not (Test-Path (Join-Path $JdkDir 'bin\java.exe'))) {
        Get-Archive $JdkUrl $JdkDir 'jdk'
    }
    $sdkManager = Join-Path $SdkDir 'cmdline-tools\latest\bin\sdkmanager.bat'
    if (-not (Test-Path $sdkManager)) {
        # The zip holds cmdline-tools\, which sdkmanager expects at cmdline-tools\latest.
        Get-Archive $CommandLineToolsUrl (Join-Path $SdkDir 'cmdline-tools\latest') 'cmdline-tools'
    }
    # For this process only: sdkmanager, keytool and apksigner need Java.
    $env:JAVA_HOME = $JdkDir
    $env:PATH = "$(Join-Path $JdkDir 'bin');$env:PATH"

    $packages = @("build-tools;$BuildToolsVersion", "platforms;android-$PlatformVersion", "ndk;$NdkVersion")
    $missing = @(
        (Join-Path $SdkDir "build-tools\$BuildToolsVersion"),
        (Join-Path $SdkDir "platforms\android-$PlatformVersion"),
        (Join-Path $SdkDir "ndk\$NdkVersion")
    ) | Where-Object { -not (Test-Path $_) }
    if ($missing.Count -eq 0) { return }

    Write-Host 'The Android SDK needs its licenses accepted before it downloads.'
    if ($AcceptLicenses) {
        # One "y" per license question, fewer than 32, fed through cmd's redirection: answers
        # piped from PowerShell do not reliably reach a batch file's input.
        $answers = Join-Path $ToolsDir 'license-answers.txt'
        Set-Content -Path $answers -Value (@('y') * 32) -Encoding ascii
        & cmd /c "`"$sdkManager`" --sdk_root=`"$SdkDir`" --licenses < `"$answers`"" | Out-Null
        Remove-Item -Force $answers
    } else {
        & $sdkManager "--sdk_root=$SdkDir" --licenses
    }
    Write-Host 'Downloading the Android SDK platform, build tools and NDK...'
    Invoke-Tool $sdkManager (@("--sdk_root=$SdkDir") + $packages)
    # sdkmanager exits successfully even when it skipped packages whose licenses were declined.
    $stillMissing = $missing | Where-Object { -not (Test-Path $_) }
    if ($stillMissing) {
        throw "Android SDK packages were not installed (were the licenses accepted?): $($stillMissing -join ', ')"
    }
}

function Get-SourceFlags {
    $ndk = Join-Path $SdkDir "ndk\$NdkVersion"
    return @(
        "--target=$ClangTarget", '-std=gnu11', '-O2', '-fPIC',
        '-ffunction-sections', '-fdata-sections',
        '-DPLATFORM_ANDROID', '-DGRAPHICS_API_OPENGL_ES2',
        "-I$(Join-Path $ExternalDir 'raylib\src')",
        "-I$(Join-Path $ExternalDir 'raygui\src')",
        "-I$(Join-Path $ExternalDir 'quirc\lib')",
        "-I$(Join-Path $ndk 'sources\android\native_app_glue')",
        # Our own headers are included as <zeman/app.h>, as in the desktop build's CMake.
        "-I$(Join-Path $RootDir 'shared\include')",
        "-I$(Join-Path $AndroidDir 'include')"
    )
}

function Build-Raylib([string]$Clang, [string]$Archiver) {
    $library = Join-Path $BuildDir 'libraylib.a'
    if ((Test-Path $library) -and -not $Clean) { return $library }
    Write-Host 'Compiling raylib for Android...'
    $objectDir = Join-Path $BuildDir 'obj\raylib'
    New-Item -ItemType Directory -Force $objectDir | Out-Null
    $ndk = Join-Path $SdkDir "ndk\$NdkVersion"
    $sources = @('rcore', 'rshapes', 'rtextures', 'rtext', 'rmodels', 'utils', 'raudio') |
        ForEach-Object { Join-Path $ExternalDir "raylib\src\$_.c" }
    $sources += Join-Path $ndk 'sources\android\native_app_glue\android_native_app_glue.c'
    $objects = @()
    foreach ($source in $sources) {
        $object = Join-Path $objectDir ([IO.Path]::GetFileNameWithoutExtension($source) + '.o')
        # Third-party code: its warnings are not ours to fix.
        Invoke-Tool $Clang ((Get-SourceFlags) + @('-w', '-c', $source, '-o', $object))
        $objects += $object
    }
    if (Test-Path $library) { Remove-Item -Force $library }
    Invoke-Tool $Archiver (@('rcs', $library) + $objects)
    return $library
}

function Build-Library([string]$Clang, [string]$RaylibLibrary) {
    Write-Host 'Compiling the app...'
    $objectDir = Join-Path $BuildDir 'obj\app'
    New-Item -ItemType Directory -Force $objectDir | Out-Null
    $shared = @('app', 'calendar', 'client', 'discovery', 'event', 'net', 'protocol') |
        ForEach-Object { Join-Path $RootDir "shared\src\$_.c" }
    $own = @('main', 'android_input', 'android_permission', 'qr_scanner') |
        ForEach-Object { Join-Path $AndroidDir "src\$_.c" }
    $objects = @()
    foreach ($source in ($shared + $own)) {
        $object = Join-Path $objectDir ([IO.Path]::GetFileNameWithoutExtension($source) + '.o')
        # As on the desktop: every warning is an error, and assertions stay on.
        $flags = (Get-SourceFlags) + @('-Wall', '-Wextra', '-Werror', '-UNDEBUG')
        Invoke-Tool $Clang ($flags + @('-c', $source, '-o', $object))
        $objects += $object
    }
    # Third-party code: raygui's implementation and the quirc QR decoder.
    $thirdParty = @(Join-Path $AndroidDir 'src\raygui_impl.c')
    $thirdParty += @('quirc', 'decode', 'identify', 'version_db') |
        ForEach-Object { Join-Path $ExternalDir "quirc\lib\$_.c" }
    foreach ($source in $thirdParty) {
        $object = Join-Path $objectDir ([IO.Path]::GetFileNameWithoutExtension($source) + '.o')
        Invoke-Tool $Clang ((Get-SourceFlags) + @('-w', '-c', $source, '-o', $object))
        $objects += $object
    }

    $libraryDir = Join-Path $BuildDir "apk\lib\$Abi"
    New-Item -ItemType Directory -Force $libraryDir | Out-Null
    $library = Join-Path $libraryDir 'libmain.so'
    # ANativeActivity_onCreate lives in the glue code and is only referenced by Android, so it
    # must be kept explicitly. 16 KiB pages, which newer Android versions require.
    $linkFlags = @(
        "--target=$ClangTarget", '-shared', '-o', $library,
        '-u', 'ANativeActivity_onCreate',
        '-Wl,--gc-sections', '-Wl,--no-undefined', '-Wl,-z,max-page-size=16384'
    )
    # camera2ndk and mediandk: the native camera, for scanning QR codes.
    $libraries = @('-llog', '-landroid', '-lEGL', '-lGLESv2', '-lOpenSLES', '-lcamera2ndk',
                   '-lmediandk', '-lm', '-ldl')
    Invoke-Tool $Clang ($linkFlags + $objects + @($RaylibLibrary) + $libraries)
    return $library
}

# Draws the launcher icon, one PNG per screen density, from the same artwork as the desktop app
# (assets\zeman-app.png), so that there is one picture to change. Android's launcher sizes:
# 48 dp at 1x (mdpi) up to 4x (xxxhdpi). Returns the folder that aapt2 compiles resources from.
function New-LauncherIcons() {
    Add-Type -AssemblyName System.Drawing
    $resources = Join-Path $BuildDir 'apk\res'
    if (Test-Path $resources) { Remove-Item -Recurse -Force $resources }
    $densities = [ordered]@{ mdpi = 48; hdpi = 72; xhdpi = 96; xxhdpi = 144; xxxhdpi = 192 }
    $source = [Drawing.Image]::FromFile((Join-Path $RootDir 'assets\zeman-app.png'))
    try {
        foreach ($density in $densities.Keys) {
            $size = $densities[$density]
            $folder = Join-Path $resources "mipmap-$density"
            New-Item -ItemType Directory -Force $folder | Out-Null
            $bitmap = New-Object Drawing.Bitmap $size, $size, ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            try {
                # Smooth scaling that keeps the transparent corners.
                $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::HighQuality
                $graphics.DrawImage($source, 0, 0, $size, $size)
            } finally {
                $graphics.Dispose()
            }
            $bitmap.Save((Join-Path $folder 'ic_launcher.png'), [Drawing.Imaging.ImageFormat]::Png)
            $bitmap.Dispose()
        }
    } finally {
        $source.Dispose()
    }
    return $resources
}

function Build-Apk([string]$NativeLibrary) {
    Write-Host 'Packaging the APK...'
    $buildTools = Join-Path $SdkDir "build-tools\$BuildToolsVersion"
    $androidJar = Join-Path $SdkDir "platforms\android-$PlatformVersion\android.jar"
    $assets = Join-Path $BuildDir 'apk\assets\fonts'
    New-Item -ItemType Directory -Force $assets | Out-Null
    Copy-Item (Join-Path $ExternalDir 'fonts\*') $assets -Force

    $unaligned = Join-Path $BuildDir 'zeman.unaligned.apk'
    $aligned = Join-Path $BuildDir 'zeman.aligned.apk'
    $final = Join-Path $BuildDir 'zeman.apk'
    foreach ($file in @($unaligned, $aligned, $final)) {
        if (Test-Path $file) { Remove-Item -Force $file }
    }
    $compiledIcons = Join-Path $BuildDir 'apk\icons.zip'
    Invoke-Tool (Join-Path $buildTools 'aapt2.exe') @(
        'compile', '--dir', (New-LauncherIcons), '-o', $compiledIcons
    )
    Invoke-Tool (Join-Path $buildTools 'aapt2.exe') @(
        'link', '-o', $unaligned,
        '--manifest', (Join-Path $AndroidDir 'AndroidManifest.xml'),
        '-I', $androidJar,
        '-R', $compiledIcons,
        '-A', (Join-Path $BuildDir 'apk\assets'),
        '--min-sdk-version', "$MinSdkVersion",
        '--target-sdk-version', $PlatformVersion
    )
    # aapt2 packages resources only, so the native library is added to the zip here.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::Open($unaligned, 'Update')
    try {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $zip, $NativeLibrary, "lib/$Abi/libmain.so") | Out-Null
    } finally {
        $zip.Dispose()
    }
    Invoke-Tool (Join-Path $buildTools 'zipalign.exe') @('-p', '-f', '4', $unaligned, $aligned)

    if (-not (Test-Path $KeystorePath)) {
        Write-Host 'Creating the signing key, once...'
        New-Item -ItemType Directory -Force (Split-Path -Parent $KeystorePath) | Out-Null
        Invoke-Tool (Join-Path $JdkDir 'bin\keytool.exe') @(
            '-genkeypair', '-keystore', $KeystorePath, '-alias', $KeyAlias,
            '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000',
            '-storepass', $KeystorePassword, '-keypass', $KeystorePassword,
            '-dname', 'CN=Zeman'
        )
    }
    $apkSigner = Join-Path $buildTools 'apksigner.bat'
    Invoke-Tool $apkSigner @(
        'sign', '--ks', $KeystorePath, '--ks-key-alias', $KeyAlias,
        '--ks-pass', "pass:$KeystorePassword", '--key-pass', "pass:$KeystorePassword",
        '--out', $final, $aligned
    )
    Invoke-Tool $apkSigner @('verify', $final)
    Remove-Item -Force $unaligned, $aligned
    return $final
}

Install-Tools
# raylib, raygui and the fonts, shared with the desktop build. Needs CMake, as that build does.
Invoke-Tool 'cmake' @('-P', (Join-Path $RootDir 'cmake\dependencies.cmake'))

$llvm = Join-Path $SdkDir "ndk\$NdkVersion\toolchains\llvm\prebuilt\windows-x86_64\bin"
$clang = Join-Path $llvm 'clang.exe'
$archiver = Join-Path $llvm 'llvm-ar.exe'
New-Item -ItemType Directory -Force $BuildDir | Out-Null
$raylib = Build-Raylib $clang $archiver
$native = Build-Library $clang $raylib
$apk = Build-Apk $native
Write-Host ''
Write-Host "Built $apk"
Write-Host 'Copy it to the phone and open it there to install (allow installing unknown apps).'
