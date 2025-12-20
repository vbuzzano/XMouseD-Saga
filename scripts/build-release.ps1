# build-release.ps1
# Usage: pwsh /build-release.ps1 -Path <pattern|dir|file> [-Recurse] [-Prefix PROGRAM] [-OutputDir <dir>] [-Force]

# check for setup script
$Setup = "$pwd\setup.ps1"
if (!(Test-Path $Setup)) {
    $Setup = "$pwd\scripts\setup.ps1"
    if (!(Test-Path $Setup)) {
        throw "setup.ps1 introuvable dans les dossiers scripts ou racine."
    }
}

# check for env-replace script
$EnvReplace = "$pwd\env-replace.ps1"
if (!(Test-Path $EnvReplace)) {
    $EnvReplace = "$pwd\scripts\env-replace.ps1"
    if (!(Test-Path $EnvReplace)) {
        throw "env-replace.ps1 introuvable dans les dossiers scripts ou racine."
    }
}

# update env
. $Setup env update

# Load .env file into environment
Get-Content .env | ForEach-Object {
    if ($_ -match '^\s*([^#=]+?)\s*=\s*(.+?)\s*$') {
        $name = $matches[1]
        $value = $matches[2]
        # Supprimer les guillemets si présents
        $value = $value -replace '^["'']|["'']$', ''
        # Définir comme variable d'environnement
        [Environment]::SetEnvironmentVariable($name, $value, 'Process')
        # Ou créer une variable dans le scope actuel
        Set-Variable -Name $name -Value $value -Scope Script
    }
}

$escapedVersion = $env:PROGRAM_VERSION -replace '[^A-Za-z0-9._-]', '_'
$ReleaseDir = "$env:PROGRAM_NAME-$escapedVersion"

# create release directory
if ((Test-Path $env:DIST_DIR)) {
    Remove-item -Recurse -Force $env:DIST_DIR -ErrorAction Stop
}
New-Item -ItemType Directory -Path "$env:DIST_DIR\$ReleaseDir" -ErrorAction Stop

# SOURCE
. $EnvReplace  -Recurse -Force -Path ".\src"

# PROGRAM
make clean
make MODE=release rebuild
Move-Item -Force "$env:DIST_DIR\$env:PROGRAM_EXE_NAME" "$env:DIST_DIR\$ReleaseDir"

# GUIDE
. $EnvReplace  -Force -OutputDir ".\dist" -Path "XMouseD.guide"
Move-Item -Force "$env:DIST_DIR\XMouseD.guide" "$env:DIST_DIR\$ReleaseDir\$env:PROGRAM_NAME.guide"
Copy-Item -Force "$env:ASSETS_DIR\Guide.info" "$env:DIST_DIR\$ReleaseDir\$env:PROGRAM_NAME.guide.info"


# INSTALL
. $EnvReplace -Force -OutputDir ".\dist" -Path "Install"
Move-Item -Force "$env:DIST_DIR\Install" "$env:DIST_DIR\$ReleaseDir\Install"
Copy-Item -Force "$env:ASSETS_DIR\Install.info" "$env:DIST_DIR\$ReleaseDir\Install.info"

# README
. $EnvReplace -Force -OutputDir ".\dist" -Path "XMouseD.readme"
Move-Item -Force "$env:DIST_DIR\XMouseD.readme" "$env:DIST_DIR\$ReleaseDir.readme"
Copy-Item -Force "$env:ASSETS_DIR\Ascii.info" "$env:DIST_DIR\$ReleaseDir.readme.info"

## Folder
Copy-Item -Force "$env:ASSETS_DIR\Drawer.info" "$env:DIST_DIR\$ReleaseDir\$ReleaseDir.info"


# Create LHA archive
cd $env:DIST_DIR
. ..\$env:LHATOOL -a "$ReleaseDir.lha" "$ReleaseDir\$env:PROGRAM_EXE_NAME" "$ReleaseDir\Install" "$ReleaseDir\Install.info" "$ReleaseDir\$env:PROGRAM_NAME.guide" "$ReleaseDir\$env:PROGRAM_NAME.guide.info" "$ReleaseDir.info" "$ReleaseDir.readme" "$ReleaseDir.readme.info"
. ..\$env:LHATOOL -l "$ReleaseDir.lha"
cd ..