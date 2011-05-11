

#include	"FoldMask.h"

#include	"Cmdline.h"
#include	"Disk.h"
#include	"PipeFiles.h"
#include	"ImageIO.h"
#include	"Maths.h"
#include	"Geometry.h"
#include	"CPicBase.h"

#include	"numerical_recipes.h"


/* --------------------------------------------------------------- */
/* CArgs_tiny ---------------------------------------------------- */
/* --------------------------------------------------------------- */

class CArgs_tiny {

public:
	double	energyT;
	double	fmTOverride;
	char	*infile,
			*outfile;
	int		Z;
	bool	oneregion,
			transpose,
			dumphist;

public:
	CArgs_tiny()
	{
		energyT	= 10.0;
		fmTOverride		= 0.0;
		infile			= NULL;
		outfile			= NULL;
		Z				= -1;
		oneregion		= false;
		transpose		= false;
		dumphist		= false;
	};

	void SetCmdLine( int argc, char* argv[] );
};

/* --------------------------------------------------------------- */
/* Statics ------------------------------------------------------- */
/* --------------------------------------------------------------- */

static CArgs_tiny	gArgs;






/* --------------------------------------------------------------- */
/* SetCmdLine ---------------------------------------------------- */
/* --------------------------------------------------------------- */

void CArgs_tiny::SetCmdLine( int argc, char* argv[] )
{
// parse command line args

	if( argc < 4 ) {
		printf(
		"Usage: tiny <z> <tif-file> <fold-mask-file> [option]\n" );
		exit( 42 );
	}

	for( int i = 1; i < argc; ++i ) {

		if( argv[i][0] != '-' ) {

			if( Z == -1 )
				Z = atoi( argv[i] );
			else if( !infile )
				infile = argv[i];
			else
				outfile = argv[i];
		}
		else if( GetArg( &energyT, "-energy=%lf", argv[i] ) )
			printf( "Energy Threshold now %f.\n", energyT );
		else if( GetArg( &fmTOverride, "-fmto=%lf", argv[i] ) ) {

			printf( "Fold Mask Threshold over-ridden, now %f.\n",
			fmTOverride );
		}
		else if( IsArg( "-one", argv[i] ) ) {

			oneregion = true;
			printf( "Using just one region.\n" );
		}
		else if( IsArg( "-h", argv[i] ) ) {

			dumphist = true;
			printf( "Dumping a histogram.\n" );
		}
		else {
			printf( "Did not understand option '%s'.\n", argv[i] );
			exit( 42 );
		}
	}

// For backwards compatibility, also accept the env. variable

	const char	*pe = getenv( "FoldMaskThreshold" );

	if( pe ) {

		fmTOverride = atof( pe );

		printf(
		"Environment variable over-ride of threshold to %f.\n",
		fmTOverride );

		printf(
		"--- This is obsolete ---  Use the -fmto option instead.\n" );
	}
}

/* --------------------------------------------------------------- */
/* WriteFOLDMAPEntry --------------------------------------------- */
/* --------------------------------------------------------------- */

static void WriteFOLDMAPEntry(
	const char	*simg,
	const char	*sfm,
	int			Z,
	int			ncr )
{
	CMutex	M;
	char	name[256];

	sprintf( name, "fm_%d", Z );

	if( M.Get( name ) ) {

		FILE *f = fopen( "fm.same", "a" );

		if( f ) {

			const char	*src = simg;
			Til2Img		t2i;

			if( strstr( simg, ".mrc" ) ) {

				if( !ReadTil2Img( t2i, Z, atoi( sfm ), stdout ) )
					exit( 42 );

				src = t2i.path;
			}

			fprintf( f,
			"FOLDMAP '%s' ../%d/%s %d\n", src, Z, sfm, ncr );
			fflush( f );
			fclose( f );
		}
	}

	M.Release();
}

/* --------------------------------------------------------------- */
/* SingleValueDominates ------------------------------------------ */
/* --------------------------------------------------------------- */

