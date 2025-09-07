// sk.c - Swissknife with cJSON and multithreaded parallel BITS download, sequential install
// Updated to support ~/knives/knives.json and per-knife folders, installed list at ~/package.json

#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include "cJSON.h"
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "urlmon.lib")

#define MAX_KNIFE_NAME 128

HANDLE installed_mutex;
static char KNIVES_FOLDER_PATH[MAX_PATH];
static char KNIVES_CONFIG_PATH[MAX_PATH];
static char INSTALLED_FILE_PATH[MAX_PATH];

typedef struct Package {
    char name[128];
    char id[64];
    char version[64];
    char url[1024];
    char silent[128];
    char type[16];
    char installer[64];
    char out_path[MAX_PATH];
    char untype[32]; // uninstaller type
    char uninstaller[MAX_PATH];
} Package;

char* read_file(const char* filepath) {
    FILE* f = fopen(filepath, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char* buffer = malloc((size_t)size + 1);
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

/* ---- knives.json helpers ----
   knives.json format:
   { "knives": [ { "name": "main", "url": "https://..." }, ... ] }
*/
void ensure_knives_folder_and_config() {
    // KNIVES_FOLDER_PATH and KNIVES_CONFIG_PATH are initialized in main()
    CreateDirectoryA(KNIVES_FOLDER_PATH, NULL);

    // ensure knives.json exists
    FILE* f = fopen(KNIVES_CONFIG_PATH, "r");
    if (!f) {
        f = fopen(KNIVES_CONFIG_PATH, "w");
        if (f) {
            fputs("{\"knives\":[]}", f);
            fclose(f);
        }
    } else {
        fclose(f);
    }
}

void init_knives() {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\knives", getenv("USERPROFILE"));
    CreateDirectory(path, NULL);

    snprintf(path, sizeof(path), "%s\\knives\\knives.json", getenv("USERPROFILE"));
    FILE *f = fopen(path, "r");
    if (!f) {
        f = fopen(path, "w");
        if (f) {
            fputs("{\"repos\":[]}", f);
            fclose(f);
        }
    } else {
        fclose(f);
    }
}

// List knives (-Skl)
void list_knives() {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\knives\\knives.json", getenv("USERPROFILE"));

    FILE *f = fopen(path, "r");
    if (!f) {
        printf("No knives.json found.\n");
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char *data = malloc(size + 1);
    fread(data, 1, size, f);
    data[size] = '\0';
    fclose(f);

    cJSON *json = cJSON_Parse(data);
    free(data);

    if (!json) {
        printf("Invalid knives.json\n");
        return;
    }

    cJSON *repos = cJSON_GetObjectItem(json, "repos");
    if (repos && cJSON_IsArray(repos)) {
        printf("Available knives:\n");
        cJSON *repo;
        cJSON_ArrayForEach(repo, repos) {
            cJSON *name = cJSON_GetObjectItem(repo, "name");
            cJSON *url  = cJSON_GetObjectItem(repo, "url");
            if (name && url)
                printf("- %s: %s\n", name->valuestring, url->valuestring);
        }
    }

    cJSON_Delete(json);
}

void save_knife(const char* name, const char* url) {
    char* data = read_file(KNIVES_CONFIG_PATH);
    cJSON* root = NULL;
    if (data) {
        root = cJSON_Parse(data);
        free(data);
    }
    if (!root) root = cJSON_CreateObject();

    cJSON* knives = cJSON_GetObjectItem(root, "knives");
    if (!knives) {
        knives = cJSON_CreateArray();
        cJSON_AddItemToObject(root, "knives", knives);
    }

    cJSON* knife = NULL;
    cJSON_ArrayForEach(knife, knives) {
        cJSON* nm = cJSON_GetObjectItem(knife, "name");
        if (nm && nm->valuestring && strcmp(nm->valuestring, name) == 0) {
            // replace url
            cJSON_ReplaceItemInObject(knife, "url", cJSON_CreateString(url));
            char* out = cJSON_Print(root);
            write_file(KNIVES_CONFIG_PATH, out);
            free(out);
            cJSON_Delete(root);
            return;
        }
    }

    // add new
    cJSON* new_knife = cJSON_CreateObject();
    cJSON_AddStringToObject(new_knife, "name", name);
    cJSON_AddStringToObject(new_knife, "url", url);
    cJSON_AddItemToArray(knives, new_knife);

    char* out = cJSON_Print(root);
    write_file(KNIVES_CONFIG_PATH, out);
    free(out);
    cJSON_Delete(root);
}

char* get_knife_url(const char* name) {
    char* data = read_file(KNIVES_CONFIG_PATH);
    if (!data) return NULL;
    cJSON* root = cJSON_Parse(data);
    free(data);
    if (!root) return NULL;

    cJSON* knives = cJSON_GetObjectItem(root, "knives");
    if (!knives) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON* knife = NULL;
    char* url_dup = NULL;
    cJSON_ArrayForEach(knife, knives) {
        cJSON* nm = cJSON_GetObjectItem(knife, "name");
        cJSON* u = cJSON_GetObjectItem(knife, "url");
        if (nm && u && nm->valuestring && u->valuestring && strcmp(nm->valuestring, name) == 0) {
            url_dup = _strdup(u->valuestring);
            break;
        }
    }

    cJSON_Delete(root);
    return url_dup;
}

int sync_knife_git_to_folder(const char* knife_name) {
    char *url = get_knife_url(knife_name);
    if (!url) {
        printf("Knife '%s' not found in knives.json\n", knife_name);
        return -1;
    }

    char dest[MAX_PATH];
    snprintf(dest, MAX_PATH, "%s\\%s", KNIVES_FOLDER_PATH, knife_name);

    if (_access(dest, 0) != 0) {
        char cmd[4096];
        snprintf(cmd, sizeof(cmd), "git clone \"%s\" \"%s\"", url, dest);
        int r = system(cmd);
        free(url);
        return r;
    } else {
        char cmd[4096];
        snprintf(cmd, sizeof(cmd), "git -C \"%s\" pull", dest);
        int r = system(cmd);
        free(url);
        return r;
    }
}

/* ---- parse package json from a given path ---- */
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
    if ((item = cJSON_GetObjectItem(root, "uninstaller"))) strncpy(pkg->uninstaller, item->valuestring, sizeof(pkg->uninstaller)-1);
    if ((item = cJSON_GetObjectItem(root, "untype"))) strncpy(pkg->untype, item->valuestring, sizeof(pkg->untype)-1);

    // build out_path
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = ".";
    snprintf(pkg->out_path, MAX_PATH, "%s\\%s.%s", tmp, pkg->id, pkg->type[0] ? pkg->type : "tmp");

    cJSON_Delete(root);
    return 0;
}

int download_file(const char* url, const char* out_path) {
    char cmd[4096];
    // Try aria2c first
    if (system("where aria2c >nul 2>&1") == 0) {
        snprintf(cmd, sizeof(cmd),
                 "aria2c -x 16 -s 16 -k 1M \"%s\" -o \"%s\"",
                 url, out_path);
    } else {
        // Fallback to PowerShell
        snprintf(cmd, sizeof(cmd),
                 "powershell -Command \"Start-BitsTransfer -Source '%s' -Destination '%s'\"",
                 url, out_path);
    }
}

DWORD WINAPI download_thread(LPVOID param) {
    Package* pkg = (Package*)param;
    printf("Downloading %s setup...\n", pkg->name);
    if (download_file(pkg->url, pkg->out_path) != 0) {
        printf("Download failed for %s\n", pkg->name);
    }
    return 0;
}

/* ---- installed packages helpers (~/package.json) ---- */
void add_or_update_installed_package(Package* pkg) {
    WaitForSingleObject(installed_mutex, INFINITE);

    char* content = read_file(INSTALLED_FILE_PATH);
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
        if (id && id->valuestring && strcmp(id->valuestring, pkg->id) == 0) {
            cJSON_DeleteItemFromArray(root, i);
            break;
        }
    }

    cJSON_AddItemToArray(root, updated);
    char* output = cJSON_Print(root);
    write_file(INSTALLED_FILE_PATH, output);
    free(output);
    cJSON_Delete(root);

    ReleaseMutex(installed_mutex);
}

void remove_installed_package(const char* id) {
    WaitForSingleObject(installed_mutex, INFINITE);
    char* content = read_file(INSTALLED_FILE_PATH);
    if (!content) {
        ReleaseMutex(installed_mutex);
        return;
    }

    cJSON* root = cJSON_Parse(content);
    free(content);
    if (!root || !cJSON_IsArray(root)) {
        ReleaseMutex(installed_mutex);
        return;
    }

    int len = cJSON_GetArraySize(root);
    for (int i = 0; i < len; ++i) {
        cJSON* item = cJSON_GetArrayItem(root, i);
        cJSON* id_field = cJSON_GetObjectItem(item, "id");
        if (id_field && id_field->valuestring && strcmp(id_field->valuestring, id) == 0) {
            cJSON_DeleteItemFromArray(root, i);
            break;
        }
    }

    char* updated = cJSON_Print(root);
    write_file(INSTALLED_FILE_PATH, updated);
    free(updated);
    cJSON_Delete(root);
    ReleaseMutex(installed_mutex);
}

/* ---- start menu helper (unchanged) ---- */
void create_start_menu_shortcut(const char *appName, const char *targetPath, const char *workingDir, const char *iconPath) {
    char shortcutPath[MAX_PATH];
    char startMenuPath[MAX_PATH];

    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROGRAMS, NULL, 0, startMenuPath))) {
        snprintf(shortcutPath, MAX_PATH, "%s\\%s.lnk", startMenuPath, appName);

        IShellLinkA* psl;
        HRESULT hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                      &IID_IShellLinkA, (LPVOID*)&psl);

        if (SUCCEEDED(hr)) {
            psl->lpVtbl->SetPath(psl, targetPath);
            if (workingDir) psl->lpVtbl->SetWorkingDirectory(psl, workingDir);
            if (iconPath)   psl->lpVtbl->SetIconLocation(psl, iconPath, 0);

            IPersistFile* ppf;
            hr = psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, (LPVOID*)&ppf);
            if (SUCCEEDED(hr)) {
                WCHAR wsz[MAX_PATH];
                MultiByteToWideChar(CP_ACP, 0, shortcutPath, -1, wsz, MAX_PATH);
                ppf->lpVtbl->Save(ppf, wsz, TRUE);
                ppf->lpVtbl->Release(ppf);
                printf("Shortcut created: %s\n", shortcutPath);
            }
            psl->lpVtbl->Release(psl);
        }
    }
}

