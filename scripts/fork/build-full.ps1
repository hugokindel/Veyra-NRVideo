[CmdletBinding()]
param([string]$Source='D:\Projects\Veyra-NRVideo',[string]$Build='D:\Projects\Veyra-work\build\full',[int]$Jobs=2,
      [string[]]$Targets=@('veyra_qml_ui'),[string]$MagewellSdk='D:\Projects\Veyra-work\deps\magewell-sdk-3.3.1.1596\extracted\app\SDKv3',[string]$UpdatePublicKey='')
# Full-feature Release build: every optional SDK/dependency the official 2.2.1 exe has.
# All inputs live under D:\Projects\Veyra-work\deps (see fetch-sdks.ps1, vcpkg-installed, chiaki-stage, ...).
# Environment changes are for the child cmd only.
$ErrorActionPreference='Stop'
$W='D:\Projects\Veyra-work'; $D="$W\deps"; $S="$D\sdks"; $vi="$D\vcpkg-installed\x64-windows-static"
$vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$py="$D\py-nanopb\Scripts\python.exe"
$cfg=@('-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DVEYRA_ENABLE_D3D12_DEBUG=OFF','-DVEYRA_BUILD_QML_UI=ON',
 "-DCMAKE_PREFIX_PATH=$D\qt\6.8.3\msvc2022_64;$vi",
 "-DVEYRA_FFMPEG_ROOT=$D\ffmpeg-root","-DVEYRA_NVENC_HEADERS_ROOT=$D\nvch\nv-codec-headers-n13.0.19.0",
 "-DOPENSSL_ROOT_DIR=$vi",'-DOPENSSL_USE_STATIC_LIBS=ON',
 '-DVEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON',"-DVEYRA_DLSS_SDK_ROOT=$D\dlss\DLSS-310.7.0",
 "-DVEYRA_NVOF_SDK_ROOT=$D\Optical_Flow_SDK_5.0.7\Optical_Flow_SDK_5.0.7",
 "-DVEYRA_CUDA_DRIVER_INCLUDE_DIR=$D\cuda-cudart-12.8.90\cuda_cudart-windows-x86_64-12.8.90-archive\include",
 "-DVEYRA_XESS_ROOT=$S\intel\xess-3.0.2",
 "-DVEYRA_FIDELITYFX_ROOT=$D\sdk-src\FidelityFX-SDK-1.1.4\sdk",
 "-DVEYRA_FIDELITYFX2_ROOT=$S\amd\FidelityFX-SDK-2.3.0\Kits\FidelityFX",
 "-DVEYRA_AMF_SDK_ROOT=$S\amd\AMF-SDK-1.5.3","-DVEYRA_ORT_DML_ROOT=$S\ort-dml",
 '-DVEYRA_ENABLE_LIBDOVI=ON',"-DVEYRA_LIBDOVI_ROOT=$D\libdovi-3.4.0",
 "-DVEYRA_LIBASS_ROOT=$vi",
 "-DVEYRA_DECKLINK_SDK_DIR=$D\decklink-sdk-win",
 '-DVEYRA_ENABLE_REMOTEPLAY=ON',"-DVEYRA_RP_CHIAKI_SOURCE_DIR=$D\chiaki-stage","-DVEYRA_RP_CHIAKI_VERIFY_DIR=$D\chiaki-ng",
 '-DVEYRA_ENABLE_MOONLIGHT=ON',"-DVEYRA_MOONLIGHT_SOURCE_DIR=$D\moonlight\moonlight-common-c",
 '-DVEYRA_ENABLE_XBOX=ON',
 "-DPKG_CONFIG_EXECUTABLE=$D\vcpkg-installed\x64-windows\tools\pkgconf\pkgconf.exe",
 "-DPython3_EXECUTABLE=$py","-DPYTHON_EXECUTABLE=$py","-DProtobuf_PROTOC_EXECUTABLE=$vi\tools\protobuf\protoc.exe")
if($MagewellSdk){ $cfg+="-DVEYRA_MAGEWELL_SDK_DIR=$MagewellSdk" }
if($UpdatePublicKey){ $cfg+="-DVEYRA_UPDATE_PUBLIC_KEY=$UpdatePublicKey" }
$q=($cfg | % { '"'+$_+'"' }) -join ' '
$bat="$W\tmp\build-full.cmd"
New-Item -ItemType Directory -Force "$W\tmp","$W\logs",$Build | Out-Null
@('@echo off',"call `"$vcvars`" >nul","set TEMP=$W\tmp","set TMP=$W\tmp",
 "set PATH=$vi\tools\protobuf;$D\py-nanopb\Scripts;%PATH%",
 "set LIB=%LIB%;$D\ffmpeg-root\lib",
 "set PKG_CONFIG_PATH=$vi\lib\pkgconfig;$vi\share\pkgconfig",
 "cmake -S `"$Source`" -B `"$Build`" $q",'if not %errorlevel%==0 exit /b 5',
 "cmake --build `"$Build`" --target $(($Targets -join ',').Split(',') -join ' ') -- -j$Jobs",'if not %errorlevel%==0 exit /b 6','exit /b 0') | Set-Content $bat -Encoding ascii
& cmd.exe /c $bat
Write-Host "build-full exit code: $LASTEXITCODE"; exit $LASTEXITCODE
