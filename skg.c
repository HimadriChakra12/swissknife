// sk_master.c - Swissknife full master code with GUI installer + CLI

#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <stdbool.h>
#include <process.h>
#include "cJSON.h"
#include <commctrl.h>

#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "shlwapi.lib")

#define REPO_FOLDER "C:/farm/wheats/Swissknife/knives"
#define CONFIG_FILE "C:/farm/wheats/Swissknife/.pkgconfig"
#define INSTALLED_FILE "C:/farm/wheats/Swissknife/package.json"
#define LOG_FILE "C:/farm/wheats/Swissknife/install.log"
#define MAX_PACKAGES 64

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

// Globals for GUI and thread communication
static HWND hwndMain = NULL;
static HWND hwndList = NULL;
static HWND hwndCancelBtn = NULL;
static HWND hwndProgress[MAX_PACKAGES];
static HANDLE cancel_event = NULL;
static HANDLE download_threads[MAX_PACKAGES];
static Package packages[MAX_PACKAGES];
static int package_count = 0;

// Mutex for installed.json updates
static HANDLE installed_mutex = NULL;

// Forward declarations
char* read_file(const char* filepath);
int write_file(const char* filepath, const char* data);
void save_repo_url(const char* url);
void read_repo_url(char* buffer, size_t size);
void git_sync(const char* repo_url);
int parse_package_json(const char* filepath, Package* pkg);
int download_file_powershell(const char* url, const char* out_path, int index);
DWORD WINAPI download_thread(LPVOID param);
void add_or_update_installed_package(Package* pkg);
void wait_and_install_packages(int count, Package packages[]);
void check_updates();
void install_from_package_json();
void list_packages();
void list_installed_packages();
void update_status_list(int index, const char* status);
void append_log(const char* text);
void cancel_install();
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
DWORD WINAPI gui_thread(LPVOID param);
int run_installation_gui(int count, Package pkgs[]);

// --- Implementation ---

char* read_file(const char* filepath) {
    FILE* f = fopen(filepath, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char* buffer = (char*)malloc(size + 1);
    if (!buffer) { fclose(f); return NULL; }
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
        if (fgets(buffer, (int)size, f)) {
            buffer[strcspn(buffer, "\r\n")] = 0;
        }
        fclose(f);
    }
    if (buffer[0] == 0) {
        printf("Enter your Git repository URL: ");
        if (fgets(buffer, (int)size, stdin)) {
            buffer[strcspn(buffer, "\r\n")] = 0;
            save_repo_url(buffer);
        }
    }
}

void git_sync(const char* repo_url) {
    if (_access(REPO_FOLDER, 0) != 0) {
        char cmd[2048];
        sprintf(cmd, "git clone %s %s", repo_url, REPO_FOLDER);
        system(cmd);
    } else {
        char cmd[2048];
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

    memset(pkg, 0, sizeof(Package));
    cJSON* item = NULL;
    if ((item = cJSON_GetObjectItem(root, "name"))) strncpy(pkg->name, item->valuestring, sizeof(pkg->name) - 1);
    if ((item = cJSON_GetObjectItem(root, "id"))) strncpy(pkg->id, item->valuestring, sizeof(pkg->id) - 1);
    if ((item = cJSON_GetObjectItem(root, "version"))) strncpy(pkg->version, item->valuestring, sizeof(pkg->version) - 1);
    if ((item = cJSON_GetObjectItem(root, "url"))) strncpy(pkg->url, item->valuestring, sizeof(pkg->url) - 1);
    if ((item = cJSON_GetObjectItem(root, "silent"))) strncpy(pkg->silent, item->valuestring, sizeof(pkg->silent) - 1);
    if ((item = cJSON_GetObjectItem(root, "type"))) strncpy(pkg->type, item->valuestring, sizeof(pkg->type) - 1);
    if ((item = cJSON_GetObjectItem(root, "installer"))) strncpy(pkg->installer, item->valuestring, sizeof(pkg->installer) - 1);

    snprintf(pkg->out_path, MAX_PATH, "%s\\%s.%s", getenv("TEMP"), pkg->id, pkg->type);
    cJSON_Delete(root);
    return 0;
}

int download_file_powershell(const char* url, const char* out_path, int index) {
    // Use PowerShell Start-BitsTransfer command and return success/failure
    // index used for progress update (if needed)
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "powershell -WindowStyle Hidden -Command \"Start-BitsTransfer -Source '%s' -Destination '%s'\"", url, out_path);
    int res = system(cmd);
    return res;
}