// Tells if a single pixel value dominates the histogram.
//
static bool SingleValueDominates( const vector<int> &histo )
{
	int	total = 0, nh = histo.size();

	for( int i = 0; i < nh; ++i )
		total += histo[i];

	for( int i = 0; i < nh; ++i ) {

		if( histo[i] > total/2 ) {

			printf(
			"Dominant value %d, %d occurences in %d pixels.\n",
			i, histo[i], total );

			return true;
		}
	}

	return false;
}

/* --------------------------------------------------------------- */
/* IsTooGaussian ------------------------------------------------- */
/* --------------------------------------------------------------- */

// Tells whether a histogram is too well explained by a gaussian.
//
static bool IsTooGaussian( const vector<int> &histo )
{
	int	nhist = histo.size();

// Find the mean and std deviation through the
// following exceedingly brute force technique.

	MeanStd	mh;
	double	mean, std;
	int		ndv = 0;	// number of different values

	for( int i = 0; i < 256; ++i ) {

		// accumulate element 'value' times
		for( int j = 0; j < histo[i]; ++j )
			mh.Element( i );

		ndv += (histo[i] > 0);
	}

	mh.Stats( mean, std );

	printf( "\nMean %f, std %f, # different values %d.\n",
		mean, std, ndv );

// Unusual case; print them

	if( ndv <= 20 ) {

		for( int i = 0; i < nhist; ++i ) {

			if( histo[i] > 0 )
				printf( "[%d %d] ", i, histo[i] );
		}

		printf( "\n" );
	}

// Now do a least squares fit. Should yield little diff, in theory.
// Create y array (same data as double); s array (const std dev).

	VecDoub	x( nhist ), y( nhist ), s( nhist );
	MeanStd orig;

	for( int i = 0; i < nhist; ++i ) {

		x[i] = i;
		y[i] = histo[i];
		s[i] = 1.0;
		orig.Element( histo[i] );
	}

	VecDoub	residual = y;
	double	omean, ostd;

	orig.Stats( omean, ostd );

	printf( "Histogram values: mean %f rms %f.\n", omean, ostd );

// Now try a 1 gaussian fit

	VecDoub	a( 3 );
	double	sum = 0.0;
	double	cnt = 0;

	for(
		int i = max( 0, int(mean-std) );
		i <= min( 255, int(mean+std) );
		++i ) {

		sum += histo[i];
		++cnt;

		printf( "%d ", histo[i] );
	}

	printf( "\n" );
	printf( "Boxcar average is %f.\n", sum/cnt );

	a[0] = 1.5 * sum/cnt;	// estimated peak value
	a[1] = mean;			// where it is
	a[2] = std * sqrt( 2 );	// and the divisor term

	printf( "Before fit fit: height %f, loc %f, width %f.\n",
		a[0], a[1], a[2] / sqrt( 2 ) );

	Fitmrq f( x, y, s, a, fgauss );

	try {
		f.fit();
	}
	catch(int) {		// if the fitting procedure blows up
		return false;	// it's safe to assume it's not very gaussian
	}

	printf( "After fit: height %f, loc %f, width %f.\n",
		f.a[0], f.a[1], f.a[2]/sqrt(2) );

// Now look at the residual

	MeanStd	m;
	int		nx = x.size();

	for( int i = 0; i < nx; ++i ) {

		VecDoub DyDa( 3 );
		double	yy;

		fgauss( x[i], f.a, yy, DyDa );

		residual[i] = y[i] - yy;

		m.Element( residual[i] );
	}

	m.Stats( mean, std );

	printf( "Residuals: mean %f, RMS about mean %f.\n", mean, std );

	return std < ostd/8;	// a guess
}

/* --------------------------------------------------------------- */
/* ImageToFoldMap ------------------------------------------------ */
/* --------------------------------------------------------------- */

