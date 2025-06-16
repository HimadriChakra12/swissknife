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

1. Installations

```
sudo sk -S <package_name>
```

2. Package List

```
sudo sk -Q
```

3. Repo Refresh

```
sudo sk -Sy
```

4. Adding New Repo

```
sudo sk -Sr <repo_git>
```

