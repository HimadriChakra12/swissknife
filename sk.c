#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <stdint.h>
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")

#define REPO_FOLDER "C:/farm/wheats/Swissknife/knives"
#define CONFIG_FILE "C:/farm/wheats/Swissknife/.pkgconfig"
#define INSTALLED_FILE "C:/farm/wheats/Swissknife/package.json"

void save_repo_url(const char* url) {
    FILE* f = fopen(CONFIG_FILE, "w");
    if (f) {
        fprintf(f, "%s\n", url);
        fclose(f);
    }
}

void read_repo_url(char* buffer, size_t size) {
    FILE* f = fopen(CONFIG_FILE, "r");
    if (f) {
        fgets(buffer, size, f);
        buffer[strcspn(buffer, "\n")] = 0;
        fclose(f);
    } else {
        printf("Enter your Git repository URL: ");
        fgets(buffer, (int)size, stdin);
        buffer[strcspn(buffer, "\n")] = 0;
        save_repo_url(buffer);
    }
}

void git_sync(const char* repo_url) {
    if (_access(REPO_FOLDER, 0) != 0) {
        printf("Cloning repository...\n");
        char cmd[1024];
        sprintf(cmd, "git clone %s %s", repo_url, REPO_FOLDER);
        system(cmd);
    } else {
        printf("Pulling latest packages...\n");
        system("git -C " REPO_FOLDER " pull");
    }
}

void log_installed(const char* name, const char* id, const char* version) {
    FILE* f = fopen(INSTALLED_FILE, "r");
    if (f) {
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            char existing_id[64] = {0};
            if (sscanf(line, " { \"name\": \"%*[^\"]\", \"id\": \"%63[^\"]\"", existing_id) == 1) {
                if (strcmp(existing_id, id) == 0) {
                    fclose(f);
                    printf("Package %s already logged. Skipping.\n", id);
                    return;
                }
            }
        }
        fclose(f);
    }

    f = fopen(INSTALLED_FILE, "a");
    if (f) {
        fprintf(f, "{ \"name\": \"%s\", \"id\": \"%s\", \"version\": \"%s\" },\n", name, id, version);
        fclose(f);
    }
}



int download_file(const char* url, const char* out_path) {
    char cmd[2048];
    sprintf(cmd,
        "powershell -Command \"Start-BitsTransfer -Source '%s' -Destination '%s'\"",
        url, out_path);
    return system(cmd);
}

void install_package(const char* sk_name) {
    char filepath[512];
    sprintf(filepath, "%s/%s.json", REPO_FOLDER, sk_name);

    FILE* fp = fopen(filepath, "r");
    if (!fp) {
        printf("Package '%s' not found.\n", sk_name);
        return;
    }

    char url[1024] = {0}, silent[128] = {0}, id[64] = {0}, type[8] = {0};
    char name[128] = {0}, version[64] = {0}, installer[64] = {0};
    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "\"url\"")) sscanf(line, " \"url\" : \"%[^\"]\"", url);
        else if (strstr(line, "\"silent\"")) sscanf(line, " \"silent\" : \"%[^\"]\"", silent);
        else if (strstr(line, "\"id\"")) sscanf(line, " \"id\" : \"%[^\"]\"", id);
        else if (strstr(line, "\"type\"")) sscanf(line, " \"type\" : \"%[^\"]\"", type);
        else if (strstr(line, "\"name\"")) sscanf(line, " \"name\" : \"%[^\"]\"", name);
        else if (strstr(line, "\"version\"")) sscanf(line, " \"version\" : \"%[^\"]\"", version);
        else if (strstr(line, "\"installer\"")) sscanf(line, " \"installer\" : \"%[^\"]\"", installer);
    }
    fclose(fp);

    if (strlen(silent) == 0 && strlen(installer) > 0) {
        if (strcmp(installer, "nsis") == 0) strcpy(silent, "/S");
        else if (strcmp(installer, "inno") == 0) strcpy(silent, "/VERYSILENT /SUPPRESSMSGBOXES");
        else if (strcmp(installer, "msi") == 0) strcpy(silent, "/quiet /norestart");
        else if (strcmp(installer, "installshield") == 0) strcpy(silent, "/s /v\"/qn\"");
        else if (strcmp(installer, "squirrel") == 0) strcpy(silent, "--silent");
    }

    char out_path[MAX_PATH];
    sprintf(out_path, "%s\\%s.%s", getenv("TEMP"), id, type);

    printf("Downloading %s...\n", id);
    if (download_file(url, out_path) != 0) {
        printf("Download failed.\n");
        return;
    }

    SHELLEXECUTEINFOA sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpFile = out_path;
    sei.lpParameters = silent;
    sei.nShow = silent[0] ? SW_HIDE : SW_SHOWNORMAL;
    sei.lpVerb = "open";

    printf("Installing %s...\n", id);
    if (!ShellExecuteExA(&sei)) {
        printf("Install failed.\n");
        return;
    }

    WaitForSingleObject(sei.hProcess, INFINITE);
    CloseHandle(sei.hProcess);
    printf("Installed %s successfully.\n", id);

    log_installed(name, id, version);
}

