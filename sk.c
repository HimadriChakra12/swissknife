// sk.c - Swissknife with cJSON integration

#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <stdint.h>
#include <urlmon.h>
#include "cJSON.h"
#pragma comment(lib, "urlmon.lib")

#define REPO_FOLDER "C:/farm/wheats/Swissknife/knives"
#define CONFIG_FILE "C:/farm/wheats/Swissknife/.pkgconfig"
#define INSTALLED_FILE "C:/farm/wheats/Swissknife/package.json"

typedef struct Package {
    char name[128];
    char id[64];
    char version[64];
    char url[1024];
    char silent[128];
    char type[16];
    char installer[64];
} Package;

////////////////////
// File utilities //
////////////////////

// Read entire file content to a string buffer (must free)
char* read_file(const char* filepath) {
    FILE* f = fopen(filepath, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char* buffer = malloc(size + 1);
    if (!buffer) {
        fclose(f);
        return NULL;
    }
    fread(buffer, 1, size, f);
    buffer[size] = 0;
    fclose(f);
    return buffer;
}

// Write string data to file (overwrite)
int write_file(const char* filepath, const char* data) {
    FILE* f = fopen(filepath, "w");
    if (!f) return -1;
    fwrite(data, 1, strlen(data), f);
    fclose(f);
    return 0;
}

///////////////////////////
// Repo URL Configuration //
///////////////////////////

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
        fgets(buffer, (int)size, f);
        buffer[strcspn(buffer, "\n")] = 0;
        fclose(f);
    } else {
        printf("Enter your Git repository URL: ");
        fgets(buffer, (int)size, stdin);
        buffer[strcspn(buffer, "\n")] = 0;
        save_repo_url(buffer);
    }
}

//////////////////////
// Git synchronization //
//////////////////////

void git_sync(const char* repo_url) {
    if (_access(REPO_FOLDER, 0) != 0) {
        printf("Cloning repository...\n");
        char cmd[1024];
        sprintf(cmd, "git clone %s %s", repo_url, REPO_FOLDER);
        system(cmd);
    } else {
        printf("Pulling latest packages...\n");
        char cmd[1024];
        sprintf(cmd, "git -C %s pull", REPO_FOLDER);
        system(cmd);
    }
}

/////////////////////////
// Package JSON parsing //
/////////////////////////

int parse_package_json(const char* filepath, Package* pkg) {
    char* content = read_file(filepath);
    if (!content) return -1;

    cJSON* root = cJSON_Parse(content);
    free(content);
    if (!root) return -2;

    cJSON* item = NULL;

    memset(pkg, 0, sizeof(Package));

    item = cJSON_GetObjectItem(root, "name");
    if (cJSON_IsString(item)) strncpy(pkg->name, item->valuestring, sizeof(pkg->name)-1);

    item = cJSON_GetObjectItem(root, "id");
    if (cJSON_IsString(item)) strncpy(pkg->id, item->valuestring, sizeof(pkg->id)-1);

    item = cJSON_GetObjectItem(root, "version");
    if (cJSON_IsString(item)) strncpy(pkg->version, item->valuestring, sizeof(pkg->version)-1);

    item = cJSON_GetObjectItem(root, "url");
    if (cJSON_IsString(item)) strncpy(pkg->url, item->valuestring, sizeof(pkg->url)-1);

    item = cJSON_GetObjectItem(root, "silent");
    if (cJSON_IsString(item)) strncpy(pkg->silent, item->valuestring, sizeof(pkg->silent)-1);
    else pkg->silent[0] = 0;

    item = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(item)) strncpy(pkg->type, item->valuestring, sizeof(pkg->type)-1);

    item = cJSON_GetObjectItem(root, "installer");
    if (cJSON_IsString(item)) strncpy(pkg->installer, item->valuestring, sizeof(pkg->installer)-1);
    else pkg->installer[0] = 0;

    cJSON_Delete(root);
    return 0;
}

////////////////////////////
// Installed packages JSON //
////////////////////////////

cJSON* load_installed_packages() {
    char* content = read_file(INSTALLED_FILE);
    if (!content) return NULL;
    cJSON* root = cJSON_Parse(content);
    free(content);
    if (!root || !cJSON_IsArray(root)) {
        if (root) cJSON_Delete(root);
        return NULL;
    }
    return root;
}

int save_installed_packages(cJSON* root) {
    if (!root) return -1;
    char* out = cJSON_Print(root);
    if (!out) return -2;
    int ret = write_file(INSTALLED_FILE, out);
    free(out);
    return ret;
}

