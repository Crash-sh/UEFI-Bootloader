#define _XOPEN_SOURCE 700
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>

static int check_mount(const char *esp)
{
    FILE *mounts = setmntent("/proc/self/mounts", "r");

    struct mntent *entry;

    char resolved[PATH_MAX];

    int result = -1;

    if (mounts == NULL) {
        return(-1);
    }

    while ((entry = getmntent(mounts)) != NULL) {
        if (realpath(entry->mnt_dir, resolved) != NULL && strcmp(esp, resolved) == 0) {
            result = strcmp(entry->mnt_type, "vfat") == 0 ? 0 : -1;
        }
    }

    endmntent(mounts);

    if (result != 0) {
        fprintf(stderr, "--esp must be the mounted FAT EFI System Partition\n");
    }

    return(result);
}

static int check_secure_boot(void)
{
    glob_t matches = {0};

    FILE *stream = NULL;

    unsigned char value[6];

    int result = -1;

    if (glob("/sys/firmware/efi/efivars/SecureBoot-*", 0, NULL, &matches) == 0 &&
        matches.gl_pathc == 1) {
        stream = fopen(matches.gl_pathv[0], "rb");
        if (stream != NULL && fread(value, 1, sizeof(value), stream) == 5 && !ferror(stream) &&
            value[4] <= 1) {
            result = value[4];
        }
    }

    if (stream != NULL) {
        fclose(stream);
    }

    globfree(&matches);

    if (result < 0) {
        fprintf(stderr, "Could not determine Secure Boot state; refusing installation\n");
    }

    return(result);
}

static int require_path(const char *path, int directory)
{
    struct stat info;

    if (stat(path, &info) != 0 || (directory ? !S_ISDIR(info.st_mode) : !S_ISREG(info.st_mode))) {
        fprintf(stderr, "Required %s missing: %s\n", directory ? "directory" : "file", path);
        return(-1);
    }
    return(0);
}

/* Publish only a validated private snapshot, keeping the old executable intact
 * until its backup has been flushed. The menu entry is deliberately preserved. */
static int update_loader(const char *source, const char *destination, int signed_package)
{
    char parent[PATH_MAX], temporary[PATH_MAX], next[PATH_MAX], old[PATH_MAX];
    char backup[PATH_MAX];
    struct stat info;
    int lock = -1, result = -1;
    int created = 0;

    if (strlen(destination) >= sizeof(parent)) {
        return(-1);
    }
    strcpy(parent, destination);
    char *slash = strrchr(parent, '/');
    if (slash == NULL || slash == parent) {
        return(-1);
    }
    *slash = '\0';
    if (path_join(temporary, sizeof(temporary), parent, ".neuros-update-XXXXXX") != 0 ||
        snprintf(backup, sizeof(backup), "%s.previous", destination) >= (int)sizeof(backup)) {
        return(-1);
    }

    lock = open(parent, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) != 0) {
        perror("Cannot lock installed loader directory");
        goto done;
    }
    if (lstat(destination, &info) != 0 || !S_ISREG(info.st_mode) ||
        validate_image(destination, 0) != 0) {
        fprintf(stderr, "Update requires an existing regular EFI executable\n");
        goto done;
    }
    if (lstat(backup, &info) == 0) {
        if (!S_ISREG(info.st_mode)) {
            fprintf(stderr, "Backup path is not a regular file: %s\n", backup);
            goto done;
        }
    } else if (errno != ENOENT) {
        perror(backup);
        goto done;
    }
    if (mkdtemp(temporary) == NULL) {
        goto done;
    }
    created = 1;
    if (path_join(next, sizeof(next), temporary, "new.efi") != 0 ||
        path_join(old, sizeof(old), temporary, "old.efi") != 0) {
        goto done;
    }
    if (copy_image(source, next, 1) != 0 || validate_image(next, 0) != 0 ||
        (signed_package && require_signature_container(next) != 0) ||
        copy_image(destination, old, 1) != 0) {
        goto cleanup;
    }
    if (rename(old, backup) != 0 || sync_directory(parent) != 0 || sync_directory(temporary) != 0) {
        goto cleanup;
    }
    if (rename(next, destination) != 0) {
        goto cleanup;
    }
    if (sync_directory(parent) != 0 || sync_directory(temporary) != 0) {
        fprintf(stderr, "Loader replaced but directory sync failed; inspect %s and %s\n",
                destination, backup);
        goto cleanup;
    }
    printf("Updated NeurOS. Previous executable: %s\n", backup);
    result = 0;