void list_installed_packages() {
    FILE* f = fopen(INSTALLED_FILE, "r");
    if (!f) {
        printf("No packages installed.\n");
        return;
    }
    char line[512];
    printf("Installed packages:\n");
    while (fgets(line, sizeof(line), f)) {
        char name[128] = {0}, id[64] = {0};
        sscanf(line, " { \"name\": \"%[^\"]\", \"id\": \"%[^\"]\"", name, id);
        printf("  %s (%s)\n", name, id);
    }
    fclose(f);
}

void show_installed_info(const char* pkg_id) {
    FILE* f = fopen(INSTALLED_FILE, "r");
    if (!f) {
        printf("No installed packages recorded.\n");
        return;
    }
    char line[1024];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        char name[128] = {0}, id[64] = {0}, version[64] = {0};
        sscanf(line, " { \"name\": \"%[^\"]\", \"id\": \"%[^\"]\", \"version\": \"%[^\"]\" }", name, id, version);
        if (strcmp(id, pkg_id) == 0) {
            printf("Name            : %s\n", name);
            printf("Version         : %s\n", version);
            printf("ID              : %s\n", id);
            found = 1;
            break;
        }
    }
    fclose(f);
    if (!found) printf("error: package '%s' not found in installed list\n", pkg_id);
}

void search_repo_package(const char* query) {
    struct _finddata_t file;
    intptr_t hFile;
    char path[512];
    sprintf(path, "%s/*.json", REPO_FOLDER);
    hFile = _findfirst(path, &file);
    if (hFile == -1L) {
        printf("No packages found in repo.\n");
        return;
    }
    int found = 0;
    do {
        char fullpath[512];
        sprintf(fullpath, "%s/%s", REPO_FOLDER, file.name);
        FILE* fp = fopen(fullpath, "r");
        if (!fp) continue;
        char name[128] = {0}, id[64] = {0}, version[64] = {0}, line[1024];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "\"name\"")) sscanf(line, " \"name\" : \"%[^\"]\"", name);
            else if (strstr(line, "\"id\"")) sscanf(line, " \"id\" : \"%[^\"]\"", id);
            else if (strstr(line, "\"version\"")) sscanf(line, " \"version\" : \"%[^\"]\"", version);
        }
        fclose(fp);
        if (strstr(id, query) || strstr(name, query)) {
            found = 1;
            printf("%s - %s (%s)\n", id, name, version);
        }
    } while (_findnext(hFile, &file) == 0);
    _findclose(hFile);
    if (!found) printf("No matching packages found in repo for '%s'\n", query);
}

void install_from_package_json() {
    FILE* f = fopen(INSTALLED_FILE, "r");
    if (!f) {
        printf("No package.json found.\n");
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char id[64] = {0};
        if (strstr(line, "\"id\"")) {
            sscanf(line, "%*[^:]: \"%[^\"]\"", id);

            char sk_path[512];
            sprintf(sk_path, "%s/%s.json", REPO_FOLDER, id);

            if (_access(sk_path, 0) == 0) {
                printf("Installing from repo: %s\n", id);
                install_package(id);
            } else {
                printf("Package not found in repo: %s\n", id);
            }
        }
    }
    fclose(f);
}