/* ---- install/unpack logic (mostly unchanged) ---- */
void wait_and_install_packages(int count, Package packages[]) {
    char userProfile[MAX_PATH];
    SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, userProfile);

    for (int i = 0; i < count; ++i) {
        if (!packages[i].silent[0] && packages[i].installer[0]) {
            if (strcmp(packages[i].installer, "nsis") == 0) strcpy(packages[i].silent, "/S");
            else if (strcmp(packages[i].installer, "inno") == 0) strcpy(packages[i].silent, "/VERYSILENT /SUPPRESSMSGBOXES");
            else if (strcmp(packages[i].installer, "msi") == 0) strcpy(packages[i].silent, "/quiet /norestart");
            else if (strcmp(packages[i].installer, "installshield") == 0) strcpy(packages[i].silent, "/s /v\"/qn\"");
            else if (strcmp(packages[i].installer, "squirrel") == 0) strcpy(packages[i].silent, "--silent");
        }

        size_t len = strlen(packages[i].out_path);
        int is_zip = len > 4 && _stricmp(packages[i].out_path + len - 4, ".zip") == 0;

        if (is_zip) {
            char extract_path[MAX_PATH];
            snprintf(extract_path, MAX_PATH, "%s\\swiss\\%s", userProfile, packages[i].name);

            CreateDirectoryA(extract_path, NULL);

            char cmd[MAX_PATH * 2];
            snprintf(cmd, sizeof(cmd),
                     "powershell -ExecutionPolicy Bypass -Command \"Expand-Archive -Path '%s' -DestinationPath '%s' -Force\"",
                     packages[i].out_path, extract_path);

            printf("Extracting %s...\n", packages[i].name);
            system(cmd);

            HKEY hKey;
            if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS) {
                char oldPath[32767];
                DWORD sizeRp = sizeof(oldPath);
                if (RegQueryValueExA(hKey, "PATH", NULL, NULL, (LPBYTE)oldPath, &sizeRp) != ERROR_SUCCESS) oldPath[0] = 0;

                if (!strstr(oldPath, extract_path)) {
                    char newPath[32767];
                    snprintf(newPath, sizeof(newPath), "%s;%s", oldPath, extract_path);
                    RegSetValueExA(hKey, "PATH", 0, REG_EXPAND_SZ, (const BYTE*)newPath, (DWORD)strlen(newPath) + 1);
                    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)"Environment", SMTO_ABORTIFHUNG, 5000, NULL);
                }
                RegCloseKey(hKey);
            }

            add_or_update_installed_package(&packages[i]);
            printf("%s extracted and PATH updated.\n", packages[i].name);

            char exePath[MAX_PATH];
            snprintf(exePath, MAX_PATH, "%s\\%s.exe", extract_path, packages[i].name);

            create_start_menu_shortcut(packages[i].name, exePath, extract_path, exePath);
            continue;
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

