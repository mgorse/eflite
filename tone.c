/* tone.c - code for making tones */

#include <sys/ioctl.h>
#include <errno.h>
#include <math.h>

/* tone.c - code for generating tones */

#include <unistd.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/soundcard.h>

int speaker_tone(int freq, int dur)
{
  int fd;
  int converted_freq;
  int result;

  converted_freq = 1193180 / freq;
  if (converted_freq == 0 || converted_freq > 0xffff) return 0;
  fd = open("/dev/console", O_WRONLY);
  if (fd == -1) return 0;
  result = ioctl(fd, KDMKTONE, (dur << 16) + converted_freq);
  close(fd);
  return (result == 0? 1: 0);
}

#define PI 3.14192653589793238

int dsp_tone(int freq, int dur)
{
  int fd;
  int fmt = AFMT_S16_LE;
  int stereo = 0;
  int speed = 44100;
  float max = 2 * PI * freq * dur / 1000;
  float step = 2 * PI * freq / speed;
  float n;
  short val;

  fd = open("/dev/dsp", O_WRONLY);
  if (fd == -1) return -1;
  ioctl(fd, SNDCTL_DSP_SETFMT, &fmt);
  ioctl(fd, SNDCTL_DSP_STEREO, &stereo);
  ioctl(fd, SNDCTL_DSP_SPEED, &speed);
  for (n = 0; n < max; n += step)
  {
    val = sin(n) * 32767;
    write(fd, &val, 2);
  }
  close(fd);
  return 0;
}