void check_updates() {
    FILE* installed = fopen(INSTALLED_FILE, "r");
    if (!installed) {
        printf("No installed packages recorded.\n");
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), installed)) {
        char name[128] = {0}, id[64] = {0}, version_installed[64] = {0};
        sscanf(line, " { \"name\": \"%[^\"]\", \"id\": \"%[^\"]\", \"version\": \"%[^\"]\" }", name, id, version_installed);

        char filepath[512];
        sprintf(filepath, "%s/%s.json", REPO_FOLDER, id);
        FILE* sk = fopen(filepath, "r");
        if (!sk) continue;

        char version_repo[64] = {0}, line2[512];
        while (fgets(line2, sizeof(line2), sk)) {
            if (strstr(line2, "\"version\"")) {
                sscanf(line2, " \"version\" : \"%[^\"]\"", version_repo);
                break;
            }
        }
        fclose(sk);

        if (strcmp(version_installed, version_repo) != 0) {
            printf("Update available: %s (%s → %s)\n", name, version_installed, version_repo);
        }
    }
    fclose(installed);
}

void list_packages() {
    struct _finddata_t file;
    intptr_t hFile;
    char path[512];
    sprintf(path, "%s/*.json", REPO_FOLDER);

    hFile = _findfirst(path, &file);
    if (hFile == -1L) {
        printf("No packages found.\n");
        return;
    }

    printf("Available packages:\n");
    do {
        char fullpath[512];
        sprintf(fullpath, "%s/%s", REPO_FOLDER, file.name);

        FILE* fp = fopen(fullpath, "r");
        if (!fp) continue;

        char name[256] = {0};
        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "\"name\"")) {
                sscanf(line, " \"name\" : \"%[^\"]\"", name);
                break;
            }
        }
        fclose(fp);

        char* no_ext = strtok(file.name, ".");
        printf(" - %s (%s)\n", name[0] ? name : no_ext, no_ext);
    } while (_findnext(hFile, &file) == 0);

    _findclose(hFile);
}

int main(int argc, char* argv[]) {
    char repo_url[1024] = {0};

    if (argc == 3 && strcmp(argv[1], "-Sr") == 0) {
        save_repo_url(argv[2]);
        printf("Repository URL saved.\n");
        return 0;
    }

    read_repo_url(repo_url, sizeof(repo_url));

    if (argc < 2) {
        printf("Usage:\n");
        printf("  sk -Q                  [List installed packages]\n");
        printf("  sk -Q --info <sk>      [Show installed package info]\n");
        printf("  sk -Ql                 [List All packages in the Repo]\n");
        printf("  sk -Ss <sk>            [Search for package in repo]\n");
        printf("  sk -S <sk>             [Install package]\n");
        printf("  sk -Sy                 [Refresh package list]\n");
        printf("  sk -Si                 [Install from Package.json]\n");
        printf("  sk -Sr <url>           [Set repo URL]\n");
        return 0;
    }

    if (strcmp(argv[1], "-Sy") == 0) {
        git_sync(repo_url);
        return 0;
    }

    if (strcmp(argv[1], "-Q") == 0) {
        if (argc == 2) {
            list_installed_packages();
        } else if (argc == 4 && strcmp(argv[2], "--info") == 0) {
            show_installed_info(argv[3]);
        } else {
            printf("Usage:\n  sk -Q              [List installed packages]\n  sk -Q --info <pkg> [Show info]\n");
        }
        return 0;
    }

    if (strcmp(argv[1], "-Si") == 0) {
        install_from_package_json();
    }

    if (strcmp(argv[1], "-Su") == 0) {
        check_updates();
    }

    git_sync(repo_url);

    if (strcmp(argv[1], "-Ql") == 0) {
        list_packages();
    }

    if (strcmp(argv[1], "-Ss") == 0 && argc == 3) {
        search_repo_package(argv[2]);
        return 0;
    }

    if (strcmp(argv[1], "-S") == 0 && argc == 3) {
        install_package(argv[2]);
        return 0;
    }

    printf("Unknown command. Run without arguments for help.\n");
    return 0;
}
