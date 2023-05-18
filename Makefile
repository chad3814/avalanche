# this is for building pure C++ library / test programs
# which is very valuable for debugging and memory checking

OUTDIR=build

CORE_SRC=\
	utils.cc \
	custom_io_group.cc \
	image_interface.cc \
	image.cc \
	video_reader.cc \
	private/custom_io_setup.cc \
	private/stream_map.cc \
	private/utils.cc \
	private/volume_data.cc

LIBS=\
	-lavformat \
	-lavutil \
	-lswscale \
	-lavcodec \
	-lswresample \

CFLAGS=-g3 -O0 -Wall -I/opt/homebrew/include --std=c++17 -L/opt/homebrew/lib

ALL_PROGS=\
	$(OUTDIR)/test_get_volume_data \
	$(OUTDIR)/test_get_clip_volume_data \
	$(OUTDIR)/test_extract_clip_reencode \
	$(OUTDIR)/test_get_metadata \
	$(OUTDIR)/test_remux \
	$(OUTDIR)/test_extract_clip_remux \
	$(OUTDIR)/test_get_image \
	$(OUTDIR)/test_get_multiple_images \


all: $(ALL_PROGS)

build:
	mkdir -p $(OUTDIR)

$(OUTDIR)/test_get_metadata: test/test_get_metadata.cc $(CORE_SRC) $(OUTDIR)
	g++ $(CFLAGS) -o $@ \
		$(CORE_SRC) \
		test/test_get_metadata.cc \
		$(LIBS)

$(OUTDIR)/test_remux: test/test_remux.cc $(CORE_SRC) $(OUTDIR)
	g++ $(CFLAGS) -o $@ \
		$(CORE_SRC) \
		test/test_remux.cc \
		$(LIBS)

$(OUTDIR)/test_extract_clip_remux: test/test_remux.cc $(CORE_SRC) $(OUTDIR)
	g++ $(CFLAGS) -o $@ \
		$(CORE_SRC) \
		test/test_extract_clip_remux.cc \
		$(LIBS)

$(OUTDIR)/test_extract_clip_reencode: test/test_extract_clip_reencode.cc $(CORE_SRC) $(OUTDIR)
	g++ $(CFLAGS) -o $@ \
		$(CORE_SRC) \
		test/test_extract_clip_reencode.cc \
		$(LIBS)

$(OUTDIR)/test_get_image: test/test_get_image.cc $(CORE_SRC) $(OUTDIR)
	g++ $(CFLAGS) -o $@ \
		$(CORE_SRC) \
		test/test_get_image.cc \
		$(LIBS)

$(OUTDIR)/test_get_multiple_images: test/test_get_multiple_images.cc $(CORE_SRC) $(OUTDIR)
	g++ $(CFLAGS) -o $@ \
		$(CORE_SRC) \
		test/test_get_multiple_images.cc \
		$(LIBS)

$(OUTDIR)/test_get_clip_volume_data: test/test_get_clip_volume_data.cc $(CORE_SRC) $(OUTDIR)
	g++ $(CFLAGS) -o $@ \
		$(CORE_SRC) \
		test/test_get_clip_volume_data.cc \
		$(LIBS)

$(OUTDIR)/test_get_volume_data: test/test_get_volume_data.cc $(CORE_SRC) $(OUTDIR)
	g++ $(CFLAGS) -o $@ \
		$(CORE_SRC) \
		test/test_get_volume_data.cc \
		$(LIBS)

clean:
	rm -rf $(OUTDIR)
