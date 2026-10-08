#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>

int main(void)
{
    char command[4096] = {0};
    if (getpid() != 1 || mount("proc", "/proc", "proc", 0, NULL) != 0) {
        goto failed;
    }
    int fd = open("/proc/cmdline", O_RDONLY);
    if (fd < 0 || read(fd, command, sizeof(command) - 1) <= 0) {
        goto failed;
    }
    close(fd);
    if (!strstr(command, "neuros_test=direct")) {
        goto failed;
    }
    /* Verify firmware tables survived the direct handoff, including the RSDP. */
    if (mount("sysfs", "/sys", "sysfs", 0, NULL) != 0) {
        goto failed;
    }
    memset(command, 0, sizeof(command));
    fd = open("/sys/firmware/efi/systab", O_RDONLY);
    if (fd < 0 || read(fd, command, sizeof(command) - 1) <= 0) {
        goto failed;
    }
    close(fd);
    if (!strstr(command, "ACPI20=")) {
        goto failed;
    }
    fd = open("/sys/firmware/acpi/tables/APIC", O_RDONLY);
    if (fd < 0) {
        goto failed;
    }
    close(fd);
    puts("NEUROS: direct Linux init reached");
    puts("NEUROS: initramfs and command line verified");
    fflush(stdout);
    for (;;) {
        pause();
    }
failed:
    puts("NEUROS: init verification failed");
    fflush(stdout);
    for (;;) {
        pause();
    }
}
