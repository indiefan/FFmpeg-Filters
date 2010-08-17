This is an updated version of the vf_fade filter, which uses the current ffmpeg filter api.

To use, when building from source add the file to the libavfilters folder in ffmpeg, then:

Update libavfilters/Makefile with the following:
 OBJS-$(CONFIG_FADE_FILTER)		     		 += vf_fade.o

And finally update libavfilters/allfilters.c with the following:
 REGISTER_FILTER (FADE,		  fade,	   	   vf);


Afterwards you should be able to apply fades, such as the following:
 ffmpeg -i input.avi -vf "fade=in:0:30" output.avi
