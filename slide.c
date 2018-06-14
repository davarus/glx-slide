/*
 * GLX JPEG Slideshow Program
 *
 * Compile: gcc -o slide slide.c -lGL -ljpeg
 *
 * Copyright (C) 2004, David A. Russell
 * ALL RIGHTS RESERVED!
 */

#include <X11/Xlib.h>
#include <GL/glx.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <jpeglib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

float swidth, sheight;
int drandom = 0;
int showtime = 5;
int fadetime = 5;
const char *config;

typedef struct _image
{
	char *name;
	double width, height;
	double ax, ay;
	int glnum;
} image;

void readconfig (void)
{
	FILE *file;
	char line[512];

	if (!config)
		return;

	file = fopen(config, "r");
	if (!file)
		return;


	while (fgets(line, sizeof(line), file) != 0)
	{
		char *name, *value;

		if (strlen(line) == sizeof(line) && line[sizeof(line) - 1]
				!= '\n')
		{
			fclose(file);
			return;
		}

		for (name = line; *name == ' '; name++);
		for (value = strchr(name, '=');
				value && (*value == ' ' || *value == '=');
				value--);
		if (!value)
		{
			fclose(file);
			return;
		}
		value++;
		*value++ = '\0';
		while (*value == ' ' || *value == '=') value++;
		value[strlen(value) - 1] = '\0';

		if (!strcmp(name, "random"))
		{
			if (atoi(value))
				drandom = 1;
			else
				drandom = 0;
		}
		else if (!strcmp(name, "fadetime"))
		{
			fadetime = atoi(value);
		}
		else if (!strcmp(name, "showtime"))
		{
			showtime = atoi(value);
		}
		else if (!strcmp(name, "dir"))
		{
			chdir(value);
		}
	}

	fclose(file);
}

char *dstrdup (const char *s)
{
	int len;
	char *n;

	/* if null return null */
	if (!s)
		return NULL;

	/* get length */
	len = strlen(s) + 1;

	/* allocate space */
	n = malloc(len);
	if (!n)
		return NULL;

	/* copy */
	memcpy(n, s, len);

	/* return */
	return n;
}


# define R GLX_RED_SIZE
# define G GLX_GREEN_SIZE
# define B GLX_BLUE_SIZE
# define D GLX_DEPTH_SIZE
#define  A GLX_ALPHA_SIZE
#define  AA GLX_ACCUM_ALPHA_SIZE
# define I GLX_BUFFER_SIZE
# define DB GLX_DOUBLEBUFFER
# define ST GLX_STENCIL_SIZE

int attributeList[][20] = {
	{ GLX_RGBA, R, 8, G, 8, B, 8, D, 8, DB,       0 },
	{ GLX_RGBA, R, 4, G, 4, B, 4, D, 4, DB,       0 },
	{ I, 8,                       D, 8, DB,       0 },
	{ I, 4,                       D, 4, DB,       0 }
};

static Bool WaitForNotify(Display *d, XEvent *e, char *arg)
{
	(void)d;
	return(e->type == MapNotify) && (e->xmap.window == (Window)arg);
}

int find2b (int x)
{
	int v;

	for (v = 2; v < x; v = v << 1);
	return v;
}

/* XXX: Random is broken. */
char *getnextfile (const char *name, int random)
{
	DIR *dir;
	struct dirent *file;
	char *next;

	next = NULL;

	/* open dir */
	dir = opendir(".");
	if (!dir)
	{
		free(next);
		return NULL;
	}

	/* cycle through all entries */
	while ((file = readdir(dir)) != NULL)
	{
		const char *ext;
		int good = 0;

		/* ensure it's a jpeg */
		ext = strrchr(file->d_name, '.');
		if (!ext || strcmp(ext, ".jpg"))
			continue;

		/* Is it the next in line or random */
		if (!random)
		{
			if (!name)
			{
				if (!next || strcmp(next, file->d_name) > 0)
					good = 1;
			}
			else if (strcmp(name, file->d_name) < 0)
			{
				if (!next || strcmp(next, file->d_name) > 0)
					good = 1;
			}
		} else {
			if (!(rand() / (++random - 1)))
			{
				good = 1;
			}
		}

		/* we have a match */
		if (good)
		{
			struct stat status;

			/* regular file? */
			if (stat(file->d_name, &status))
				continue;

			if (S_ISREG(status.st_mode))
			{
				free(next);
				next = dstrdup(file->d_name);
				if (!next)
					break;
			}
		}
	}

	/* redo if needed */
	if (!random && name && !next)
	{
		closedir(dir);
		return getnextfile(NULL, random);
	}
	else if (name && !strcmp(name, next))
	{
		closedir(dir);
		return getnextfile(name, 1);
	}

	/* clean up and return */
	closedir(dir);
	return next;
}

