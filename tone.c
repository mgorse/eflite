/* tone.c - code for generating tones
 * $Id: tone.c,v 1.4 2002/05/03 01:17:58 mgorse Exp $
 */

#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include "es.h"

#ifdef EFLITE
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

#ifdef EFLITE
cst_wave *generate_tone(int freq, int dur, int vol)
{
  float max = 2 * PI * freq * dur / 1000;
  float step = 2 * PI * freq / 8000;
  float n;
  int i;
  cst_wave *wptr;

  wptr = cst_alloc(cst_wave, 1);
  if (wptr == NULL) return wptr;
  wptr->num_samples = max / step;
  wptr->samples = cst_alloc(short, wptr->num_samples);
  if (wptr->samples == NULL)
  {
    free(wptr);
    return NULL;
  }
  wptr->num_channels = 1;
  wptr->sample_rate = 8000;
  for (i = 0,n = 0; i < wptr->num_samples; n += step,i++)
  {
    wptr->samples[i] = sin(n) * vol;
  }
  return wptr;
}
#endif	/* EFLITE */

#ifdef LINUX_NATIVE
static int soundcard_tone(int freq, int dur, int vol)
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
    val = sin(n) * vol;
    write(fd, &val, 2);
  }
  close(fd);
  return 0;
}
#endif	/* LINUX_NATIVE */

void do_tone(struct synth_struct *s, int freq, int dur, int vol, int flags)
{
#ifdef LINUX_NATIVE
  if ((flags & 0x01) && !speaker_tone(freq, dur)) return;
  if ((flags & 0x02) && !soundcard_tone(freq, dur, vol)) return;
#elif defined(EFLITE)
  if (flags & 0x02) add_tone_command(s, freq, dur, vol);
#else
  es_log(2, "tones not supported on this platform");
#endif
}
