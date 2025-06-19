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
  "name": "qimgv",
  "id": "qimgv",
  "url": "https://github.com/easymodo/qimgv/releases/download/v1.0.2/qimgv-x64_1.0.2.exe",
  "version": "1.0.2",
  "type": "exe",
  "installer": "inno",
  "silent": ""
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
