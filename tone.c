/* tone.c - code for generating tones
 * $Id$
 */

#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef EFLITE
#include "flite.h"
#elif defined(__linux__)
#define LINUX_NATIVE
#include <sys/ioctl.h>
#include <linux/kd.h>
#include <linux/soundcard.h>
#else
#define DUMMY
#endif

#ifdef LINUX_NATIVE
static int speaker_tone(int freq, int dur)
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
  return result;
}
#endif

#define PI 3.141592653589793238

#ifndef DUMMY
static int soundcard_tone(int freq, int dur, int vol)
{
#ifdef LINUX_NATIVE
  int fd;
  int fmt = AFMT_S16_LE;
  int stereo = 0;
#elif defined(EFLITE)
  cst_audiodev *ad;
#endif
  int speed = 44100;
  float max = 2 * PI * freq * dur / 1000;
  float step = 2 * PI * freq / speed;
  float n;
  short val;

#ifdef LINUX_NATIVE
  fd = open("/dev/dsp", O_WRONLY);
  if (fd == -1) return -1;
  ioctl(fd, SNDCTL_DSP_SETFMT, &fmt);
  ioctl(fd, SNDCTL_DSP_STEREO, &stereo);
  ioctl(fd, SNDCTL_DSP_SPEED, &speed);
#elif defined(EFLITE)
  ad = audio_open(44100, 1, CST_AUDIO_LINEAR16);
  if (ad == NULL) return -1;
#endif
  for (n = 0; n < max; n += step)
  {
    val = sin(n) * vol;
#ifdef LINUX_NATIVE
    write(fd, &val, 2);
#elif defined(EFLITE)
    audio_write(ad, &val, 2);
#endif
  }
#ifdef LINUX_NATIVE
  close(fd);
#elif defined(EFLITE)
  audio_close(ad);
#endif
  return 0;
}
#endif	/* DUMMY */

int do_tone(int freq, int dur, int vol, int flags)
{
#ifdef LINUX_NATIVE
  if ((flags & 0x01) && !speaker_tone(freq, dur)) return 0;
#endif
#ifndef DUMMY
  if ((flags & 0x02) && !soundcard_tone(freq, dur, vol)) return 0;
#else
  es_log(2, "tones not supported on this platform");
#endif
  return -1;
}