void add_or_update_installed_package(const Package* pkg) {
    cJSON* root = load_installed_packages();
    if (!root) root = cJSON_CreateArray();

    int found = 0;
    int size = cJSON_GetArraySize(root);
    for (int i = 0; i < size; i++) {
        cJSON* item = cJSON_GetArrayItem(root, i);
        cJSON* id = cJSON_GetObjectItem(item, "id");
        if (id && cJSON_IsString(id) && strcmp(id->valuestring, pkg->id) == 0) {
            cJSON_ReplaceItemInObject(item, "name", cJSON_CreateString(pkg->name));
            cJSON_ReplaceItemInObject(item, "version", cJSON_CreateString(pkg->version));
            found = 1;
            break;
        }
    }

    if (!found) {
        cJSON* new_pkg = cJSON_CreateObject();
        cJSON_AddStringToObject(new_pkg, "name", pkg->name);
        cJSON_AddStringToObject(new_pkg, "id", pkg->id);
        cJSON_AddStringToObject(new_pkg, "version", pkg->version);
        cJSON_AddItemToArray(root, new_pkg);
    }

    save_installed_packages(root);
    cJSON_Delete(root);
}

///////////////////////
// Download function //
///////////////////////

int download_file(const char* url, const char* out_path) {
    char cmd[2048];
    sprintf(cmd,
        "powershell -Command \"Start-BitsTransfer -Source '%s' -Destination '%s'\"",
        url, out_path);
    return system(cmd);
}

/////////////////////////
// Install package //
/////////////////////////

void install_package(const char* sk_name) {
    char filepath[512];
    sprintf(filepath, "%s/%s.json", REPO_FOLDER, sk_name);

    Package pkg = {0};
    if (parse_package_json(filepath, &pkg) != 0) {
        printf("Package '%s' not found or invalid.\n", sk_name);
        return;
    }

    if (strlen(pkg.silent) == 0 && strlen(pkg.installer) > 0) {
        if (strcmp(pkg.installer, "nsis") == 0) strcpy(pkg.silent, "/S");
        else if (strcmp(pkg.installer, "inno") == 0) strcpy(pkg.silent, "/VERYSILENT /SUPPRESSMSGBOXES");
        else if (strcmp(pkg.installer, "msi") == 0) strcpy(pkg.silent, "/quiet /norestart");
        else if (strcmp(pkg.installer, "installshield") == 0) strcpy(pkg.silent, "/s /v\"/qn\"");
        else if (strcmp(pkg.installer, "squirrel") == 0) strcpy(pkg.silent, "--silent");
    }

    char out_path[MAX_PATH];
    sprintf(out_path, "%s\\%s.%s", getenv("TEMP"), pkg.id, pkg.type);

    printf("Downloading %s...\n", pkg.id);
    if (download_file(pkg.url, out_path) != 0) {
        printf("Download failed.\n");
        return;
    }

    SHELLEXECUTEINFOA sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpFile = out_path;
    sei.lpParameters = pkg.silent[0] ? pkg.silent : NULL;
    sei.nShow = pkg.silent[0] ? SW_HIDE : SW_SHOWNORMAL;
    sei.lpVerb = "open";

    printf("Installing %s...\n", pkg.id);
    if (!ShellExecuteExA(&sei)) {
        printf("Install failed.\n");
        return;
    }

    WaitForSingleObject(sei.hProcess, INFINITE);
    CloseHandle(sei.hProcess);
    printf("Installed %s successfully.\n", pkg.id);

    add_or_update_installed_package(&pkg);
}

//////////////////////////////
// List installed packages //
//////////////////////////////

void list_installed_packages() {
    cJSON* root = load_installed_packages();
    if (!root) {
        printf("No packages installed.\n");
        return;
    }

    printf("Installed packages:\n");
    int size = cJSON_GetArraySize(root);
    for (int i = 0; i < size; i++) {
        cJSON* item = cJSON_GetArrayItem(root, i);
        cJSON* name = cJSON_GetObjectItem(item, "name");
        cJSON* id = cJSON_GetObjectItem(item, "id");
        if (cJSON_IsString(name) && cJSON_IsString(id)) {
            printf("  %s (%s)\n", name->valuestring, id->valuestring);
        }
    }
    cJSON_Delete(root);
}

/////////////////////////////
// Show installed package info //
/////////////////////////////

