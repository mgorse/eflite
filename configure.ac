dnl Much of this code stolen from Festival Lite's configure.in

AC_INIT(es.h)

AC_PROG_CC

if test "x$GCC" = "xyes"; then
	CFLAGS="$CFLAGS -Wall"
fi

AC_CHECK_LIB(socket,connect)
AC_SEARCH_LIBS(pthread_create,pthread c_r)

dnl determine audio type or use none if none supported on this platform
dnl
AUDIODRIVER=none
AC_CHECK_HEADER(sys/soundcard.h,
              [AUDIODRIVER="oss"
               AUDIODEFS=-DCST_AUDIO_LINUX])
AC_CHECK_HEADER(machine/soundcard.h,
              [AUDIODRIVER="oss"
               AUDIODEFS=-DCST_AUDIO_FREEBSD])
AC_CHECK_HEADER(sys/audioio.h,
              [AUDIODRIVER="sun"
               AUDIODEFS=-DCST_AUDIO_SUNOS])
AC_CHECK_HEADER(alsa/asoundlib.h,
              [AUDIODRIVER="alsa"
	       AUDIODEFS=-DCST_AUDIO_ALSA
		                  AUDIOLIBS=-lasound])
AC_CHECK_HEADER(mmsystem.h,
	      [AUDIODRIVER="wince"
	       AUDIODEFS=-DCST_AUDIO_WINCE
	       AUDIOLIBS=-lwinmm])


dnl
dnl allow the user to override the one detected above
dnl
AC_ARG_WITH( audio,
	[  --with-audio          with specific audio support (none linux freebsd etc) ],
        AUDIODRIVER=$with_audio )

if test "x$AUDIODEFS" = x; then
    case "$AUDIODRIVER" in
	linux|oss)
	    AUDIODRIVER=oss
	    AUDIODEFS=-DCST_AUDIO_LINUX
	    ;;
	*bsd)
	    AUDIODRIVER=oss
	    AUDIODEFS=-DCST_AUDIO_FREEBSD
	    ;;
	qnx)
	    AUDIODRIVER=alsa
	    AUDIODEFS=-DCST_AUDIO_QNX
	    ;;
	none)
	    AUDIODEFS=-DCST_AUDIO_NONE
	    ;;
    esac
fi
AC_SUBST(AUDIODRIVER)
AC_SUBST(AUDIODEFS)
AC_SUBST(AUDIOLIBS)

dnl
dnl language/lexicon/voice parameters (we don't really support multiple
dnl ones yet, but its included as a start
dnl
AC_ARG_WITH( lang, [  --with-lang           with language ],
        FL_LANG=$with_lang )
if test "x$with_lang" = "x"; then
        FL_LANG="usenglish"
fi
AC_SUBST(FL_LANG)

AC_ARG_WITH( vox, [  --with-vox            with vox ],
        FL_VOX=$with_vox )
if test "x$with_vox" = "x"; then
        FL_VOX="cmu_us_kal"
fi
AC_SUBST(FL_VOX)

AC_ARG_WITH( lex, [  --with-lex            with lexicon ],
        FL_LEX=$with_lex )
if test "x$with_lex" = "x"; then
        FL_LEX="cmulex"
fi
AC_SUBST(FL_LEX)
AC_SUBST(LIBS)

AC_OUTPUT(Makefile)
