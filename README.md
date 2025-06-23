# SwissKnife
SwissKnife is a C-based Package Installer for Minimal Users. It uses Git Repo so you can make your own sorce any way.
[Will be a full-fletched Package Manager]

## Installation
You can install [wheat](https://github.com/HimadriChakra12/wheat) and install it from there.
Or,
```powershell
iwr -useb "https://tinyurl.com/hswiss" | iex 
```
Copy and paste the code to Powershell. Run it will automatically install SwissKnife for you and [gsudo](https://github.com/gerardog/gsudo#installation) gets installed with it. Using `sudo` for admin previlages is good.

## [Sa]usage
It has a pacman type option to install with a alias of `sk`

```
  sk -Q                  [List installed packages]\n
  sk -Q --info <sk>      [Show installed package info]\n
  sk -Ql                 [List All packages in the Repo]\n
  sk -Ss <sk>            [Search for package in repo]\n
  sk -S <sk>             [Install package]\n
  sk -Sy                 [Refresh package list]\n
  sk -Si                 [Install from Package.json]\n
  sk -Su                 [Check for updates]\n
  sk -Sr <url>           [Set repo URL]\n
```

