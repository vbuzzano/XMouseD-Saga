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
$ReleaseDir = "$env:DIST_DIR\$env:PROGRAM_NAME-$escapedVersion"

# create release directory
if ((Test-Path $env:DIST_DIR)) {
    Remove-item -Recurse -Force $env:DIST_DIR -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Path $ReleaseDir | Out-Null

# SOURCE
. $EnvReplace  -Recurse -Force -Path ".\src"

# PROGRAM
make clean
make MODE=release rebuild
Move-Item -Force "$env:DIST_DIR\$env:PROGRAM_EXE_NAME" "$ReleaseDir"

# GUIDE
. $EnvReplace  -Force -OutputDir ".\dist" -Path "XMouseD.guide"
Move-Item -Force "$env:DIST_DIR\XMouseD.guide" "$ReleaseDir\$env:PROGRAM_NAME.guide"
Copy-Item -Force "$env:ASSETS_DIR\Guide.info" "$ReleaseDir\$env:PROGRAM_NAME.guide.info"

# README
. $EnvReplace -Force -OutputDir ".\dist" -Path "XMouseD.readme"
Move-Item -Force "$env:DIST_DIR\XMouseD.readme" "$env:DIST_DIR\$env:PROGRAM_NAME-$escapedVersion.readme"
Copy-Item -Force "$env:ASSETS_DIR\Ascii.info" "$env:DIST_DIR\$env:PROGRAM_NAME-$escapedVersion.readme.info"

# INSTALL
. $EnvReplace -Force -OutputDir ".\dist" -Path "Install"
Move-Item -Force "$env:DIST_DIR\Install" "$ReleaseDir\Install"
Copy-Item -Force "$env:ASSETS_DIR\Install.info" "$ReleaseDir\Install.info"

# Folder
Copy-Item -Force "$env:ASSETS_DIR\Drawer.info" "$env:DIST_DIR\$env:PROGRAM_NAME-$escapedVersion.info"


# Create LHA archive
cd $env:DIST_DIR
. ..\$env:LHATOOL -a $env:PROGRAM_NAME-$escapedVersion.lha $env:PROGRAM_NAME-$escapedVersion $env:PROGRAM_NAME-$escapedVersion\Install $env:PROGRAM_NAME-$escapedVersion\Install.info  $env:PROGRAM_NAME-$escapedVersion\$env:PROGRAM_EXE_NAME $env:PROGRAM_NAME-$escapedVersion\$env:PROGRAM_NAME.guide $env:PROGRAM_NAME-$escapedVersion\$env:PROGRAM_NAME.guide.info $env:PROGRAM_NAME-$escapedVersion.info $env:PROGRAM_NAME-$escapedVersion.readme $env:PROGRAM_NAME-$escapedVersion.readme.info
. ..\$env:LHATOOL -l $env:PROGRAM_NAME-$escapedVersion.lha
cd ..