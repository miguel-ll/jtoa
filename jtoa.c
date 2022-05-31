#include <stdio.h>
#include "jpeglib.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ROUND(x) (int) ( 0.5f + x )

typedef struct Image_ {
	int width;
	int height;
	float *pixel;
	int *yadds;
	float resize_y;
	float resize_x;
	int *lookup_resx;
} Image;

// Options with defaults
int verbose = 0;
int auto_height = 1;
int auto_width = 0;
int width = 78;
int height = 0;
int progress_barlength = 56;
int invert = 0;
int flipx = 0;
int flipy = 0;

#define ASCII_PALETTE_SIZE 256

char ascii_palette[ASCII_PALETTE_SIZE+1] = "";
const char* default_palette = "   ...',;:clodxkO0KXNWM";

void help() {
	fputs("Usage: jtoa [ options ] [ file(s) ]\n\n"

	"Convert files in JPEG format to ASCII.\n\n"
	"OPTIONS\n"
	"    --chars=...  Leftmost char corresponds to black pixel, right-most to white (specify at least 2 characters).\n"
	"    --flipx      Flip image in X direction.\n"
	"    --flipy      Flip image in Y direction.\n"
	"    --height=N   Set output height, calculate width from aspect ratio.\n"
	"-h, --help       Print program help.\n"
	"-i, --invert     Invert output image.  Use if your display has a dark background.\n"
	"    --size=WxH   Set output width and height.\n"
	"-v, --verbose    Verbose output.\n"
	"    --width=N    Set output width, calculate height from ratio.\n\n"

	"  The default running mode is 'jtoa --width=78'\n", stderr);
}
// returns positive error code, or -1 for parsing OK
int parse_options(const int argc, char** argv) {

	// define some shorthand defines
	#define IF_OPTS(shortopt, longopt) if ( !strcmp(s, shortopt) || !strcmp(s, longopt) )
	#define IF_OPT(shortopt) if ( !strcmp(s, shortopt) )
	#define IF_VARS(format, var1, var2) if ( sscanf(s, format, var1, var2) == 2 )
	#define IF_VAR(format, var) if ( sscanf(s, format, var) == 1 )

	int n, files;
	for ( n=1, files=0; n<argc; ++n ) {
		const char *s = argv[n];

		if ( *s != '-' ) { // count files to read
			++files; continue;
		}

		IF_OPT("-")			{ ++files; continue; }
		IF_OPTS("-h", "--help")		{ help(); return 0; }
		IF_OPTS("-v", "--verbose")	{ verbose = 1; continue; }
		IF_OPTS("-i", "--invert") 	{ invert = 1; continue; }
		IF_OPT("--flipx") 		{ flipx = 1; continue; }
		IF_OPT("--flipy") 		{ flipy = 1; continue; }
		IF_VAR("--width=%d", &width)	{ auto_height += 1; continue; }
		IF_VAR("--height=%d", &height)	{ auto_width += 1; continue; }
		IF_VARS("--size=%dx%d", &width, &height) { auto_width = auto_height = 0; continue; }

		if ( !strncmp(s, "--chars=", 8) ) {
			if ( strlen(s-8) > ASCII_PALETTE_SIZE ) {
				fprintf(stderr, "Too many ascii characters specified.\n");
				return 1;
			}
			// don't use sscanf, we need to read spaces as well
			strcpy(ascii_palette, s+8);
			continue;
		}
		fprintf(stderr, "Unknown option %s\n\n", s);
		help();
		return 1;

	} // args ...
	if ( !files ) {
		fprintf(stderr, "No files specified.\n\n");
		help();
		return 1;
	}
	// only --width specified, calc width
	if ( auto_width==1 && auto_height == 1 )
		auto_height = 0;
	// --width and --height is the same as using --size
	if ( auto_width==2 && auto_height==1 )
		auto_width = auto_height = 0;

	if ( strlen(ascii_palette) < 2 ) {
		fprintf(stderr, "You must specify at least two characters in --chars.\n");
		return 1;
	}
	if ( (width < 1 && !auto_width) || (height < 1 && !auto_height) ) {
		fprintf(stderr, "Invalid width or height specified.\n");
		return 1;
	}
	return -1;
}

void calc_aspect_ratio(const int jpeg_width, const int jpeg_height) {
	// Calculate width or height, but not both
	if ( auto_width && !auto_height ) {
		width = ROUND(2.0f * (float) height * (float) jpeg_width / (float) jpeg_height);
		// adjust for too small dimensions
		while ( width==0 ) {
			++height;
			calc_aspect_ratio(jpeg_width, jpeg_height);
		}
	}
	if ( !auto_width && auto_height ) {
		height = ROUND(0.5f * (float) width * (float) jpeg_height / (float) jpeg_width);
		// adjust for too small dimensions
		while ( height==0 ) {
			++width;
			calc_aspect_ratio(jpeg_width, jpeg_height);
		}
	}
}

void print_image(const Image* i, const int chars) {
	int x, y;
	const int w = i->width;
	const int h = i->height;
	char line[w + 1];
	line[w] = 0;

	for ( y=0; y < h; ++y ) {
		for ( x=0; x < w; ++x ) {
			float intensity = i->pixel[(!flipy? y : h-y-1)*w + x];
			int pos = ROUND( (float) chars * intensity );
			line[!flipx? x : w-x-1] = ascii_palette[ !invert ? chars - pos : pos ];
		}

		printf("%s\n", line);
	}
}