/* ---- helper: build package file path from "knife/id" or "id" (falls back to "main") ---- */
void build_package_filepath(const char* spec, char* out_path, size_t out_len) {
    // spec: either "knife/id" or "id"
    const char* slash = strchr(spec, '/');
    char knife[MAX_KNIFE_NAME] = {0};
    char id[256] = {0};
    if (slash) {
        size_t klen = (size_t)(slash - spec);
        if (klen >= sizeof(knife)) klen = sizeof(knife)-1;
        strncpy(knife, spec, klen);
        knife[klen] = '\0';
        strncpy(id, slash + 1, sizeof(id)-1);
    } else {
        // default knife 'main'
        strcpy(knife, "main");
        strncpy(id, spec, sizeof(id)-1);
    }

    snprintf(out_path, out_len, "%s\\%s\\%s.json", KNIVES_FOLDER_PATH, knife, id);
}

/* ---- download_and_install_packages updated to use build_package_filepath ---- */
void download_and_install_packages(int count, char* package_names[]) {
    Package* packages = malloc(sizeof(Package) * count);
    HANDLE* threads = malloc(sizeof(HANDLE) * count);
    Package** package_ptrs = malloc(sizeof(Package*) * count);

    int created_threads = 0;

    for (int i = 0; i < count; ++i) {
        package_ptrs[i] = malloc(sizeof(Package));
        char filepath[MAX_PATH];
        build_package_filepath(package_names[i], filepath, sizeof(filepath));

        if (parse_package_json(filepath, package_ptrs[i]) != 0) {
            printf("Failed to load %s.json (looked at %s)\n", package_names[i], filepath);
            free(package_ptrs[i]);
            package_ptrs[i] = NULL;
            threads[i] = NULL;
            continue;
        }
        threads[i] = CreateThread(NULL, 0, download_thread, package_ptrs[i], 0, NULL);
        created_threads++;
    }

    if (created_threads > 0) {
        // Wait only for created threads
        WaitForMultipleObjects(count, threads, TRUE, INFINITE);
    }

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

/* ---- list packages across knives (lists all JSON under each knife folder) ---- */
void list_packages() {
    struct _finddata_t file;
    intptr_t hFind;
    char search[MAX_PATH];

    snprintf(search, MAX_PATH, "%s\\*\\*.json", KNIVES_FOLDER_PATH); // all knives
    hFind = _findfirst(search, &file);
    if (hFind == -1L) {
        printf("No packages found.\n");
        return;
    }

    printf("Available packages:\n");
    do {
        char fullpath[MAX_PATH];
        snprintf(fullpath, MAX_PATH, "%s\\%s", KNIVES_FOLDER_PATH, file.name); // file.name contains "knife\\file.json"
        // But the _findfirst with pattern above returns filenames with subdir\file.json on Windows MSVCRT? To keep safe we can reconstruct:
        // file.name might be "knife\\id.json" or just "id.json" depending on libc; so parse name:
        char *p = strchr(file.name, '\\');
        if (!p) continue;
        char knife[MAX_KNIFE_NAME] = {0};
        strncpy(knife, file.name, (size_t)(p - file.name));
        char idfile[256];
        strncpy(idfile, p + 1, sizeof(idfile)-1);

        char path2[MAX_PATH];
        snprintf(path2, MAX_PATH, "%s\\%s\\%s", KNIVES_FOLDER_PATH, knife, idfile);
        char* content = read_file(path2);
        if (!content) continue;
        cJSON* root = cJSON_Parse(content);
        free(content);
        if (!root) continue;
        const cJSON* name = cJSON_GetObjectItem(root, "name");
        const cJSON* id = cJSON_GetObjectItem(root, "id");
        if (name && id) {
            printf(" - %s (%s) [%s]\n", name->valuestring, id->valuestring, knife);
        }
        cJSON_Delete(root);
    } while (_findnext(hFind, &file) == 0);

    _findclose(hFind);
}

/* ---- installed list helpers (list & check updates) ---- */
void list_installed_packages() {
    char* content = read_file(INSTALLED_FILE_PATH);
    if (!content) {
        printf("No packages installed.\n");
        return;
    }

    cJSON* root = cJSON_Parse(content);
    free(content);
    if (!root || !cJSON_IsArray(root)) {
        printf("Invalid installed package format.\n");
        cJSON_Delete(root);
        return;
    }

    printf("Installed packages:\n");
    int len = cJSON_GetArraySize(root);
    for (int i = 0; i < len; ++i) {
        cJSON* item = cJSON_GetArrayItem(root, i);
        if (!item) continue;
        const cJSON* name = cJSON_GetObjectItem(item, "name");
        const cJSON* id = cJSON_GetObjectItem(item, "id");
        if (name && id) {
            printf("  %s (%s)\n", name->valuestring, id->valuestring);
        }
    }

    cJSON_Delete(root);
}

void check_updates() {
    char* content = read_file(INSTALLED_FILE_PATH);
    if (!content) {
        printf("No installed packages recorded.\n");
        return;
    }
    cJSON* root = cJSON_Parse(content);
    free(content);
    if (!root || !cJSON_IsArray(root)) {
        printf("Invalid installed file format.\n");
        cJSON_Delete(root);
        return;
    }

    int len = cJSON_GetArraySize(root);
    for (int i = 0; i < len; ++i) {
        cJSON* item = cJSON_GetArrayItem(root, i);
        const cJSON* id = cJSON_GetObjectItem(item, "id");
        const cJSON* name = cJSON_GetObjectItem(item, "name");
        const cJSON* version_installed = cJSON_GetObjectItem(item, "version");
        if (!id || !cJSON_IsString(id)) continue;

        // Look across knives for the package ID
        // naive scan: try each knife folder and check if <id>.json present
        struct _finddata_t f;
        char search[MAX_PATH];
        snprintf(search, MAX_PATH, "%s\\*\\%s.json", KNIVES_FOLDER_PATH, id->valuestring);
        intptr_t h = _findfirst(search, &f);
        if (h == -1L) continue;
        do {
            // Found a matching package file; read its version
            char path[MAX_PATH];
            // f.name could be "knife\\id.json" depending on pattern; try to reconstruct
            // simpler: iterate knives.json to find which knife has file
            // to keep code short: build path from the found filename by splitting
            char *p = strchr(f.name, '\\');
            if (!p) continue;
            char knife[MAX_KNIFE_NAME] = {0};
            strncpy(knife, f.name, (size_t)(p - f.name));
            snprintf(path, MAX_PATH, "%s\\%s\\%s", KNIVES_FOLDER_PATH, knife, f.name + (p - f.name + 1));
            char* pkgdata = read_file(path);
            if (!pkgdata) continue;
            cJSON* pkgroot = cJSON_Parse(pkgdata);
            free(pkgdata);
            if (!pkgroot) continue;
            cJSON* version_repo = cJSON_GetObjectItem(pkgroot, "version");
            if (version_repo && version_installed && strcmp(version_installed->valuestring, version_repo->valuestring) != 0) {
                printf("Update available: %s (%s → %s)\n", name ? name->valuestring : id->valuestring,
                       version_installed ? version_installed->valuestring : "unknown",
                       version_repo->valuestring);
            }
            cJSON_Delete(pkgroot);
        } while (_findnext(h, &f) == 0);
        _findclose(h);
    }
    cJSON_Delete(root);
}

/* ---- install from installed file (reinstall all) ---- */
void install_from_package_json() {
    char* content = read_file(INSTALLED_FILE_PATH);
    if (!content) {
        printf("No package.json found.\n");
        return;
    }

    cJSON* root = cJSON_Parse(content);
    free(content);
    if (!root) {
        printf("Invalid package.json format.\n");
        return;
    }

    int count = cJSON_GetArraySize(root);
    if (count == 0) {
        printf("No packages listed in package.json.\n");
        cJSON_Delete(root);
        return;
    }

    Package* packages = malloc(sizeof(Package) * count);
    HANDLE* threads = malloc(sizeof(HANDLE) * count);
    int actual_count = 0;

    for (int i = 0; i < count; ++i) {
        cJSON* item = cJSON_GetArrayItem(root, i);
        cJSON* id = cJSON_GetObjectItem(item, "id");
        if (!id || !cJSON_IsString(id)) continue;

        char filepath[MAX_PATH];
        // assume id stored as "knife/id" or just "id" in installed list; we will try knife 'main' by default
        build_package_filepath(id->valuestring, filepath, sizeof(filepath));
        if (parse_package_json(filepath, &packages[actual_count]) != 0) {
            printf("Failed to load %s.json\n", id->valuestring);
            continue;
        }

        threads[actual_count] = CreateThread(NULL, 0, download_thread, &packages[actual_count], 0, NULL);
        actual_count++;
    }

    if (actual_count > 0) {
        WaitForMultipleObjects(actual_count, threads, TRUE, INFINITE);
        for (int i = 0; i < actual_count; ++i) {
            if (threads[i]) CloseHandle(threads[i]);
        }
        wait_and_install_packages(actual_count, packages);
    }

    free(threads);
    free(packages);
    cJSON_Delete(root);
}

/* ---- main() ---- */
int main(int argc, char* argv[]) {
    installed_mutex = CreateMutex(NULL, FALSE, NULL);
    if (!installed_mutex) return 1;

    CoInitialize(NULL);

    // Build paths based on %USERPROFILE%
    char userProfile[MAX_PATH];
    if (FAILED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, userProfile))) {
        // fallback to env
        char* up = getenv("USERPROFILE");
        if (up) strncpy(userProfile, up, MAX_PATH-1);
        else strcpy(userProfile, ".");
    }

    snprintf(KNIVES_FOLDER_PATH, MAX_PATH, "%s\\knives", userProfile);
    snprintf(KNIVES_CONFIG_PATH, MAX_PATH, "%s\\knives.json", KNIVES_FOLDER_PATH);
    snprintf(INSTALLED_FILE_PATH, MAX_PATH, "%s\\package.json", userProfile);

    ensure_knives_folder_and_config();

    if (argc >= 2 && strcmp(argv[1], "-Sr") == 0 && argc == 4) {  // Add/update knife
        save_knife(argv[2], argv[3]);  // name, url
        printf("Knife '%s' saved with URL: %s\n", argv[2], argv[3]);
        return 0;
    }

    if (argc >= 2 && strcmp(argv[1], "-Sy") == 0 && argc == 3) {  // Sync a knife
        int r = sync_knife_git_to_folder(argv[2]);
        if (r == 0) printf("Synced knife '%s'\n", argv[2]);
        return 0;
    }

    if (argc < 2) {
        printf("Usage:\n");
        printf("  sk -Qi                 [List installed packages]\n");
        printf("  sk -Q --info <pkg>     [Show installed package info]\n");
        printf("  sk -Ql                 [List all packages in the knives folders]\n");
        printf("  sk -Ss <pkg>           [Search for package in knives]\n");
        printf("  sk -S <knife>/<pkg> [pkg2...] [Install packages]\n");
        printf("  sk -Sy <knife>         [Refresh package list for knife]\n");
        printf("  sk -Si                 [Install from %s]\n", INSTALLED_FILE_PATH);
        printf("  sk -Sr <name> <url>    [Add/update knife]\n");
        return 0;
    }

    if (strcmp(argv[1], "-Sy") == 0 && argc == 2) {
        // sync default 'main' if present
        char* url = get_knife_url("main");
        if (!url) {
            printf("Default knife 'main' not configured. Add it with -Sr main <url>\n");
        } else {
            sync_knife_git_to_folder("main");
            free(url);
        }
        return 0;
    }

    if (strcmp(argv[1], "-R") == 0 && argc >= 3) {
        // Uninstall by id (we need to locate package file first)
        char spec[256];
        strncpy(spec, argv[2], sizeof(spec)-1);
        char pkgpath[MAX_PATH];
        build_package_filepath(spec, pkgpath, sizeof(pkgpath));
        Package pkg;
        if (parse_package_json(pkgpath, &pkg) != 0) {
            printf("Failed to load package info for %s\n", argv[2]);
            return 1;
        }

        if (strlen(pkg.uninstaller) == 0) {
            printf("No uninstaller path defined for %s\n", pkg.name);
            return 1;
        }

        char uninstall_flags[256] = "";
        if (strcmp(pkg.untype, "nsis") == 0) strcpy(uninstall_flags, "/S");
        else if (strcmp(pkg.untype, "inno") == 0) strcpy(uninstall_flags, "/VERYSILENT /SUPPRESSMSGBOXES");
        else if (strcmp(pkg.untype, "msi") == 0) strcpy(uninstall_flags, "/quiet /norestart");
        else if (strcmp(pkg.untype, "installshield") == 0) strcpy(uninstall_flags, "/s /x /v\"/qn\"");
        else if (strcmp(pkg.untype, "squirrel") == 0) strcpy(uninstall_flags, "--silent");
        else if (strcmp(pkg.untype, "exe") == 0) strcpy(uninstall_flags, "/S");

        printf("Uninstalling %s...\n", pkg.name);
        SHELLEXECUTEINFOA sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpFile = pkg.uninstaller;
        sei.lpParameters = strlen(uninstall_flags) ? uninstall_flags : NULL;
        sei.lpVerb = "open";
        sei.nShow = strlen(uninstall_flags) ? SW_HIDE : SW_SHOWNORMAL;

        if (ShellExecuteExA(&sei)) {
            WaitForSingleObject(sei.hProcess, INFINITE);
            CloseHandle(sei.hProcess);
            remove_installed_package(pkg.id);
            printf("%s uninstalled and removed from records.\n", pkg.name);
        } else {
            printf("Failed to run uninstaller for %s\n", pkg.name);
        }
        return 0;
    }

    if (strcmp(argv[1], "-Q") == 0) {
        list_installed_packages();
        return 0;
    }
    if (strcmp(argv[1], "-Su") == 0) {
        check_updates();
        return 0;
    }
    if (strcmp(argv[1], "-Si") == 0) {
        install_from_package_json();
        return 0;
    }
    if (strcmp(argv[1], "-Ql") == 0) {
        list_packages();
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "-Skl") == 0) {
    list_knives();
    return 0;
    }

    if (strcmp(argv[1], "-S") == 0 && argc >= 3) {
        // For each package arg, ensure the knife is synced first if it references a knife
        for (int i = 2; i < argc; ++i) {
            // if arg contains knife/name then sync that knife
            char* slash = strchr(argv[i], '/');
            if (slash) {
                char knife_name[MAX_KNIFE_NAME] = {0};
                size_t len = (size_t)(slash - argv[i]);
                if (len >= sizeof(knife_name)) len = sizeof(knife_name)-1;
                strncpy(knife_name, argv[i], len);
                knife_name[len] = '\0';
                sync_knife_git_to_folder(knife_name);
            } else {
                // ensure default main is present and synced
                // optional: we can auto sync main, but keep it optional:
                // sync_knife_git_to_folder("main");
            }
        }

        download_and_install_packages(argc - 2, &argv[2]);
        return 0;
    }

    CoUninitialize();
    printf("Unknown command. Run without arguments for help.\n");
    return 0;
}