void show_installed_info(const char* pkg_id) {
    cJSON* root = load_installed_packages();
    if (!root) {
        printf("No installed packages recorded.\n");
        return;
    }

    int size = cJSON_GetArraySize(root);
    int found = 0;
    for (int i = 0; i < size; i++) {
        cJSON* item = cJSON_GetArrayItem(root, i);
        cJSON* id = cJSON_GetObjectItem(item, "id");
        if (!cJSON_IsString(id)) continue;

        if (strcmp(id->valuestring, pkg_id) == 0) {
            cJSON* name = cJSON_GetObjectItem(item, "name");
            cJSON* version = cJSON_GetObjectItem(item, "version");
            printf("Name            : %s\n", cJSON_IsString(name) ? name->valuestring : "(unknown)");
            printf("Version         : %s\n", cJSON_IsString(version) ? version->valuestring : "(unknown)");
            printf("ID              : %s\n", id->valuestring);
            found = 1;
            break;
        }
    }
    cJSON_Delete(root);
    if (!found) printf("error: package '%s' not found in installed list\n", pkg_id);
}

/////////////////////////////
// Search packages in repo //
/////////////////////////////

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

        Package pkg = {0};
        if (parse_package_json(fullpath, &pkg) != 0) continue;

        if (strstr(pkg.id, query) || strstr(pkg.name, query)) {
            found = 1;
            printf("%s - %s (%s)\n", pkg.id, pkg.name, pkg.version);
        }
    } while (_findnext(hFile, &file) == 0);
    _findclose(hFile);
    if (!found) printf("No matching packages found in repo for '%s'\n", query);
}

/////////////////////////////
// Install from installed package.json //
/////////////////////////////

void install_from_package_json() {
    cJSON* root = load_installed_packages();
    if (!root) {
        printf("No package.json found.\n");
        return;
    }

    int size = cJSON_GetArraySize(root);
    for (int i = 0; i < size; i++) {
        cJSON* item = cJSON_GetArrayItem(root, i);
        cJSON* id = cJSON_GetObjectItem(item, "id");
        if (!cJSON_IsString(id)) continue;

        char sk_path[512];
        sprintf(sk_path, "%s/%s.json", REPO_FOLDER, id->valuestring);

        if (_access(sk_path, 0) == 0) {
            printf("Installing from repo: %s\n", id->valuestring);
            install_package(id->valuestring);
        } else {
            printf("Package not found in repo: %s\n", id->valuestring);
        }
    }
    cJSON_Delete(root);
}

///////////////////////
// Check updates //
///////////////////////

void check_updates() {
    cJSON* installed = load_installed_packages();
    if (!installed) {
        printf("No installed packages recorded.\n");
        return;
    }

    int size = cJSON_GetArraySize(installed);
    for (int i = 0; i < size; i++) {
        cJSON* item = cJSON_GetArrayItem(installed, i);
        cJSON* id = cJSON_GetObjectItem(item, "id");
        cJSON* name = cJSON_GetObjectItem(item, "name");
        cJSON* version_installed = cJSON_GetObjectItem(item, "version");
        if (!cJSON_IsString(id) || !cJSON_IsString(name) || !cJSON_IsString(version_installed)) continue;

        char filepath[512];
        sprintf(filepath, "%s/%s.json", REPO_FOLDER, id->valuestring);

        Package pkg = {0};
        if (parse_package_json(filepath, &pkg) != 0) continue;

        if (strcmp(version_installed->valuestring, pkg.version) != 0) {
            printf("Update available: %s (%s → %s)\n", name->valuestring, version_installed->valuestring, pkg.version);
        }
    }
    cJSON_Delete(installed);
}

/////////////////////
// List repo packages //
/////////////////////

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

        Package pkg = {0};
        if (parse_package_json(fullpath, &pkg) == 0) {
            printf(" - %s (%s)\n", pkg.name[0] ? pkg.name : file.name, pkg.id);
        } else {
            printf(" - %s\n", file.name);
        }
    } while (_findnext(hFile, &file) == 0);

    _findclose(hFile);
}

///////////////////////
// Main entry point //
///////////////////////

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
        printf("  sk -Su                 [Check for updates]\n");
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
        return 0;
    }

    if (strcmp(argv[1], "-Su") == 0) {
        check_updates();
        return 0;
    }

    git_sync(repo_url);

    if (strcmp(argv[1], "-Ql") == 0) {
        list_packages();
        return 0;
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
