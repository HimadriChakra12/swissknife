# Package Creation Manual

1. Make a Git Repo for packages. If you want your own.
or, You can use my repo.
```
https://github.com/HimadriChakra12/knives.git
```
2. If You want to make a Package you will need a `.json` for the package.
You Will Need `name`, `id`, `url`, `type`, `silent`.
For example,
```
{
  "name": "Firefox",
  "id": "firefox",
  "url": "https://download.mozilla.org/?product=firefox-latest&os=win64&lang=en-US",
  "type": "exe",
  "silent": "/S"
}
```
- Name and Id will specify the package. Url will have the download link.
- `Silent` has `/S` that will exicute the setup file in silent mode. It means it will run in silent mode so automatic setup.