int readjpeg (image *img)
{
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	JSAMPARRAY buffer;
	unsigned char *image;
	FILE *file;
	int width, height, components;
	double aspect, raspect;

	/* initialize jpeg stuff */
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_decompress(&cinfo);

	/* open file */
	file = fopen(img->name, "rb");
	if (!file)
	{
		jpeg_destroy_decompress(&cinfo);
		return 1;
	}
	jpeg_stdio_src(&cinfo, file);

	/* read header */
	jpeg_read_header(&cinfo, TRUE);
	jpeg_start_decompress(&cinfo);

	/* calculate sizes */
	width = find2b(cinfo.output_width);
	height = find2b(cinfo.output_height);
	components = cinfo.output_components;
	img->height = (double)cinfo.output_height / height;
	img->width = (double)cinfo.output_width / width;
	aspect = (double)cinfo.output_width / cinfo.output_height;
	raspect = (double)cinfo.output_height / cinfo.output_width;
	if (aspect > swidth / sheight)
	{
		img->ax = 1.0;
		img->ay = 1.0 - (aspect - swidth / sheight);
	}
	else
	{
		img->ax = 1.0 - (raspect - sheight / swidth);
		img->ay = 1.0;
	}

	/* allocate buffers */
	/* TODO:  Error checking... */
	buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo,
			JPOOL_IMAGE,
			cinfo.output_width * cinfo.output_components,
			1);
	image = malloc(4 * width * height);
	memset(image, 0, components * width * height);

	/* read in image */
	while (cinfo.output_scanline < cinfo.output_height)
	{
		int x, y;
		jpeg_read_scanlines(&cinfo, buffer, 1);
		y = (height - cinfo.output_height);
		x = (width - cinfo.output_width) / 2;
		memcpy(image + (cinfo.output_scanline - 1)
				* width * components,
				buffer[0], components * cinfo.output_width);
	}

	/* create a texture */
	glBindTexture(GL_TEXTURE_2D, img->glnum);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0,
			components, width,
			height, 0, (components == 3) ? GL_RGB : GL_LUMINANCE,
			GL_UNSIGNED_BYTE, image);

	/* clean up and return */
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	fclose(file);
	free(image);

	/* return any errors */
	return glGetError();
}


int displayimage (image *img, double fade)
{
	/* setup */
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glBlendFunc(GL_ONE, GL_ONE);
	glBindTexture(GL_TEXTURE_2D, img->glnum);
	glColor4f(fade, fade, fade, fade);
	glNormal3f(0.0, 0.0, 1.0);
	glLoadIdentity();

	/* render quad */
	glBegin(GL_QUADS);
	glTexCoord2f(0.0, img->height);
	glVertex3f(-img->ax, -img->ay, 0.1);
	glTexCoord2f(img->width, img->height);
	glVertex3f(img->ax, -img->ay, 0.1);
	glTexCoord2f(img->width, 0.0);
	glVertex3f(img->ax, img->ay, 0.1);
	glTexCoord2f(0.0, 0.0);
	glVertex3f(-img->ax, img->ay, 0.1);
	glEnd();

	/* clean up */
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);

	/* return any errors */
	return glGetError();
}


void displayusage (void)
{
	fprintf(stderr, "ERROR: Invalid Usage!\n");
	fprintf(stderr, "USAGE: slide [dir] [random] [showtime] [fadetime]\n");
}