DWORD WINAPI download_thread(LPVOID param) {
    int idx = *(int*)param;
    Package* pkg = &packages[idx];
    if (WaitForSingleObject(cancel_event, 0) == WAIT_OBJECT_0) return 0; // cancelled

    update_status_list(idx, "Downloading...");
    int res = download_file_powershell(pkg->url, pkg->out_path, idx);
    if (res != 0) {
        update_status_list(idx, "Download Failed");
    } else {
        update_status_list(idx, "Downloaded");
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

    // Remove existing package with same id
    int len = cJSON_GetArraySize(root);
    for (int i = 0; i < len; ++i) {
        cJSON* item = cJSON_GetArrayItem(root, i);
        cJSON* id = cJSON_GetObjectItem(item, "id");
        if (id && strcmp(id->valuestring, pkg->id) == 0) {
            cJSON_DeleteItemFromArray(root, i);
            break;
        }
    }

    cJSON* new_pkg = cJSON_CreateObject();
    cJSON_AddStringToObject(new_pkg, "name", pkg->name);
    cJSON_AddStringToObject(new_pkg, "id", pkg->id);
    cJSON_AddStringToObject(new_pkg, "version", pkg->version);

    cJSON_AddItemToArray(root, new_pkg);
    char* output = cJSON_Print(root);
    write_file(INSTALLED_FILE, output);
    free(output);
    cJSON_Delete(root);
    ReleaseMutex(installed_mutex);
}

void wait_and_install_packages(int count, Package packages[]) {
    for (int i = 0; i < count; ++i) {
        if (WaitForSingleObject(cancel_event, 0) == WAIT_OBJECT_0) {
            update_status_list(i, "Cancelled");
            return;
        }

        update_status_list(i, "Installing...");
        if (packages[i].silent[0] == 0 && packages[i].installer[0] != 0) {
            // Default silent flags for known installers
            if (strcmp(packages[i].installer, "nsis") == 0) strcpy(packages[i].silent, "/S");
            else if (strcmp(packages[i].installer, "inno") == 0) strcpy(packages[i].silent, "/VERYSILENT /SUPPRESSMSGBOXES");
            else if (strcmp(packages[i].installer, "msi") == 0) strcpy(packages[i].silent, "/quiet /norestart");
            else if (strcmp(packages[i].installer, "installshield") == 0) strcpy(packages[i].silent, "/s /v\"/qn\"");
            else if (strcmp(packages[i].installer, "squirrel") == 0) strcpy(packages[i].silent, "--silent");
        }

        SHELLEXECUTEINFOA sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpFile = packages[i].out_path;
        sei.lpParameters = (packages[i].silent[0] != 0) ? packages[i].silent : NULL;
        sei.nShow = (packages[i].silent[0] != 0) ? SW_HIDE : SW_SHOWNORMAL;
        sei.lpVerb = "open";

        if (ShellExecuteExA(&sei)) {
            WaitForSingleObject(sei.hProcess, INFINITE);
            CloseHandle(sei.hProcess);
            update_status_list(i, "Installed");
            append_log(packages[i].name);
            add_or_update_installed_package(&packages[i]);
        } else {
            update_status_list(i, "Install Failed");
        }
    }
}

void append_log(const char* text) {
    FILE* f = fopen(LOG_FILE, "a");
    if (f) {
        fprintf(f, "%s\n", text);
        fclose(f);
    }
}

void update_status_list(int index, const char* status) {
    if (!hwndList) return;
    // Get original string (name)
    TCHAR buf[512] = {0};
    SendMessage(hwndList, LB_GETTEXT, (WPARAM)index, (LPARAM)buf);

    // Strip any existing "[...]" suffix
    char* orig = strstr(buf, " [");
    if (orig) *orig = 0;

    char newentry[512];
    snprintf(newentry, sizeof(newentry), "%s [%s]", buf, status);

    SendMessage(hwndList, LB_DELETESTRING, (WPARAM)index, 0);
    SendMessage(hwndList, LB_INSERTSTRING, (WPARAM)index, (LPARAM)newentry);
}

void cancel_install() {
    if (cancel_event) {
        SetEvent(cancel_event);
    }
}

// --- GUI code ---

// Add this above WndProc if not already declared:
static HWND hwndOverallProgress = NULL;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HFONT hFont = CreateFont(
                -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

            // Group box
            CreateWindow("BUTTON", " Package Installation Status ", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                5, 5, 470, 280, hwnd, NULL, GetModuleHandle(NULL), NULL);

            hwndList = CreateWindow("LISTBOX", NULL,
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                15, 30, 450, 180,
                hwnd, (HMENU)1, GetModuleHandle(NULL), NULL);
            SendMessage(hwndList, WM_SETFONT, (WPARAM)hFont, TRUE);

            hwndOverallProgress = CreateWindowEx(0, PROGRESS_CLASS, NULL,
                WS_CHILD | WS_VISIBLE,
                15, 220, 450, 20,
                hwnd, NULL, GetModuleHandle(NULL), NULL);
            SendMessage(hwndOverallProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessage(hwndOverallProgress, PBM_SETPOS, 0, 0);

            hwndCancelBtn = CreateWindow("BUTTON", "Cancel",
                WS_CHILD | WS_VISIBLE,
                370, 250, 95, 30,
                hwnd, (HMENU)2, GetModuleHandle(NULL), NULL);
            SendMessage(hwndCancelBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

            // Per-package progress bars below the group box
            for (int i = 0; i < MAX_PACKAGES; i++) {
                hwndProgress[i] = CreateWindowEx(0, PROGRESS_CLASS, NULL,
                    WS_CHILD | WS_VISIBLE,
                    15, 300 + i * 18, 450, 15,
                    hwnd, NULL, GetModuleHandle(NULL), NULL);
                SendMessage(hwndProgress[i], PBM_SETRANGE, 0, MAKELPARAM(0, 100));
                SendMessage(hwndProgress[i], PBM_SETPOS, 0, 0);
                ShowWindow(hwndProgress[i], SW_HIDE);
            }
            break;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == 2) { // Cancel button pressed
                cancel_install();
                MessageBox(hwnd, "Installation cancelled.", "Swissknife", MB_OK | MB_ICONINFORMATION);
            }
            break;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}


DWORD WINAPI gui_thread(LPVOID param) {
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "SwissknifeWindowClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);

    RegisterClass(&wc);

    hwndMain = CreateWindow(wc.lpszClassName, "Swissknife Installer",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT, 510, 650,
            NULL, NULL, wc.hInstance, NULL);

    ShowWindow(hwndMain, SW_SHOW);
    UpdateWindow(hwndMain);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

int run_installation_gui(int count, Package pkgs[]) {
    cancel_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!cancel_event) {
        printf("Failed to create cancel event.\n");
        return -1;
    }

    // Copy packages globally
    package_count = count;
    memcpy(packages, pkgs, sizeof(Package) * count);

    // Create GUI thread
    HANDLE gui = CreateThread(NULL, 0, gui_thread, NULL, 0, NULL);
    if (!gui) {
        printf("Failed to create GUI thread.\n");
        CloseHandle(cancel_event);
        return -1;
    }

    // Wait a bit for window to create
    Sleep(500);

    // Add package names to listbox and show progress bars
    for (int i = 0; i < package_count; i++) {
        SendMessage(hwndList, LB_ADDSTRING, 0, (LPARAM)packages[i].name);
        ShowWindow(hwndProgress[i], SW_SHOW);
        SendMessage(hwndProgress[i], PBM_SETPOS, 0, 0);
    }

    // Start download threads
    int indices[MAX_PACKAGES];
    for (int i = 0; i < package_count; i++) {
        indices[i] = i;
        download_threads[i] = CreateThread(NULL, 0, download_thread, &indices[i], 0, NULL);
        if (!download_threads[i]) {
            update_status_list(i, "Failed to start download thread");
        }
    }

    // Wait all downloads
    WaitForMultipleObjects(package_count, download_threads, TRUE, INFINITE);
    for (int i = 0; i < package_count; i++) {
        CloseHandle(download_threads[i]);
    }

    // Sequentially install packages
    wait_and_install_packages(package_count, packages);

    // Show done message
    MessageBox(hwndMain, "All done!", "Swissknife", MB_OK | MB_ICONINFORMATION);

    // Close GUI
    PostMessage(hwndMain, WM_CLOSE, 0, 0);

    WaitForSingleObject(gui, INFINITE);
    CloseHandle(gui);
    CloseHandle(cancel_event);
    cancel_event = NULL;

    return 0;
}

// --- CLI Helper functions ---

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
        char* content = read_file(fullpath);
        if (!content) continue;

        cJSON* root = cJSON_Parse(content);
        free(content);
        if (!root) continue;

        cJSON* name = cJSON_GetObjectItem(root, "name");
        cJSON* id = cJSON_GetObjectItem(root, "id");

        if (name && id && cJSON_IsString(name) && cJSON_IsString(id)) {
            printf(" - %s (%s)\n", name->valuestring, id->valuestring);
        }
        cJSON_Delete(root);
    } while (_findnext(hFile, &file) == 0);
    _findclose(hFile);
}

