# Package Creation Manual

1. Make a Git Repo for packages if you want your own packages listed
  or, You can use my repo.
```
https://github.com/HimadriChakra12/knives.git
```
2. If You want to make a Package you will need a `.json` for the package.
You Will Need `name`, `id`, `url`,`version`, `type`, `installer`, `silent`.
For example,
```
{
  "name": "7-Zip",
  "id": "7zip",
  "version": "23.01",
  "url": "https://www.7-zip.org/a/7z2301-x64.exe",
  "silent": "/S",
  "type": "exe",
  "installer": "nsis",
  "uninstaller": "C:\\Program Files\\7-Zip\\Uninstall.exe",
  "untype": "nsis"
}
```

- Name and Id will specify the package. Url will have the download link. Versions are for checking updates
- Installer Specifies what type of setup file it is. Installer will add commands as according automatically.

```
"installer": "nsis"       → /S
"installer": "msi"        → /quiet /norestart
"installer": "inno"       → /VERYSILENT /SUPPRESSMSGBOXES
"installer": "squirrel"   → --silent
```