void clear(Image* i) {
	memset(i->pixel, 0, i->width * i->height * sizeof(float));
	memset(i->yadds, 0, i->height * sizeof(int) );
}

void normalize(Image* i) {
	const int w = i->width;
	const int h = i->height;

	register int x, y, yoffs;

	for ( y=0, yoffs=0; y < h; ++y, yoffs += w )
	for ( x=0; x < w; ++x ) {
		if ( i->yadds[y] != 0 )
			i->pixel[yoffs + x] /= (float) i->yadds[y];
	}
}

static float intensity(const JSAMPLE* source, const int components) {
	float v = source[0];

	int c=1;
	while ( c < components )
		v += source[c++];

	v /= 255.0f * components;
	return v;
}

void print_info(const struct jpeg_decompress_struct* cinfo) {
	fprintf(stderr, "Source width: %d\n", cinfo->output_width);
	fprintf(stderr, "Source height: %d\n", cinfo->output_height);
	fprintf(stderr, "Source color components: %d\n", cinfo->output_components);
	fprintf(stderr, "Output width: %d\n", width);
	fprintf(stderr, "Output height: %d\n", height);
	fprintf(stderr, "Output palette (%d chars): '%s'\n\n", (int) strlen(ascii_palette), ascii_palette);
}

static void process_scanline(const struct jpeg_decompress_struct *jpg, const JSAMPLE* scanline, Image* i) {
	static int lasty = 0;
	const int y = ROUND( i->resize_y * (float) (jpg->output_scanline-1) );
	// include all scanlines since last call
	while ( lasty <= y ) {
		const int yoff = lasty * i->width;
		int x;

		for ( x=0; x < i->width; ++x ) {
			i->pixel[yoff + x] += intensity( &scanline[ i->lookup_resx[x] ],
				jpg->out_color_components);
		}

		++i->yadds[lasty++];
	}
	lasty = y;
}

void free_image(Image* i) {
	if ( i->pixel ) free(i->pixel);
	if ( i->yadds ) free(i->yadds);
	if ( i->lookup_resx ) free(i->lookup_resx);
}

void malloc_image(Image* i) {
	i->pixel = NULL;
	i->yadds = NULL;
	i->lookup_resx = NULL;

	i->width = width;
	i->height = height;

	if ( (i->pixel = (float*) malloc(width * height * sizeof(float))) == NULL ) {
		fprintf(stderr, "Not enough memory for given output dimension\n");
		exit(1);
	}

	if ( (i->yadds = (int*) malloc(height * sizeof(int))) == NULL ) {
		fprintf(stderr, "Not enough memory for given output dimension (for yadds)\n");
		free_image(i);
		exit(1);
	}

	if ( (i->lookup_resx = (int*) malloc(width * sizeof(int))) == NULL ) {
		fprintf(stderr, "Not enough memory for given output dimension (lookup_resx)\n");
		free_image(i);
		exit(1);
	}
}

void init_image(Image *i, const struct jpeg_decompress_struct *jpg) {
	i->resize_y = (float) (i->height - 1) / (float) (jpg->output_height-1);
	i->resize_x = (float) jpg->output_width / (float) i->width;

	int dst_x;
	for ( dst_x=0; dst_x < i->width; ++dst_x ) {
		i->lookup_resx[dst_x] = (int)( (float) dst_x * i->resize_x );
		i->lookup_resx[dst_x] *= jpg->out_color_components;
	}
}

int decompress(FILE *fp) {
	struct jpeg_error_mgr jerr;
	struct jpeg_decompress_struct jpg;

	jpg.err = jpeg_std_error(&jerr);

	jpeg_create_decompress(&jpg);
	jpeg_stdio_src(&jpg, fp);
	jpeg_read_header(&jpg, TRUE);
	jpeg_start_decompress(&jpg);

	int row_stride = jpg.output_width * jpg.output_components;

	JSAMPARRAY buffer = (*jpg.mem->alloc_sarray)
		((j_common_ptr) &jpg, JPOOL_IMAGE, row_stride, 1);

	calc_aspect_ratio(jpg.output_width, jpg.output_height);

	Image image;
	malloc_image(&image);
	clear(&image);

	if ( verbose ) print_info(&jpg);

	init_image(&image, &jpg);

	while ( jpg.output_scanline < jpg.output_height ) {
		jpeg_read_scanlines(&jpg, buffer, 1);
		process_scanline(&jpg, buffer[0], &image);
	}
	normalize(&image);

	print_image(&image, (int) strlen(ascii_palette) - 1);

	free_image(&image);

	jpeg_finish_decompress(&jpg);
	jpeg_destroy_decompress(&jpg);

	return 0;
}

int main(int argc, char** argv) {
	strcpy(ascii_palette, default_palette);
	int r = parse_options(argc, argv);
	if ( r >= 0 ) return r;

	int n;
	for ( n=1; n<argc; ++n ) {

		// Skip options
		if ( argv[n][0]=='-' && argv[n][1]!=0 )
			continue;

		// Read from stdin
		if ( argv[n][0]=='-' && argv[n][1]==0 ) {
			int r = decompress(stdin);
			if ( r == 0 )
				continue;
		}
		FILE *fp;
		if ( (fp = fopen(argv[n], "rb")) != NULL ) {

			if ( verbose )
				fprintf(stderr, "File: %s\n", argv[n]);

			int r = decompress(fp);
			fclose(fp);

			if ( r != 0 )
				return r;
		} else {
			fprintf(stderr, "Can't open %s\n", argv[n]);
			return 1;
		}
	}

	return 0;
}