void list_installed_packages() {
    char* content = read_file(INSTALLED_FILE);
    if (!content) {
        printf("No packages installed.\n");
        return;
    }

    cJSON* root = cJSON_Parse(content);
    free(content);
    if (!root || !cJSON_IsArray(root)) {
        printf("Invalid installed package format.\n");
        return;
    }

    printf("Installed packages:\n");
    int count = cJSON_GetArraySize(root);
    for (int i = 0; i < count; i++) {
        cJSON* pkg = cJSON_GetArrayItem(root, i);
        cJSON* name = cJSON_GetObjectItem(pkg, "name");
        cJSON* version = cJSON_GetObjectItem(pkg, "version");
        if (name && version)
            printf(" - %s (%s)\n", name->valuestring, version->valuestring);
    }
    cJSON_Delete(root);
}

void search_package(const char* query) {
    struct _finddata_t file;
    intptr_t hFile;
    char path[512];
    sprintf(path, "%s/*.json", REPO_FOLDER);

    hFile = _findfirst(path, &file);
    if (hFile == -1L) {
        printf("No packages found.\n");
        return;
    }

    printf("Search results for \"%s\":\n", query);
    bool found = false;
    do {
        char fullpath[512];
        sprintf(fullpath, "%s/%s", REPO_FOLDER, file.name);
        char* content = read_file(fullpath);
        if (!content) continue;

        cJSON* root = cJSON_Parse(content);
        free(content);
        if (!root) continue;

        cJSON* name = cJSON_GetObjectItem(root, "name");
        cJSON* id = cJSON_GetObjectItem(root, "id");

        if (name && id && cJSON_IsString(name) && cJSON_IsString(id)) {
            if (strstr(name->valuestring, query) || strstr(id->valuestring, query)) {
                printf(" - %s (%s)\n", name->valuestring, id->valuestring);
                found = true;
            }
        }
        cJSON_Delete(root);
    } while (_findnext(hFile, &file) == 0);
    _findclose(hFile);

    if (!found) {
        printf("No matching packages found.\n");
    }
}

