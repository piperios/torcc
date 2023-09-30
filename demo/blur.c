#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>
#include <torc.h>

#define TORC_NODES 12

#pragma pack(push, 2)          
	typedef struct bmpheader_ {
		char sign;
		int size;
		int notused;
		int data;
		int headwidth;
		int width;
		int height;
		short numofplanes;
		short bitpix;
		int method;
		int arraywidth;
		int horizresol;
		int vertresol;
		int colnum;
		int basecolnum;
	} bmpheader_t;
#pragma pack(pop)

/* This is the image structure, containing all the BMP information 
 * plus the RGB channels.
 */
typedef struct img_ {
	bmpheader_t header;
	int rgb_width;
	unsigned char *imgdata;
	unsigned char *red;
	unsigned char *green;
	unsigned char *blue;
} img_t;

void gaussian_blur_serial(int, img_t *, img_t *);

/* START of BMP utility functions */
static void bmp_read_img_from_file(char *inputfile, img_t *img) {
	FILE *file;
	bmpheader_t *header = &(img->header);

	file = fopen(inputfile, "rb");
	if (file == NULL) {
		fprintf(stderr, "File %s not found; exiting.", inputfile);
		exit(1);
	}
	
	fread(header, sizeof(bmpheader_t)+1, 1, file);
	if (header->bitpix != 24) {
		fprintf(stderr, "File %s is not in 24-bit format; exiting.", inputfile);
		exit(1);
	}

	img->imgdata = (unsigned char*) calloc(header->arraywidth, sizeof(unsigned char));
	if (img->imgdata == NULL) {
		fprintf(stderr, "Cannot allocate memory for image data; exiting.");
		exit(1);
	}
	
	fseek(file, header->data, SEEK_SET);
	fread(img->imgdata, header->arraywidth, 1, file);
	fclose(file);
}

static void bmp_clone_empty_img(img_t *imgin, img_t *imgout) {
	imgout->header = imgin->header;
	imgout->imgdata = 
		(unsigned char*) calloc(imgout->header.arraywidth, sizeof(unsigned char));
	if (imgout->imgdata == NULL) {
		fprintf(stderr, "Cannot allocate memory for clone image data; exiting.");
		exit(1);
	}
}

static void bmp_write_data_to_file(char *fname, img_t *img) {
	FILE *file;
	bmpheader_t *bmph = &(img->header);

	file = fopen(fname, "wb");
	fwrite(bmph, sizeof(bmpheader_t)+1, 1, file);
	fseek(file, bmph->data, SEEK_SET);
	fwrite(img->imgdata, bmph->arraywidth, 1, file);
	fclose(file);
}

static void bmp_rgb_from_data(img_t *img) {
	bmpheader_t *bmph = &(img->header);

	int i, j, pos = 0;
	int width = bmph->width, height = bmph->height;
	int rgb_width = img->rgb_width;

	for (i = 0; i < height; i++) 
		for (j = 0; j < width * 3; j += 3, pos++) {
			img->red[pos]   = img->imgdata[i * rgb_width + j];
			img->green[pos] = img->imgdata[i * rgb_width + j + 1];
			img->blue[pos]  = img->imgdata[i * rgb_width + j + 2];  
		}
}

static void bmp_data_from_rgb(img_t *img) {
	bmpheader_t *bmph = &(img->header);
	int i, j, pos = 0;
	int width = bmph->width, height = bmph->height;
	int rgb_width = img->rgb_width;

	for (i = 0; i < height; i++ ) 
		for (j = 0; j < width* 3 ; j += 3 , pos++) {
			img->imgdata[i * rgb_width  + j]     = img->red[pos];
			img->imgdata[i * rgb_width  + j + 1] = img->green[pos];
			img->imgdata[i * rgb_width  + j + 2] = img->blue[pos];
		}
}

static void bmp_rgb_alloc(img_t *img) {
	int width, height;

	width = img->header.width;
	height = img->header.height;

	img->red = (unsigned char*) calloc(width*height, sizeof(unsigned char));
	if (img->red == NULL) {
		fprintf(stderr, "Cannot allocate memory for the red channel; exiting.");
		exit(1);
	}

	img->green = (unsigned char*) calloc(width*height, sizeof(unsigned char));
	if (img->green == NULL) {
		fprintf(stderr, "Cannot allocate memory for the green channel; exiting.");
		exit(1);
	}

	img->blue = (unsigned char*) calloc(width*height, sizeof(unsigned char));
	if (img->blue == NULL) {
		fprintf(stderr, "Cannot allocate memory for the blue channel; exiting.");
		exit(1);
	}

	img->rgb_width = width * 3;
	if ((width * 3  % 4) != 0) { img->rgb_width += (4 - (width * 3 % 4)); }
}