// Here we convert an input image to a map where 0 = on fold, 1=region 1, 2=region 2, etc.
// Normally the pipeline does this, but we do it here for standalone testing.
//
// Return maximal region id.
//
static int ImageToFoldMap(
	PicBase	&pic,
	uint8*	FoldMask,
	bool	remove_low_contrast = false,
	bool	one_region = false,
	double	FoldMaskThresholdOverride = 0.0 )
{

// Here, define the connected region(s) on the above layer.
int w = pic.w;
int h = pic.h;
uint8 *raster = pic.raster;
int npixels = w * h;

// if they asked for one region, do that
if( one_region ) {
    printf("Generating one uniform region\n");
    memset( FoldMask, 1, npixels );
    return 1;
}

// First, find the mean and standard deviation of the 'real'
// non-saturated pixels. Create a mask to remove pixels from
// regions that look too much like gaussians, or that are
// dominated by a single value. These are almost always just
// a flat color.

vector<uint8> goodp(npixels, 1);  // start by assuming all pixels are good
bool remove_too_gaussian = true;
if( remove_too_gaussian ) {
    // If there are chunks that are too gaussian, don't use them
    int nx = (w-256)/128+1;     // origin steps by 128; tile size is 256
    double dx = (w-256)/double(nx);  // idea is to tile with overlapping 128x128 squares
    int ny = (w-256)/128+1;
    double dy = (w-256)/double(ny);
    for(double y=0; y< (h-255); y += dy) {
	int ymin = min(int(y), h-256);  // just in case
	int ymax = ymin+255;
	for(double x=0; x< (w-255); x += dx) {
	    int xmin = min(int(x), w-256);
	    int xmax = xmin+256;
	    vector<int> histo(256,0);
	    //printf("xmin, ymin = (%d %d)\n", xmin, ymin);
	    for(int ix=xmin; ix <= xmax; ix++)
		for(int iy=ymin; iy <= ymax; iy++)
		    histo[raster[ix + w*iy]]++;
	    if( SingleValueDominates( histo ) || IsTooGaussian( histo ) ) {
		for(int ix=xmin; ix <= xmax; ix++)
		    for(int iy=ymin; iy <= ymax; iy++)
			goodp[ix + w*iy] = 0;
		}
	    }
	}
    }
//write_png_file("goodp.png", &goodp[0], w, h);  // for debugging
// Ignore any that are too close to saturated light or dark.
int SAT=3;  // how close to the boundary do you need to be to be considered 'saturated'
MeanStd m;
int ngood = 0;
for(int x=0; x<w; x++) {
    for(int y=0; y<h; y++) {
        if( goodp[x + w*y] ) {
	    ngood++;
            uint8 pix = raster[x + w*y];
            if( goodp[x + w*y] && pix >= SAT && pix <= 255-SAT )
	        m.Element(pix);
	    }
	}
    }
printf("%d real pixels, %f percent\n", m.HowMany(), m.HowMany()*100.0/ngood);

if(m.HowMany()/double(ngood) < 0.9) {
    printf("Saturated image!  Retrying with SAT=1\n");
    SAT = 1;
    m.Reset();
    for(int x=0; x<w; x++) {
        for(int y=0; y<h; y++) {
            if( goodp[x + w*y] ) {
                uint8 pix = raster[x + w*y];
                if( goodp[x + w*y] && pix >= SAT && pix <= 255-SAT )
	            m.Element(pix);
		}
	    }
	}
    printf("%d real pixels, %f percent\n", m.HowMany(), m.HowMany()*100.0/ngood);
    }

double mean, std;
m.Stats(mean, std);  // find existing statistics
printf("Of the above image points, mean= %f and std dev = %f\n", mean, std);

// If pure white is outside the range of practical values, then it too
// should be ignored, as it is not useful info.  But if 255 is within the
// 'practical' range, we don't want to cut these pixels out
if( mean + 2.5*std < 255 ) {
    printf("Removing white pixels\n");
    for(int i=0; i<npixels; i++) {
	uint8 pix = raster[i];
	if (pix > 255-SAT)           // too bright is just as bad as too dark - not useful info
	    raster[i] = 0;
	}
    }

// Now find the connected regions.  We want to ignore very black pixels as folds, and those
// near them (distance = D) as unreliable, and might connect regions which should be disconnected.
// However, if the image as a whole is too black, this will result in lots of disconnected
// regions, as there will be many black pixels and many near them.  So reset the parameters in
// these cases.
int D=10;
int nbig = 0;
vector<ConnRegion> cr;
double thresh = 4.0;          // we would like 4 std dev, if possible
if( mean - thresh*std < 1 ) {  // but if not, pick a value that is feasible
    thresh = mean/std*0.95;   // set to 95% of the way to 0
    printf("Forced to reduce threshold to %f std dev.\n", thresh);
    if( thresh < 2.0 ) {       // we'll get too many black, and fragment the area
	thresh = (mean - 0.5)/std;  // set threshold to a pixel value of 0.5 (on scale of 255)
	printf("Desperate measures.  Changing threshold to %f, so only v=0 pixels are out.\n", thresh);
        printf("Also disabling black pixel expansion\n");
	D = 0;
	}
    }
if( FoldMaskThresholdOverride != 0.0 ) {
    thresh = FoldMaskThresholdOverride;
    printf("Explicit over-ride of threshold; set to %f\n", thresh);
    }

// pixels to vector
vector<double> v(npixels);
vector<double> vorig(npixels);	//@@@
for(int i=0; i<npixels; i++) {
    int y = i / w;
    int x = i - w * y;   // pixels next to background pixels should also be black
    int pix = raster[i];
    v[i] = (pix-mean)/std;
     vorig[i] = v[i];	//@@@
   }

if( remove_low_contrast ) {
    // If there are chunks with no contrast, get rid of them.
    int nx = (w-128)/32+1;     // origin steps by 32; tile size is 128
    double dx = (w-128)/double(nx);  // idea is to tile with overlapping 128x128 squares
    int ny = (w-128)/32+1;
    double dy = (w-128)/double(ny);
    vector<int> zap_me;        // save list of pixels to be zapped, since cannot zap on the fly without
			       // munging the overlapping squares
    for(double y=0; y< (h-127); y += dy) {
	int ymin = min(int(y), h-128);  // just in case
	int ymax = ymin+127;
	for(double x=0; x< (w-127); x += dx) {
	    int xmin = min(int(x), w-128);
	    int xmax = xmin+127;
	    vector<double>local(128*128);
	    //printf("xmin, ymin = (%d %d)\n", xmin, ymin);
	    for(int ix=xmin; ix <= xmax; ix++)
		for(int iy=ymin; iy <= ymax; iy++)
		    local[(ix-xmin) + 128*(iy-ymin)] = v[ix + w*iy];
	    if( IsLowContrast(local, std) ) {
		for(int ix=xmin; ix <= xmax; ix++)
		    for(int iy=ymin; iy <= ymax; iy++)
			zap_me.push_back(ix + w*iy);
		}
	    }
	}
    printf("Zap list has %d entries\n", zap_me.size());
    for(int i=0; i<zap_me.size(); i++)
	v[zap_me[i]] = -thresh - 100.0;  // sure to be bad
    }

// for those that should not be included, remove all pixels within distance D as well.  This will
// remove tiny filaments connected big regions together.
vector<int> remove;  // indices of points to be removed.
for(int i=0; i<npixels; i++)
    if( v[i] < -thresh )
	remove.push_back(i);
for(int ii=0; ii<remove.size(); ii++) {
    int i = remove[ii];
    int y = i / w;
    int x = i - w * y;
    int x0 = max(x-D,0);
    int x1 = min(x+D, w-1);
    int y0 = max(y-D,0);
    int y1 = min(y+D,h-1);
    for(int xx = x0; xx <= x1; xx++) {
	for(int yy = y0; yy <= y1; yy++) {
	    v[xx + w*yy] = -thresh - 1.0;   // set to a value that is more than enough black
            }
        }
    }


// -----------------------------------------------------
#if 0

// Now find the connected regions

	for( int i = 0; i < npixels; ++i ) {

		if( v[i] > -thresh ) {

			ConnRegion	c;
			int			npts;

			npts = Propagate( c.pts, v, w, h, i,
					-thresh, -thresh - 1.0 );

			printf(
			"ImageToFoldMap: ConnRegion with %d pixels.\n", npts );

			if( npts > 90000 ) {	// want 100k, but we shrank it
				cr.push_back( c );
				++nbig;
			}
		}
	}

// Final accounting

   SetBoundsAndColors( cr, FoldMask, w, h );

#endif
// -----------------------------------------------------
#if 1
int start = 0;
// find all connected regions of 'reasonable' values.  The background should be about
// -4 or -5 on this scale.
for(int k=0;;){
    int i;
    for(i=start; i<npixels; i++)  // find first good pixel
	 if (v[i] > -thresh)
	    break;
    if (i >= npixels)             // if there are not any, then all done
	break;
    // found at least one pixel.  Find all connected ones.
    //printf("found v[%d] = %f\n", i, v[i]);
    start = i+1;  // next time, start here...
    ConnRegion c;
    stack<int> st;
    //printf("push %d\n", i);
    st.push(i);
    while(!st.empty()) {
	int j = st.top();
	//printf("pop %d, val %f\n",j, v[j]);
	st.pop();
	//if (fabs(v[j]) < thresh) {
	if (v[j] > -thresh) {
	    int y = j/w;
	    int x = j-y*w;
	    //printf("j, x, y = %d %d %d, v=%f\n", j, x, y, v[j]);
	    Point p(x,y);
	    c.pts.push_back(p);
	    v[j] = -thresh - 1.0;  // make this point not eligible any more
	    if (x-1 >= 0) st.push(j-1);
	    if (x+1 < w)  st.push(j+1);
	    if (y-1 >= 0) st.push(j-w);
	    if (y+1 < h)  st.push(j+w);
	    }
	}
    //printf("Connected region of %d pixels\n", c.pts.size());
    if (c.pts.size() > 90000) {  // want 100k, but we shrank it
	nbig++;
	cr.push_back(c);
        }
    }
printf("got a total of %d regions that are big enough\n", nbig);
// Now color in the map with the region ids. Also increase in size by amount D, since
// we shrank by that amount earlier.
for(int i=0; i<npixels; i++)
    FoldMask[i] = 0;
for(int i=0; i<nbig; i++) {
    for(int k=0; k<cr[i].pts.size(); k++) {
        int x = int(cr[i].pts[k].x);
        int y = int(cr[i].pts[k].y);
	int x0 = max(x-D,0);
	int x1 = min(x+D, w-1);
	int y0 = max(y-D,0);
	int y1 = min(y+D,h-1);
	for(int xx = x0; xx <= x1; xx++) {
	    for(int yy = y0; yy <= y1; yy++) {
                if (vorig[xx+yy*w] >= -thresh)
		FoldMask[xx+yy*w] = i+1;
		}
	    }
	}
   }

// find the bounding boxes
for(int i=0; i<nbig; i++) {
    cr[i].B.L = BIG; cr[i].B.B = BIG; cr[i].B.R = -BIG; cr[i].B.T = -BIG;
    }
for(int i=0; i<npixels; i++) {
    int k = FoldMask[i] - 1;
    if (k >= 0) { // part of some region
        int y = i/w;
        int x = i-y*w;
	cr[k].B.L = min(cr[k].B.L, x);
	cr[k].B.R = max(cr[k].B.R, x);
	cr[k].B.B = min(cr[k].B.B, y);
	cr[k].B.T = max(cr[k].B.T, y);
	}
    }

// Print the bounding boxes
for(int k=0; k<nbig; k++) {
    printf("k=%d, #points %d, region size is [%d %d] in x, [%d %d] in y\n", k, cr[k].pts.size(),
     cr[k].B.L, cr[k].B.R, cr[k].B.B, cr[k].B.T);
    }
#endif
// -----------------------------------------------------

   return cr.size();
}