void install_from_package_json() {
    char* content = read_file(INSTALLED_FILE);
    if (!content) {
        printf("No installed package JSON found.\n");
        return;
    }

    cJSON* root = cJSON_Parse(content);
    free(content);
    if (!root || !cJSON_IsArray(root)) {
        printf("Invalid installed package JSON.\n");
        return;
    }

    int count = cJSON_GetArraySize(root);
    if (count == 0) {
        printf("No packages listed in installed JSON.\n");
        cJSON_Delete(root);
        return;
    }

    Package pkgs[MAX_PACKAGES];
    int valid = 0;
    for (int i = 0; i < count && valid < MAX_PACKAGES; i++) {
        cJSON* pkg = cJSON_GetArrayItem(root, i);
        cJSON* id = cJSON_GetObjectItem(pkg, "id");
        if (!id || !cJSON_IsString(id)) continue;

        char path[512];
        sprintf(path, "%s/%s.json", REPO_FOLDER, id->valuestring);
        if (parse_package_json(path, &pkgs[valid]) == 0) {
            valid++;
        }
    }
    cJSON_Delete(root);

    if (valid > 0) {
        printf("Installing %d packages from package.json...\n", valid);
        run_installation_gui(valid, pkgs);
    } else {
        printf("No valid packages found to install.\n");
    }
}