int main(int argc, char**argv)
{
	Display *dpy;
	int screen;
	XVisualInfo *vi;
	Colormap cmap;
	XSetWindowAttributes swa;
	Window win;
	GLXContext cx;
	XEvent event;
	int img;
	image images[2];
	int i;


	/* Get arguments */
	if (argc > 2)
	{
		displayusage();
		return 0;
	}
	if (argc > 1)
	{
		config = argv[1];
	}

	/* get a connection   */
    	dpy   = XOpenDisplay(0);
    	if (!dpy) {
        	fprintf(stderr, "ERROR: Could not open display.\n");
        	return -1;
    	}
    	screen = DefaultScreen(dpy);

	/* get an appropriate visual */
	for (i = 0; i < sizeof(attributeList)/(sizeof(*attributeList)); i++)
	{
		vi = glXChooseVisual(dpy, DefaultScreen(dpy),
			attributeList[i]);
		if (vi)
			break;
	}
	if (!vi)
	{
		fprintf(stderr, "ERROR: Could not find suitable visual.\n");
		return -1;
	}
	
	/* create a GLX context */
	{
	XVisualInfo vi_in, *vi_out;
	int out_count;
	vi_in.screen = DefaultScreen(dpy);
	vi_in.visualid = XVisualIDFromVisual(vi->visual);
	vi_out = XGetVisualInfo (dpy, VisualScreenMask | VisualIDMask,
			&vi_in, &out_count);
	if (!vi_out)
	{
		fprintf(stderr, "ERROR: Internal X oddness\n");
		return -1;
	}
	
	cx = glXCreateContext(dpy, vi_out, 0, GL_TRUE);
	if (!cx) {
		fprintf(stderr, "ERROR: Could not create GLX conetext.\n");
		return -1;
	}
	}

	/* create a colormap */
	cmap = XCreateColormap(dpy, RootWindow(dpy, DefaultScreen(dpy)),
			vi->visual, AllocNone);
	if (!cmap) {
		fprintf(stderr, "ERROR: Could not allocate colormap.\n");
		return -1;
	}

	/* create window */
	swa.colormap = cmap;
	swa.override_redirect = 1;
	swa.event_mask = StructureNotifyMask;
	win = XCreateWindow(dpy, RootWindow(dpy, vi->screen), 0, 0,
		DisplayWidth(dpy, DefaultScreen(dpy)),
		DisplayHeight(dpy, DefaultScreen(dpy)),
		0, vi->depth, InputOutput, vi->visual,
		CWColormap | CWEventMask | CWOverrideRedirect, &swa);
	swidth = DisplayWidth(dpy, DefaultScreen(dpy));
	sheight = DisplayHeight(dpy, DefaultScreen(dpy));
	if (!win)
	{
		fprintf(stderr, "ERROR: Could not create window\n");
		return -1;
	}

	/* display Window */
	XMapWindow(dpy, win);

	/* initialize GL stuff */
	glXMakeCurrent(dpy, win, cx);
	glDrawBuffer(GL_BACK);
	glDisable(GL_LIGHTING);

	/* initialize images array */
	memset(images, 0, sizeof(images));
	images[0].glnum = 0;
	images[1].glnum = 1;

	/* main loop */
	img = 0;
	do
	{
		struct timeval stime, ctime;
		long diff;
		char *name;

		/* update config */
		readconfig();

		/* load next image */
		free(images[img].name);
		name = dstrdup(images[(img + 1) & 1].name);
		do
		{
			images[img].name = getnextfile(name, drandom);
			free(name);
			name = dstrdup(images[img].name);
		} while (readjpeg(&images[img]));
		free(name);

		/* fade between images */
		gettimeofday(&stime, NULL);
		do
		{
			struct timespec sleeptime;

			/* get current time */
			gettimeofday(&ctime, NULL);
			diff = (ctime.tv_sec - stime.tv_sec) * 1000000;
			if (stime.tv_usec > ctime.tv_usec)
			{
				diff -= stime.tv_usec - ctime.tv_usec;
			}
			else
			{
				diff += ctime.tv_usec - stime.tv_usec;
			}

			/* display image */
			glClear(GL_COLOR_BUFFER_BIT);
			displayimage(&images[(img + 1) & 1], 1.0
				- (double) diff / (fadetime * 1000000.0));
			displayimage(&images[img],
				(double)diff / (fadetime * 1000000.0));
			glFinish();
			glXSwapBuffers(dpy, win);

			/* sleep */
			memset(&sleeptime, 0, sizeof(sleeptime));
			sleeptime.tv_nsec = 100000;
			while (nanosleep(&sleeptime, &sleeptime));
		} while (diff < fadetime * 1000000);
		
		/* display image */
		glClear(GL_COLOR_BUFFER_BIT);
		displayimage(&images[img], 1.0);
		glFinish();
		glXSwapBuffers(dpy, win);

		/* wait */
		{
			char *filetime;
			filetime = strchr(images[img].name, '.');
			if (filetime)
			{
				filetime++;
				if (strcmp("jpg", filetime))
					sleep(atoi(filetime));
				else
					sleep(showtime);
			}
			else
			{
				sleep(showtime);
			}
		}

		/* next image */
		img = (img + 1) & 1;
	} while (1);

	/* exit cleanly */
    	XCloseDisplay(dpy);
    	exit(0);
}

