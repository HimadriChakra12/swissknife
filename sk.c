// sk.c - Swissknife with cJSON and multithreaded parallel BITS download, sequential install

#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <stdint.h>
#include "cJSON.h"
#pragma comment(lib, "urlmon.lib")

#define REPO_FOLDER "C:/farm/wheats/Swissknife/knives"
#define CONFIG_FILE "C:/farm/wheats/Swissknife/.pkgconfig"
#define INSTALLED_FILE "C:/farm/wheats/Swissknife/package.json"

HANDLE installed_mutex;

typedef struct Package {
    char name[128];
    char id[64];
    char version[64];
    char url[1024];
    char silent[128];
    char type[16];
    char installer[64];
    char out_path[MAX_PATH];
} Package;

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

int write_file(const char* filepath, const char* data) {
    FILE* f = fopen(filepath, "w");
    if (!f) return -1;
    fwrite(data, 1, strlen(data), f);
    fclose(f);
    return 0;
}

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

void git_sync(const char* repo_url) {
    if (_access(REPO_FOLDER, 0) != 0) {
        char cmd[1024];
        sprintf(cmd, "git clone %s %s", repo_url, REPO_FOLDER);
        system(cmd);
    } else {
        char cmd[1024];
        sprintf(cmd, "git -C %s pull", REPO_FOLDER);
        system(cmd);
    }
}

int parse_package_json(const char* filepath, Package* pkg) {
    char* content = read_file(filepath);
    if (!content) return -1;
    cJSON* root = cJSON_Parse(content);
    free(content);
    if (!root) return -2;

    cJSON* item = NULL;
    memset(pkg, 0, sizeof(Package));
    if ((item = cJSON_GetObjectItem(root, "name"))) strncpy(pkg->name, item->valuestring, sizeof(pkg->name)-1);
    if ((item = cJSON_GetObjectItem(root, "id"))) strncpy(pkg->id, item->valuestring, sizeof(pkg->id)-1);
    if ((item = cJSON_GetObjectItem(root, "version"))) strncpy(pkg->version, item->valuestring, sizeof(pkg->version)-1);
    if ((item = cJSON_GetObjectItem(root, "url"))) strncpy(pkg->url, item->valuestring, sizeof(pkg->url)-1);
    if ((item = cJSON_GetObjectItem(root, "silent"))) strncpy(pkg->silent, item->valuestring, sizeof(pkg->silent)-1);
    if ((item = cJSON_GetObjectItem(root, "type"))) strncpy(pkg->type, item->valuestring, sizeof(pkg->type)-1);
    if ((item = cJSON_GetObjectItem(root, "installer"))) strncpy(pkg->installer, item->valuestring, sizeof(pkg->installer)-1);

    sprintf(pkg->out_path, "%s\\%s.%s", getenv("TEMP"), pkg->id, pkg->type);
    cJSON_Delete(root);
    return 0;
}

int download_file(const char* url, const char* out_path) {
    char cmd[2048];
    sprintf(cmd,
        "powershell -WindowStyle Hidden -Command \"Start-BitsTransfer -Source '%s' -Destination '%s'\"",
        url, out_path);
    return system(cmd);
}

DWORD WINAPI download_thread(LPVOID param) {
    Package* pkg = (Package*)param;
    printf("Downloading %s setup...\n", pkg->name);
    if (download_file(pkg->url, pkg->out_path) != 0) {
        printf("Download failed for %s\n", pkg->name);
    }
    return 0;
}

void add_or_update_installed_package(Package* pkg) {
    WaitForSingleObject(installed_mutex, INFINITE);

    char* content = read_file(INSTALLED_FILE);
    cJSON* root = NULL;
    if (content) {
        root = cJSON_Parse(content);
        free(content);
    }
    if (!root) root = cJSON_CreateArray();

    cJSON* updated = cJSON_CreateObject();
    cJSON_AddStringToObject(updated, "name", pkg->name);
    cJSON_AddStringToObject(updated, "id", pkg->id);
    cJSON_AddStringToObject(updated, "version", pkg->version);

    int len = cJSON_GetArraySize(root);
    for (int i = 0; i < len; ++i) {
        cJSON* item = cJSON_GetArrayItem(root, i);
        cJSON* id = cJSON_GetObjectItem(item, "id");
        if (id && strcmp(id->valuestring, pkg->id) == 0) {
            cJSON_DeleteItemFromArray(root, i);
            break;
        }
    }

    cJSON_AddItemToArray(root, updated);
    char* output = cJSON_Print(root);
    write_file(INSTALLED_FILE, output);
    free(output);
    cJSON_Delete(root);

    ReleaseMutex(installed_mutex);
}

