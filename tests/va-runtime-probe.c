#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <va/va.h>
#include <va/va_drm.h>

int main(int argc, char **argv) {
    const char *device = argc > 1 ? argv[1] : "/dev/dri/renderD128";
    int fd = open(device, O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror(device); return 2; }
    Dl_info info;
    if (dladdr((void *)vaInitialize, &info)) printf("libva=%s\n", info.dli_fname);
    VADisplay display = vaGetDisplayDRM(fd);
    int major = 0, minor = 0;
    VAStatus status = vaInitialize(display, &major, &minor);
    printf("vaInitialize=%d (%s); API=%d.%d\n", status, vaErrorStr(status), major, minor);
    if (status == VA_STATUS_SUCCESS) {
        printf("vendor=%s\n", vaQueryVendorString(display));
        VAEntrypoint entries[32];
        int count = 0;
        status = vaQueryConfigEntrypoints(display, VAProfileH264High, entries, &count);
        printf("H264High entrypoints status=%d:", status);
        for (int i = 0; i < count; ++i) printf(" %d", entries[i]);
        puts("");
        vaTerminate(display);
    }
    close(fd);
    return status == VA_STATUS_SUCCESS ? 0 : 1;
}
