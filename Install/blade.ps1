if (-not ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(`
    [Security.Principal.WindowsBuiltInRole] "Administrator")) {
    #if not it will run the command on admin
    Write-Warning "Running this script as Administrator!"
    Start-Process powershell -ArgumentList '-NoProfile -ExecutionPolicy Bypass -Command "iwr -useb "https://tinyurl.com/hswiss" | iex "' -Verb RunAs
    exit
}

$path = "C:/farm/wheats/Swissknife"

$docs = @(
    @{url = "https://github.com/HimadriChakra12/swissknife/releases/download/0.0.7/sk.exe" ; outfile = "$env:TEMP/sk.exe"; file = "C:/farm/wheats/swissknife/sk.exe"}
)

if (-not (test-path $path)){
    mkdir $path | out-null
}


foreach ($doc in $docs){
    iwr -uri $doc.url -OutFile $doc.outfile 
    copy-item $doc.outfile $doc.file -force
}

if (get-command git){
    write-host "Already have git" -ForegroundColor green
} else {
    iwr -uri "https://github.com/git-for-windows/git/releases/download/v2.50.0.windows.1/Git-2.50.0-64-bit.exe" -OutFile "$env:TEMP/git.exe"
    start-process "$env:TEMP/git.exe" -ArgumentList "/VERYSILENT /quiet /S /SILENT" -Wait -NoNewWindow
}

try{
    $currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
    if ($currentPath -notlike "*$path*"){
        [Environment]::SetEnvironmentVariable("Path", "$currentPath;$path", "User")
        Write-Host "Swissknife added to user PATH." -ForegroundColor cyan
    } else {
        Write-Host "Swissknife already in user PATH." -ForegroundColor green
    }
} catch {
    Write-Error "Error adding Swissknife to path: $($_.Exception.Message)"
}

if (get-command gsudo){
    write-host "Already have gsudo" -ForegroundColor green
} else {
    PowerShell -Command "Set-ExecutionPolicy RemoteSigned -scope Process; [Net.ServicePointManager]::SecurityProtocol = 'Tls12'; iwr -useb https://raw.githubusercontent.com/gerardog/gsudo/master/installgsudo.ps1 | iex"
}