void wait_and_install_packages(int count, Package packages[]) {
    for (int i = 0; i < count; ++i) {
        if (!packages[i].silent[0] && packages[i].installer[0]) {
            if (strcmp(packages[i].installer, "nsis") == 0) strcpy(packages[i].silent, "/S");
            else if (strcmp(packages[i].installer, "inno") == 0) strcpy(packages[i].silent, "/VERYSILENT /SUPPRESSMSGBOXES");
            else if (strcmp(packages[i].installer, "msi") == 0) strcpy(packages[i].silent, "/quiet /norestart");
            else if (strcmp(packages[i].installer, "installshield") == 0) strcpy(packages[i].silent, "/s /v\"/qn\"");
            else if (strcmp(packages[i].installer, "squirrel") == 0) strcpy(packages[i].silent, "--silent");
        }

        printf("Installing %s...\n", packages[i].name);
        SHELLEXECUTEINFOA sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpFile = packages[i].out_path;
        sei.lpParameters = packages[i].silent[0] ? packages[i].silent : NULL;
        sei.nShow = packages[i].silent[0] ? SW_HIDE : SW_SHOWNORMAL;
        sei.lpVerb = "open";
        if (ShellExecuteExA(&sei)) {
            WaitForSingleObject(sei.hProcess, INFINITE);
            CloseHandle(sei.hProcess);
            add_or_update_installed_package(&packages[i]);
            printf("%s installed successfully.\n", packages[i].name);
        } else {
            printf("Installation failed for %s\n", packages[i].name);
        }
    }
}

void download_and_install_packages(int count, char* package_names[]) {
    Package* packages = malloc(sizeof(Package) * count);
    HANDLE* threads = malloc(sizeof(HANDLE) * count);
    Package** package_ptrs = malloc(sizeof(Package*) * count);

    for (int i = 0; i < count; ++i) {
        package_ptrs[i] = malloc(sizeof(Package));
        char filepath[512];
        sprintf(filepath, "%s/%s.json", REPO_FOLDER, package_names[i]);
        if (parse_package_json(filepath, package_ptrs[i]) != 0) {
            printf("Failed to load %s.json\n", package_names[i]);
            free(package_ptrs[i]);
            package_ptrs[i] = NULL;
            threads[i] = NULL;
            continue;
        }
        threads[i] = CreateThread(NULL, 0, download_thread, package_ptrs[i], 0, NULL);
    }

    WaitForMultipleObjects(count, threads, TRUE, INFINITE);

    for (int i = 0; i < count; ++i) {
        if (threads[i]) CloseHandle(threads[i]);
    }

    for (int i = 0; i < count; ++i) {
        if (package_ptrs[i]) {
            packages[i] = *package_ptrs[i];
            free(package_ptrs[i]);
        } else {
            memset(&packages[i], 0, sizeof(Package));
        }
    }
    free(package_ptrs);
    free(threads);

    wait_and_install_packages(count, packages);

    free(packages);
}

int main(int argc, char* argv[]) {
    installed_mutex = CreateMutex(NULL, FALSE, NULL);
    if (!installed_mutex) return 1;
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
        printf("  sk -Q --info <pkg>     [Show installed package info]\n");
        printf("  sk -Ql                 [List all packages in the repo]\n");
        printf("  sk -Ss <pkg>           [Search for package in repo]\n");
        printf("  sk -S <pkg1> [pkg2...] [Install packages]\n");
        printf("  sk -Sy                 [Refresh package list]\n");
        printf("  sk -Si                 [Install from package.json]\n");
        printf("  sk -Sr <url>           [Set repo URL]\n");
        return 0;
    }

    if (strcmp(argv[1], "-Sy") == 0) {
        git_sync(repo_url);
        return 0;
    }

    if (strcmp(argv[1], "-Q") == 0) {
        // List installed packages or show info if args provided
        // ... you can implement this similarly as before
        printf("List/show installed packages functionality not implemented in this snippet.\n");
        return 0;
    }

    if (strcmp(argv[1], "-Si") == 0) {
        // Install from package.json - implement if needed
        printf("Install from package.json functionality not implemented in this snippet.\n");
        return 0;
    }

    if (strcmp(argv[1], "-S") == 0 && argc >= 3) {
        git_sync(repo_url);

        download_and_install_packages(argc - 2, &argv[2]);
        return 0;
    }

    printf("Unknown command. Run without arguments for help.\n");
    return 0;
}