static void bmp_img_free(img_t *img) {
	free(img->red);
	free(img->green);
	free(img->blue);
	free(img->imgdata);
}

/* END of BMP utility functions */

/* check bounds */
int clamp(int i , int min , int max) {
	if (i < min) return min;
	else if (i > max) return max;
	return i;  
}

/* Sequential Gaussian Blur */
void gaussian_blur(int radius, img_t *imgin, img_t *imgout) {
	int i, j;
	int width = imgin->header.width, height = imgin->header.height;
	double row, col;
	double weightSum = 0.0, redSum = 0.0, greenSum = 0.0, blueSum = 0.0;

	for (i = 0; i < height; i++) {
		for (j = 0; j < width ; j++) {
			for (row = i-radius; row <= i + radius; row++) {
				for (col = j-radius; col <= j + radius; col++) {
					int x = clamp(col, 0, width-1);
					int y = clamp(row, 0, height-1);
					int tempPos = y * width + x;
					double square = (col-j)*(col-j)+(row-i)*(row-i);
					double sigma = radius*radius;
					double weight = exp(-square / (2*sigma)) / (3.14*2*sigma);

					redSum += imgin->red[tempPos] * weight;
					greenSum += imgin->green[tempPos] * weight;
					blueSum += imgin->blue[tempPos] * weight;
					weightSum += weight;
				}    
			}
			imgout->red[i*width+j] = round(redSum/weightSum);
			imgout->green[i*width+j] = round(greenSum/weightSum);
			imgout->blue[i*width+j] = round(blueSum/weightSum);

			redSum = 0;
			greenSum = 0;
			blueSum = 0;
			weightSum = 0;
		}
	}
}

char *remove_ext(char *str, char extsep, char pathsep) {
	char *newstr, *ext, *lpath;

	if (str == NULL) return NULL;
	if ((newstr = malloc(strlen(str) + 1)) == NULL) return NULL;

	strcpy(newstr, str);
	ext = strrchr(newstr, extsep);
	lpath = (pathsep == 0) ? NULL : strrchr(newstr, pathsep);
	if (ext != NULL) {
		if (lpath != NULL) {
			if (lpath < ext) 
				*ext = '\0';
		} else 
			*ext = '\0';
	}
	return newstr;
}


int main(int argc, char *argv[]) {
	int i, radius;
	double t1, exectime = 0.0;
	char *inputfile, *noextfname;   
	char seqoutfile[128];
	img_t imgin, imgout;

	if (argc < 3) {
		fprintf(stderr, "Syntax: %s <blur-radius> <filename>, \n\te.g. %s 2 500.bmp\n", 
			argv[0], argv[0]);
		fprintf(stderr, "Available images: 500.bmp, 1000.bmp, 1500.bmp\n");
		exit(1);
	}

	torc_register_task(gaussian_blur);
	torc_init(argc, argv, MODE_MS);

	inputfile = argv[2];

	radius = atoi(argv[1]);
	if (radius < 0) {
		fprintf(stderr, "Radius should be an integer >= 0; exiting.");
		exit(1);
	}

	noextfname = remove_ext(inputfile, '.', '/');
	sprintf(seqoutfile, "%s-r%d-serial.bmp", noextfname, radius);

	bmp_read_img_from_file(inputfile, &imgin);
	bmp_clone_empty_img(&imgin, &imgout);

	bmp_rgb_alloc(&imgin);
	bmp_rgb_alloc(&imgout);

	printf("<<< Gaussian Blur (h=%d,w=%d,r=%d) >>>\n", imgin.header.height, 
	       imgin.header.width, radius);

	/* Image data to R,G,B */
	bmp_rgb_from_data(&imgin);

	/* Run & time serial Gaussian Blur */
	t1 = torc_gettime();
	for (i = 0; i < TORC_NODES; ++i) {
		exectime = timeit(gaussian_blur_serial, radius, &imgin, &imgout);
	}
	exectume = torc_gettime() - t1;

	/* Save the results (serial) */
	bmp_data_from_rgb(&imgout);
	bmp_write_data_to_file(seqoutfile, &imgout);

	printf("Total execution time: %lf\n", exectime_serial);

	bmp_img_free(&imgin);
	bmp_img_free(&imgout);

	return 0;
}