cleanup:
    unlink(next);
    unlink(old);
done:
    if (created) {
        rmdir(temporary);
    }
    if (lock >= 0) {
        close(lock);
    }
    return(result);
}

int main(int argc, char **argv)
{
    const char *requested = "/boot";
    const char *files[] = {"EFI/NeurOS/neuros.efi", "loader/entries/neuros.conf"};
    char esp[PATH_MAX], executable[PATH_MAX], deploy[PATH_MAX], path[PATH_MAX];
    char sources[2][PATH_MAX], destinations[2][PATH_MAX];
    struct stat info;
    ssize_t length;
    int signed_package = 0, update = 0, secure;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--esp") == 0 && i + 1 < argc) {
            requested = argv[++i];
        } else if (strcmp(argv[i], "--signed") == 0) {
            signed_package = 1;
        } else if (strcmp(argv[i], "--update") == 0) {
            update = 1;
        } else {
            fprintf(stderr, "Usage: %s [--esp /boot] [--signed] [--update]\n", argv[0]);
            return(2);
        }
    }

    if (geteuid() != 0) {
        fprintf(stderr, "Run with sudo after make package.\n");
        return(1);
    }

    if (realpath(requested, esp) == NULL) {
        perror(requested);
        return(1);
    }

    length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);

    if (length < 0 || (size_t)length >= sizeof(executable) - 1) {
        return(1);
    }

    executable[length] = '\0';

    char *last = strrchr(executable, '/');

    if (last == NULL) {
        return(1);
    }

    *last = '\0';

    secure = check_secure_boot();
    if (path_join(deploy, sizeof(deploy), executable,
                  signed_package ? "deploy-signed" : "deploy") != 0 ||
        check_mount(esp) != 0 || secure < 0) {
        return(1);
    }
    if (secure && !signed_package) {
        fprintf(stderr, "Secure Boot is enabled: use make package-signed and install --signed.\n");
        return(1);
    }

    if (path_join(path, sizeof(path), esp, "EFI/Linux/arch-linux.efi") != 0 ||
        validate_image(path, 1) != 0 || (secure && require_signature_container(path) != 0)) {
        return(1);
    }

    if (path_join(path, sizeof(path), esp, "EFI/systemd/systemd-bootx64.efi") != 0 ||
        validate_image(path, 0) != 0 || (secure && require_signature_container(path) != 0)) {
        return(1);
    }

    if (path_join(path, sizeof(path), esp, "loader/entries") != 0 || require_path(path, 1) != 0) {
        return(1);
    }

    for (size_t i = 0; i < 2; ++i) {
        if (path_join(sources[i], sizeof(sources[i]), deploy, files[i]) != 0 ||
            path_join(destinations[i], sizeof(destinations[i]), esp, files[i]) != 0 ||
            require_path(sources[i], 0) != 0) {
            return(1);
        }

        if (update) {
            if (lstat(destinations[i], &info) != 0 || !S_ISREG(info.st_mode)) {
                fprintf(stderr, "Update requires existing regular file: %s\n", destinations[i]);
                return(1);
            }
        } else if (lstat(destinations[i], &info) == 0 || errno != ENOENT) {
            fprintf(stderr, "Refusing to overwrite or access %s\n", destinations[i]);
            return(1);
        }
    }
    if (validate_image(sources[0], 0) != 0 ||
        (signed_package && require_signature_container(sources[0]) != 0)) {
        return(1);
    }
    if (update) {
        if (update_loader(sources[0], destinations[0], signed_package) != 0) {
            return(1);
        }
        puts("Menu entry, boot order, and default are unchanged.");
        return(0);
    }
    if (copy_image(sources[0], destinations[0], 1) != 0) {
        return(1);
    }

    if (copy_image(sources[1], destinations[1], 1) != 0) {
        if (unlink(destinations[0]) != 0) {
            perror("Could not roll back new loader");
        }

        return(1);
    }

    puts("Installed NeurOS menu entry. Boot order and default are unchanged.");
    if (signed_package) {
        puts("Signature containers checked; firmware validates signatures and enrolled trust at "
             "boot.");
    }
    puts("Reboot manually, hold Space for the systemd-boot menu, and choose NeurOS.");

    return(0);
}
