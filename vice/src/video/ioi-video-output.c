#ifdef DEBUG_VIDEO
#define DBG(_x_) log_debug _x_
#else
#define DBG(_x_)
#endif

#include "ioi-video-output.h"
#include <windows.h>
#include <tchar.h>
#include "machine.h"

#define CANVAS_SIZE 417792

const int surfaceBytesPerPixel = 4;

int signal = 0;
int WIDTH = 0;
int HEIGHT = 0;

int videoOutputCanvas[CANVAS_SIZE];

LPCTSTR pBuf;
HANDLE hMapFile;
TCHAR VICE_IOI_VIDEO_OUTPUT_NAME[] = TEXT("VICE_IOI_VIDEO_OUTPUT");

void ioi_video_output_update(uint8_t *trg, int pitcht)
{
    int scIndex = 0;
    int py = 0;

    videoOutputCanvas[scIndex++] = machine_class;
    videoOutputCanvas[scIndex++] = WIDTH;
    videoOutputCanvas[scIndex++] = HEIGHT;
    videoOutputCanvas[scIndex++] = signal++;

    for (int y = 0; y < HEIGHT; y++) {
        py = pitcht * y;
        for (int x = 0; x < WIDTH; x++) {
            videoOutputCanvas[scIndex++] = *(int *)(trg + py + x * surfaceBytesPerPixel);
        }
    }

    if (pBuf != NULL) {
        CopyMemory((PVOID)pBuf, videoOutputCanvas, (scIndex * sizeof(int)));
    }
}

void ioi_video_output_init(void)
{
    switch (machine_class) {
        case VICE_MACHINE_C64:   /* fall through */
        case VICE_MACHINE_C64SC:
            // TODO replace magic numbers
            WIDTH = 384;
            HEIGHT = 272;
            break;
        case VICE_MACHINE_VIC20:
            WIDTH = 568;
            HEIGHT = 272;
            break;
        case VICE_MACHINE_PET:
            WIDTH = 384;
            HEIGHT = 266;
            break;
        default:
            break;
    }

    hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, 0, VICE_IOI_VIDEO_OUTPUT_NAME);

    if (hMapFile == NULL) {
        DBG((" - Could not create file mapping object (%ld).", GetLastError()));
    }

    pBuf = (LPTSTR) MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);

    if (pBuf == NULL) {
        DBG((" - Could not map view of file (%ld).", GetLastError()));
        CloseHandle(hMapFile);
    }
}

void ioi_video_output_close(void)
{
    UnmapViewOfFile(pBuf);
    CloseHandle(hMapFile);
}