/* --------------------------------------------------------------- */
/* main ---------------------------------------------------------- */
/* --------------------------------------------------------------- */

int main( int argc, char **argv )
{
/* ------------------ */
/* Parse command line */
/* ------------------ */

	gArgs.SetCmdLine( argc, argv );

/* -------- */
/* Load src */
/* -------- */

	PicBase	p;

	p.LoadOriginal( gArgs.infile, stdout, gArgs.transpose );

/* --------------- */
/* Save src as png */
/* --------------- */

	if( strstr( gArgs.infile, ".mrc" ) ) {

		char	path[64];
		int		id = atoi( gArgs.outfile );

		sprintf( path, "%d/nmrc_%d_%d.png", id, gArgs.Z, id );
		Raster8ToPng8( path, p.original, p.w, p.h );
	}

// Create two different fold masks - one optimized for alignment,
// with all low constrast regions removed, and one optimized for
// drawing, with regions as big as possible. But the two must be
// consistent, so make sure this is the case.

	int		np = p.w * p.h;
	bool	remove_low_contrast = true;
	uint8	*FoldMaskAlign = (uint8*)malloc( np );
	int		ncr = ImageToFoldMap( p, FoldMaskAlign,
					remove_low_contrast, gArgs.oneregion,
					gArgs.fmTOverride );

	uint8 *FoldMaskDraw = (uint8*)malloc( np );
	ImageToFoldMap( p, FoldMaskDraw,
		!remove_low_contrast, gArgs.oneregion,
		gArgs.fmTOverride );

	Raster8ToTif8( gArgs.outfile, FoldMaskAlign, p.w, p.h );
	WriteFOLDMAPEntry( gArgs.infile, gArgs.outfile, gArgs.Z, ncr );

// Now the additional removal of low contrast regions means that
// one region in the drawing mask may be split into multiple
// regions in the alignment mask. Find the ID with the most pixels
// in common, and use that. First, create an identity map for pixel
// values.

	vector<int>	map( 256 );

	int	nm = map.size();

	for( int i = 0; i < nm; i++ )
		map[i] = i;

	int n = 1;	// number of pixels found on previous layer

	for( int mask = 1; mask <= 255 && n != 0; ++mask ) {

		// Does this value exist in the drawing mask?
		// If so, what's the most common value in
		// the alignment mask?

		vector<int>	counts( 255, 0 );

		n = 0;

		for( int i = 0; i < np; ++i ) {

			if( FoldMaskDraw[i] == mask ) {
				++counts[FoldMaskAlign[i]];
				++n;
			}
		}

		if( n > 0 ) {

			// Found some.
			// Find the most common non-zero value, if any.

			int	max_val = 0;
			int	max_ind = 0;

			for( int j = 1; j < 255; ++j ) {

				if( counts[j] > max_val ) {
					max_val = counts[j];
					max_ind = j;
				}
			}

			printf( "Mapping %d to %d on fold mask for drawing.\n",
				mask, max_ind );

			map[mask] = max_ind;
		}
	}

// Now change all the pixels

	for( int i = 0; i < np; ++i ) {

		if( FoldMaskAlign[i] != 0 )
			FoldMaskDraw[i] = FoldMaskAlign[i];
		else
			FoldMaskDraw[i] = map[FoldMaskDraw[i]];
	}

// Now write it out.

	char	*suf = strstr( gArgs.outfile, ".tif" );

	if( suf == NULL )
		suf = strstr( gArgs.outfile, ".png" );

	if( suf != NULL ) {

		*suf = 0;	// gArgs.outfile now just the root part

		char	fname[256];

		sprintf( fname, "%s.png", gArgs.outfile );
		Raster8ToPng8( fname, FoldMaskAlign, p.w, p.h );

		sprintf( fname, "%sd.tif", gArgs.outfile );
		Raster8ToTif8( fname, FoldMaskDraw, p.w, p.h );

		sprintf( fname, "%sd.png", gArgs.outfile );
		Raster8ToPng8( fname, FoldMaskDraw, p.w, p.h );
	}

	return 0;
}


