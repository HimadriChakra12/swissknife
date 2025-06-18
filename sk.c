#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <stdint.h>

#define REPO_FOLDER "C:/farm/wheats/Swissknife/knives"
#define CONFIG_FILE "C:/farm/wheats/Swissknife/.pkgconfig"
#define INSTALLED_FILE "C:/farm/wheats/Swissknife/package.json"

void save_repo(const char* name, const char* url) {
    FILE* f = fopen(CONFIG_FILE, "a");
    if (f) {
        fprintf(f, "%s|%s\n", name, url);
        fclose(f);
    }
}

void list_repos() {
    FILE* f = fopen(CONFIG_FILE, "r");
    if (!f) {
        printf("No repos configured.\n");
        return;
    }

    printf("Configured repos:\n");
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char name[128], url[900];
        if (sscanf(line, "%127[^|]|%899[^\n]", name, url) == 2) {
            printf(" - %s: %s\n", name, url);
        }
    }

    fclose(f);
}

void save_repo_url(const char* url) {
    FILE* f = fopen(CONFIG_FILE, "w");
    if (f) {
        fprintf(f, "%s\n", url);
        fclose(f);
    }
}
void sync_all_repos() {
    FILE* f = fopen(CONFIG_FILE, "r");
    if (!f) return;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char name[64], url[960];
        if (sscanf(line, "%63[^|]|%959[^\n]", name, url) == 2) {
            char dest[1024];
            sprintf(dest, "%s/%s", REPO_FOLDER, name);
            if (_access(dest, 0) != 0) {
                printf("Cloning %s...\n", name);
                char cmd[2048];
                sprintf(cmd, "git clone %s %s", url, dest);
                system(cmd);
            } else {
                printf("Pulling %s...\n", name);
                char cmd[2048];
                sprintf(cmd, "git -C %s pull", dest);
                system(cmd);
            }
        }
    }

    fclose(f);
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
    FILE* f = fopen(INSTALLED_FILE, "a+");
    if (f) {
        fprintf(f, "{ \"name\": \"%s\", \"id\": \"%s\", \"version\": \"%s\" },\n", name, id, version);
        fclose(f);
    }
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
    char name[128] = {0}, version[64] = {0};
    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "\"url\"")) sscanf(line, " \"url\" : \"%[^\"]\"", url);
        else if (strstr(line, "\"silent\"")) sscanf(line, " \"silent\" : \"%[^\"]\"", silent);
        else if (strstr(line, "\"id\"")) sscanf(line, " \"id\" : \"%[^\"]\"", id);
        else if (strstr(line, "\"type\"")) sscanf(line, " \"type\" : \"%[^\"]\"", type);
        else if (strstr(line, "\"name\"")) sscanf(line, " \"name\" : \"%[^\"]\"", name);
        else if (strstr(line, "\"version\"")) sscanf(line, " \"version\" : \"%[^\"]\"", version);
    }
    fclose(fp);

    char out_path[MAX_PATH];
    sprintf(out_path, "%s\\%s.%s", getenv("TEMP"), id, type);

    printf("Downloading %s...\n", url);
    HRESULT hr = URLDownloadToFileA(NULL, url, out_path, 0, NULL);
    if (hr != S_OK) {
        printf("Download failed.\n");
        return;
    }

    SHELLEXECUTEINFOA sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpFile = out_path;
    sei.lpParameters = silent;
    sei.nShow = SW_HIDE;
    sei.lpVerb = "open";

    printf("Installing %s...\n", id);
    if (!ShellExecuteExA(&sei)) {
        printf("Install failed.\n");
        return;
    }

    WaitForSingleObject(sei.hProcess, INFINITE);
    CloseHandle(sei.hProcess);
    printf("Installed %s successfully.\n", id);

    // Log it
    log_installed(name, id, version);
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

            // Build path to repo's JSON
            char sk_path[512];
            sprintf(sk_path, "%s/%s.json", REPO_FOLDER, id);

            if (_access(sk_path, 0) == 0) {
                printf("Installing from repo: %s\n", id);
                install_package(id);  // reuse your existing install function
            } else {
                printf("Package not found in repo: %s\n", id);
            }
        }
    }
    fclose(f);
}


int main(int argc, char *argv[]) {
    char repo_url[1024] = {0};

    if (argc == 3 && strcmp(argv[1], "-Sr") == 0) {
        save_repo_url(argv[2]);
        printf("Repository URL saved.\n");
        return 0;
    }

    read_repo_url(repo_url, sizeof(repo_url));

    if (argc < 2) {
        printf("Usage:\n");
        printf("  sk -Q        [List packages]\n");
        printf("  sk -S <sk>  [Install package]\n");
        printf("  sk -Sy       [Refresh package list]\n");
        printf("  sk -Su       [Check for updates]\n");
        printf("  sk -Sr <url> [Set repo URL]\n");
        printf("  sk -Si [Import package.json]\n");
        return 0;
    }

    if (strcmp(argv[1], "-Sy") == 0) {
        git_sync(repo_url);
        return 0;
    }

else if (strcmp(argv[1], "-Si") == 0) {
    install_from_package_json();
}
    if (strcmp(argv[1], "-Q") == 0) {
        list_packages();
    } else if (strcmp(argv[1], "-S") == 0 && argc >= 3) {
        install_package(argv[2]);
    } else if (strcmp(argv[1], "-Su") == 0) {
        check_updates();
    } else {
        printf("Unknown command.\n");
    }

    return 0;
}
