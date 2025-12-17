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
if ((Test-Path $ReleaseDir)) {
    Remove-item -Recurse -Force $ReleaseDir -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Path $ReleaseDir | Out-Null

# update env in sources
. $EnvReplace  -Recurse -Force -Path ".\src"

# build release
make clean
make MODE=release rebuild
Move-Item -Force "$env:DIST_DIR\$env:PROGRAM_EXE_NAME" "$ReleaseDir"

# update env in guide
. $EnvReplace  -Force -OutputDir ".\dist" -Path "XMouseD.guide"
Move-Item -Force "$env:DIST_DIR\XMouseD.guide" "$ReleaseDir\$env:PROGRAM_NAME.guide"

. $EnvReplace -Force -OutputDir ".\dist" -Path "XMouseD.readme"
Copy-Item -Force "$env:DIST_DIR\XMouseD.readme" "$ReleaseDir\readme"
Move-Item -Force "$env:DIST_DIR\XMouseD.readme" "$env:DIST_DIR\$env:PROGRAM_NAME-$escapedVersion.readme"