void check_updates() {
    char* installed_content = read_file(INSTALLED_FILE);
    if (!installed_content) {
        printf("No installed package JSON found.\n");
        return;
    }
    cJSON* installed_root = cJSON_Parse(installed_content);
    free(installed_content);
    if (!installed_root || !cJSON_IsArray(installed_root)) {
        printf("Invalid installed package JSON.\n");
        return;
    }

    int count = cJSON_GetArraySize(installed_root);
    if (count == 0) {
        printf("No installed packages.\n");
        cJSON_Delete(installed_root);
        return;
    }

    bool update_found = false;
    printf("Checking for updates...\n");

    for (int i = 0; i < count; i++) {
        cJSON* pkg = cJSON_GetArrayItem(installed_root, i);
        cJSON* id = cJSON_GetObjectItem(pkg, "id");
        cJSON* version = cJSON_GetObjectItem(pkg, "version");
        if (!id || !version) continue;

        char path[512];
        sprintf(path, "%s/%s.json", REPO_FOLDER, id->valuestring);
        Package repo_pkg;
        if (parse_package_json(path, &repo_pkg) == 0) {
            if (strcmp(repo_pkg.version, version->valuestring) != 0) {
                printf("Update available for %s: installed %s, repo %s\n",
                    repo_pkg.name, version->valuestring, repo_pkg.version);
                update_found = true;
            }
        }
    }

    if (!update_found) {
        printf("All packages up to date.\n");
    }

    cJSON_Delete(installed_root);
}

// --- Main install function (CLI entry point for -S) ---

void install_packages(int count, char* package_names[]) {
    Package pkgs[MAX_PACKAGES];
    int valid = 0;

    for (int i = 0; i < count; i++) {
        char path[512];
        sprintf(path, "%s/%s.json", REPO_FOLDER, package_names[i]);
        if (parse_package_json(path, &pkgs[valid]) == 0) {
            valid++;
        } else {
            printf("Failed to load package info for %s\n", package_names[i]);
        }
    }

    if (valid == 0) {
        printf("No valid packages to install.\n");
        return;
    }

    run_installation_gui(valid, pkgs);
}

int main(int argc, char* argv[]) {
    installed_mutex = CreateMutex(NULL, FALSE, NULL);

    if (argc < 2) {
        printf("Swissknife usage:\n");
        printf(" -Q           : List installed packages\n");
        printf(" -Ql          : List all packages in repo\n");
        printf(" -Ss <query>  : Search package by name/id\n");
        printf(" -S <pkg...>  : Install packages\n");
        printf(" -Sy          : Sync repo (git clone/pull)\n");
        printf(" -Si          : Install packages from installed package.json\n");
        printf(" -Sr <url>    : Set repo Git URL\n");
        printf(" -Su          : Check for updates\n");
        return 1;
    }

    if (strcmp(argv[1], "-Q") == 0) {
        list_installed_packages();
    } else if (strcmp(argv[1], "-Ql") == 0) {
        list_packages();
    } else if (strcmp(argv[1], "-Ss") == 0) {
        if (argc < 3) {
            printf("Specify search query.\n");
            return 1;
        }
        search_package(argv[2]);
    } else if (strcmp(argv[1], "-S") == 0) {
        if (argc < 3) {
            printf("Specify at least one package to install.\n");
            return 1;
        }
        install_packages(argc - 2, &argv[2]);
    } else if (strcmp(argv[1], "-Sy") == 0) {
        char repo_url[1024] = {0};
        read_repo_url(repo_url, sizeof(repo_url));
        if (repo_url[0] == 0) {
            printf("No repository URL set.\n");
            return 1;
        }
        git_sync(repo_url);
    } else if (strcmp(argv[1], "-Si") == 0) {
        install_from_package_json();
    } else if (strcmp(argv[1], "-Sr") == 0) {
        if (argc < 3) {
            printf("Specify repo URL.\n");
            return 1;
        }
        save_repo_url(argv[2]);
        printf("Repo URL saved.\n");
    } else if (strcmp(argv[1], "-Su") == 0) {
        check_updates();
    } else {
        printf("Unknown command.\n");
    }

    if (installed_mutex) CloseHandle(installed_mutex);
    return 0;
}
