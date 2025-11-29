#include <windows.h>
#include <tchar.h>

#include "ioi-video-output.h"

#include "cmdline.h"
#include "log.h"
#include "machine.h"
#include "resources.h"
#include "videoarch.h"

#define IOI_VIDEO_CANVAS_SIZE 510000
#define IOI_HEADER_SIZE 100

const int headerOffset = IOI_HEADER_SIZE / sizeof(int);

int renderCount = 0;
int WIDTH = 0;
int HEIGHT = 0;
int WIDTH_IN_BYTES = 0;
int ioi_output_enabled = 0;
int videoOutputHeader[IOI_HEADER_SIZE / sizeof(int)];
int *intPtr;
int *intCanvasPtr;
HANDLE hMapFile;
TCHAR VICE_IOI_VIDEO_OUTPUT_NAME[] = TEXT("VICE_IOI_VIDEO_OUTPUT");

void ioi_video_output_update(uint8_t *trg, int pitcht)
{
    if (!ioi_output_enabled)
    {
        return;
    }

    int *trgInt = (int *)trg;

    for (int y = 0; y < HEIGHT; y++)
    {
        int rowOffset = WIDTH * y;
        int *row = trgInt + rowOffset;
        memcpy(intCanvasPtr + rowOffset, row, WIDTH_IN_BYTES);
    }

    // Update header
    intPtr[3] = ++renderCount;
}

void ioi_video_output_init(video_canvas_t *canvas)
{
    resources_get_int("IOImode", &ioi_output_enabled);

    if (!ioi_output_enabled)
    {
        return;
    }

    WIDTH = canvas->draw_buffer->visible_width;
    HEIGHT = canvas->draw_buffer->visible_height;
    WIDTH_IN_BYTES = WIDTH * sizeof(int);

    log_message(LOG_DEFAULT, "IOI video init: %s -> machine_class=%d, width=%d, height=%d", machine_name, machine_class, WIDTH, HEIGHT);

    log_verbose("canvas->draw_buffer->visible : width=%u height=%u", canvas->draw_buffer->visible_width, canvas->draw_buffer->visible_height);
    log_verbose("      ->geometry->screen_size: width=%u height=%u", canvas->geometry->screen_size.width, canvas->geometry->screen_size.height);
    log_verbose("      ->geometry->gfx_size   : width=%u height=%u", canvas->geometry->gfx_size.width, canvas->geometry->gfx_size.height);
    log_verbose("      ->geometry->text_size  : width=%u height=%u", canvas->geometry->text_size.width, canvas->geometry->text_size.height);
    log_verbose("      ->geometry: extra_offscreen_border_left=%u extra_offscreen_border_right=%u", canvas->geometry->extra_offscreen_border_left, canvas->geometry->extra_offscreen_border_right);

    int totalSharedMemoryArea = IOI_VIDEO_CANVAS_SIZE + IOI_HEADER_SIZE;
    int requiredSize = (WIDTH * HEIGHT * sizeof(int)) + IOI_HEADER_SIZE;

    if (requiredSize > totalSharedMemoryArea)
    {
        log_error(LOG_DEFAULT, "IOI video init ERROR. IOI video shared memory area size [%d] exceeded, requiring: %d", totalSharedMemoryArea, requiredSize);
        ioi_output_enabled = 0;
        return;
    }

    hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, 0, VICE_IOI_VIDEO_OUTPUT_NAME);

    if (hMapFile == NULL)
    {
        log_message(LOG_DEFAULT, "IOI video init ERROR. Unable to open file mapping, error: %lu", GetLastError());
        ioi_output_enabled = 0;
        return;
    }

    intPtr = (int *)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);

    if (intPtr == NULL)
    {
        log_message(LOG_DEFAULT, "IOI video init ERROR. Failed to map view of file, error: %lu", GetLastError());
        ioi_video_output_close();
        return;
    }

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(intPtr, &mbi, sizeof(mbi)) != 0)
    {
        SIZE_T viewSize = mbi.RegionSize;
        log_verbose("Size of the mapped view: %llu bytes.", viewSize);
        if (viewSize < requiredSize)
        {
            log_error(LOG_DEFAULT, "IOI video init ERROR. Mapped view size [%llu] is smaller than required size [%d].", viewSize, requiredSize);
            ioi_video_output_close();
            return;
        }
    }
    else
    {
        log_message(LOG_DEFAULT, "VirtualQuery failed with error %lu\n", GetLastError());
    }

    intCanvasPtr = intPtr + headerOffset; // Precalculating canvas pointer

    // Initializing IOI video-output header static values
    intPtr[0] = machine_class;
    intPtr[1] = WIDTH;
    intPtr[2] = HEIGHT;
}

void ioi_video_output_close(void)
{
    if (!ioi_output_enabled)
    {
        return;
    }

    log_message(LOG_DEFAULT, "IOI video shutdown");
    ioi_output_enabled = 0;

    // Clear the shared memory area
    memset(intPtr, 0, IOI_VIDEO_CANVAS_SIZE + IOI_HEADER_SIZE);

    if (intPtr != NULL)
    {
        UnmapViewOfFile(intPtr);
        intPtr = NULL;
    }
    if (hMapFile != NULL)
    {
        CloseHandle(hMapFile);
        hMapFile = NULL;
    }
}

void ioi_update_drive_led(unsigned int drive_number,
                          unsigned int drive_base,
                          unsigned int led_pwm1,
                          unsigned int led_pwm2)
{
    if (!ioi_output_enabled || drive_number > 0)
    {
        return;
    }

    intPtr[4] = drive_number;
    intPtr[5] = drive_base;
    intPtr[6] = led_pwm1;
    intPtr[7] = led_pwm2;
}

void ioi_update_tape_motor_status(int port, int motor)
{
    if (!ioi_output_enabled || port > 0)
    {
        return;
    }

    intPtr[8] = port;
    intPtr[9] = motor;
}

void ioi_update_tape_control_status(int port, int control)
{
    if (!ioi_output_enabled || port > 0)
    {
        return;
    }

    intPtr[8] = port;
    intPtr[10] = control;
}

void ioi_update_tape_counter(int port, int counter)
{
    if (!ioi_output_enabled || port > 0)
    {
        return;
    }

    intPtr[8] = port;
    intPtr[11] = counter;
}
