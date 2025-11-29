#ifndef VICE_IOI_VIDEO_OUTPUT_H
#define VICE_IOI_VIDEO_OUTPUT_H

#include "types.h"
#include "video.h"

typedef struct video_canvas_s video_canvas_t;

extern void ioi_video_output_init(video_canvas_t *canvas);
extern void ioi_video_output_update(uint8_t *trg, int pitcht);
extern void ioi_video_output_close(void);

extern void ioi_update_drive_led(unsigned int drive_number, unsigned int drive_base, unsigned int led_pwm1, unsigned int led_pwm2);
extern void ioi_update_tape_motor_status(int port, int motor);
extern void ioi_update_tape_control_status(int port, int control);
extern void ioi_update_tape_counter(int port, int counter);

#endif
