/* Exercise publication without root, a real ESP, or firmware variables. */
#define main installer_main
#include "../tools/install.c"
#undef main

#undef NDEBUG
#include <assert.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>

static void write_fixture(const char *path, unsigned char marker)
{
    unsigned char data[512] = {0};

    memcpy(data, "MZ", 2);

    data[60] = 64;

    memcpy(data + 64, "PE\0\0", 4);
    data[68] = 0x64;
    data[69] = 0x86;
    data[70] = 1;
    data[84] = 240;
    data[88] = 0x0b;
    data[89] = 2;
    data[156] = 10;

    memcpy(data + 328, ".text", 5);

    data[344] = 1;
    data[348] = 0xff;
    data[349] = 1;
    data[511] = marker;

    FILE *file = fopen(path, "wb");

    assert(file != NULL);
    assert(fwrite(data, 1, sizeof(data), file) == sizeof(data));
    assert(fclose(file) == 0);
}

static int marker(const char *path)
{
    FILE *file = fopen(path, "rb");

    assert(file != NULL);
    assert(fseek(file, 511, SEEK_SET) == 0);

    int value = fgetc(file);

    assert(fclose(file) == 0);

    return(value);
}

int main(void)
{
    char directory[] = "/tmp/neuros-install-XXXXXX";
    char source[PATH_MAX], destination[PATH_MAX], backup[PATH_MAX];

    assert(mkdtemp(directory) != NULL);
    assert(path_join(source, sizeof(source), directory, "source.efi") == 0);
    assert(path_join(destination, sizeof(destination), directory, "neuros.efi") == 0);
    assert(path_join(backup, sizeof(backup), directory, "neuros.efi.previous") == 0);

    write_fixture(source, 2);
    write_fixture(destination, 1);

    assert(update_loader(source, destination, 0) == 0);
    assert(marker(destination) == 2 && marker(backup) == 1);

    /* A second update preserves exactly the immediately preceding executable. */
    write_fixture(source, 3);

    assert(update_loader(source, destination, 0) == 0);
    assert(marker(destination) == 3 && marker(backup) == 2);

    /* Signed mode rejects an unsigned snapshot before changing either file. */
    assert(update_loader(source, destination, 1) != 0);
    assert(marker(destination) == 3 && marker(backup) == 2);

    FILE *file = fopen(source, "wb");

    assert(file != NULL && fclose(file) == 0);
    assert(update_loader(source, destination, 0) != 0);
    assert(marker(destination) == 3 && marker(backup) == 2);

    write_fixture(source, 4);

    /* A failed staged write must not replace the installed file or backup. */
    pid_t child = fork();

    assert(child >= 0);

    if (child == 0) {
        struct rlimit limit = {128, 128};
        assert(signal(SIGXFSZ, SIG_IGN) != SIG_ERR);
        assert(setrlimit(RLIMIT_FSIZE, &limit) == 0);
        _exit(update_loader(source, destination, 0) != 0 ? 0 : 1);
    }
    int status;

    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(marker(destination) == 3 && marker(backup) == 2);

    int lock = open(directory, O_RDONLY | O_DIRECTORY);

    assert(lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) == 0);
    assert(update_loader(source, destination, 0) != 0);
    assert(marker(destination) == 3 && marker(backup) == 2);
    assert(close(lock) == 0);

    assert(unlink(backup) == 0);
    assert(symlink(source, backup) == 0);
    assert(update_loader(source, destination, 0) != 0);
    assert(marker(source) == 4 && marker(destination) == 3);
    assert(unlink(backup) == 0);
    assert(mkdir(backup, 0700) == 0);
    assert(update_loader(source, destination, 0) != 0);
    assert(marker(destination) == 3);
    assert(rmdir(backup) == 0);
    assert(unlink(destination) == 0);
    assert(update_loader(source, destination, 0) != 0);
    assert(symlink(source, destination) == 0);
    assert(update_loader(source, destination, 0) != 0);
    assert(marker(source) == 4);
    assert(unlink(destination) == 0);
    assert(unlink(source) == 0);
    assert(rmdir(directory) == 0);

    puts("Installer update tests passed");
    return(0);
}
