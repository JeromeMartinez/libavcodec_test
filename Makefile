default:
	cc main.c $(CFLAGS) $(LDFLAGS) -lavformat$(FFMPEG_SUFFIX) -lavcodec$(FFMPEG_SUFFIX) -lswscale$(FFMPEG_SUFFIX) -lavutil$(FFMPEG_SUFFIX) $(LDLIBS) -o dec_tx_test
