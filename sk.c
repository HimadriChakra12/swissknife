#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Make sure these two lines are present and spelled correctly:
#include <io.h>
#include <stdint.h>
// And ensure this line is NOT present if you removed it previously:
// #include <direct.h>


#define REPO_FOLDER "C:/farm/wheats/Swissknife/knives"
#define CONFIG_FILE "C:/farm/wheats/Swissknife/.pkgconfig"

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
        buffer[strcspn(buffer, "\n")] = 0; // remove newline
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

void install_package(const char* pkg_name) {
    char filepath[512];
    sprintf(filepath, "%s/%s.json", REPO_FOLDER, pkg_name);

    FILE* fp = fopen(filepath, "r");
    if (!fp) {
        printf("Package '%s' not found.\n", pkg_name);
        return;
    }

    char url[1024] = {0}, silent[128] = {0}, id[64] = {0}, type[8] = {0};
    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "\"url\"")) sscanf(line, " \"url\" : \"%[^\"]\"", url);
        else if (strstr(line, "\"silent\"")) sscanf(line, " \"silent\" : \"%[^\"]\"", silent);
        else if (strstr(line, "\"id\"")) sscanf(line, " \"id\" : \"%[^\"]\"", id);
        else if (strstr(line, "\"type\"")) sscanf(line, " \"type\" : \"%[^\"]\"", type);
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

int main(int argc, char *argv[]) {
    char repo_url[1024] = {0};

    // handle --set-repo <url>
    if (argc == 3 && strcmp(argv[1], "-Sr") == 0) {
        save_repo_url(argv[2]);
        printf("Repository URL saved.\n");
        return 0;
    }

    read_repo_url(repo_url, sizeof(repo_url));

    if (argc < 2) {
        printf("Usage:\n");
        printf("  pkg -Q [list packages]\n");
        printf("  pkg -S <pkg-name> [Install packages]\n");
        printf("  pkg -Sy [Refresh package list]\n");
        printf("  pkg -Sr <url> [set repo packages]\n");
        return 0;
    }

    if (strcmp(argv[1], "-Sy") == 0) {
        git_sync(repo_url);
        return 0;
    }

    git_sync(repo_url);

    if (strcmp(argv[1], "-Q") == 0) {
        list_packages();
    } else if (strcmp(argv[1], "-S") == 0) {
        if (argc < 3) {
            printf("Please provide a package name.\n");
            return 1;
        }
        install_package(argv[2]);
    } else {
        printf("Unknown command.\n");
    }

    return 0;
}

